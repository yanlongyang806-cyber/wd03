/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerTreeEval.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatEnums.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "entCritter.h"
#include "Expression.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GamePermissionsCommon.h"
#include "GamePermissionsCommon_h_ast.h"
#include "mission_common.h"
#include "OfficerCommon.h"
#include "Powers.h"
#include "Player.h"
#include "StringCache.h"
#include "PowerTree_h_ast.h"
#include "Powers_h_ast.h"
#include "PowersEnums_h_ast.h"
#include "inventoryCommon.h"
#include "species_common.h"
#include "Guild.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static Entity *GetSource(ExprContext *pContext)
{
	Entity *e = exprContextGetVarPointerUnsafe(pContext, "Source");
	if(e)
		return e;
	return exprContextGetSelfPtr(pContext);
}

static PTNode *GetNode(ExprContext *pContext)
{
	return exprContextGetVarPointerUnsafe(pContext, "CurNode");
}


// Fetches the static context used for generating and evaluating expressions in PowerTreeRespecConfig
ExprContext *powerTreeEval_GetContextRespec(void)
{
	static ExprContext *s_pContext = NULL; 
	
	if(s_pContext==NULL)
	{
		ExprFuncTable* stTable = exprContextCreateFunctionTable();
		s_pContext = exprContextCreate();

		exprContextAddFuncsToTableByTag(stTable,"CEFuncsGeneric");
		exprContextAddFuncsToTableByTag(stTable,"PTECharacter");
		exprContextAddFuncsToTableByTag(stTable,"PTERespec");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext, stTable);
		exprContextSetAllowRuntimePartition(s_pContext);
	}

	return s_pContext;
}

int entity_PointsSpentUnderLevel(Entity *pEnt, const char *pchPoints, int iLevelCap)
{
	int i, iPointsSpent = 0;

	if(pEnt && pEnt->pChar)
	{
		for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1;i>=0;i--)
		{
			iPointsSpent += entity_PointsSpentInTreeUnderLevel(CONTAINER_NOCONST(Entity, pEnt),CONTAINER_NOCONST(PowerTree, pEnt->pChar->ppPowerTrees[i]),pchPoints,iLevelCap);
		}
	}

	return iPointsSpent;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetPointsSpentUnderLevel);
int Player_ExprGetPointsSpentUnderLevel(SA_PARAM_OP_VALID Entity *pPlayer, const char *pchPoints, int iLevel)
{
	if(pPlayer)
		return entity_PointsSpentUnderLevel(pPlayer,pchPoints,iLevel);

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetRequiredPointsSpentForRank);
int Player_ExprGetRequiredPointsSpentForRank(SA_PARAM_OP_VALID Entity *pPlayer)
{
	if(pPlayer)
	{
		S32 iRank = Officer_GetRank(pPlayer);
		return Officer_GetRequiredPointsForRank(pPlayer,pPlayer,iRank,false);
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetRequiredPointsSpentAtRank);
int ExprGetRequiredPointsSpentForRank(SA_PARAM_OP_VALID Entity *pPlayer, S32 iRank)
{
	return Officer_GetRequiredPointsForRank(pPlayer,pPlayer,iRank,false);
}

AUTO_EXPR_FUNC(PTERespec) ACMD_NAME(GetRequiredPointsSpentForRank);
int Officer_ExprGetRequiredPointsSpentForRank(ExprContext *pContext)
{
	Entity *pEntity = GetSource(pContext);

	return Player_ExprGetRequiredPointsSpentForRank(pEntity);
}

AUTO_EXPR_FUNC(PTENode) ACMD_NAME(EnhTypeOwned);
int GetEnhTypeOwned(ExprContext* pContext, ACMD_EXPR_DICT(PTEnhTypeDef) const char *pchEnhType)
{
	PTNode *pNode = GetNode(pContext);
	PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
	int i,c,iReturn = 0;

	for(i=0;i<eaSize(&pNode->ppEnhancements);i++)
	{
		PowerDef *pPowerDef = GET_REF(pNode->ppEnhancements[i]->hDef);
		for(c=0;c<eaSize(&pNodeDef->ppEnhancements);c++)
		{
			if(pPowerDef == GET_REF(pNodeDef->ppEnhancements[c]->hPowerDef))
			{
				PTEnhTypeDef *pEnhType = GET_REF(pNodeDef->ppEnhancements[c]->hEnhType);
				
				if(strcmp(pEnhType->pchEnhType,pchEnhType) == 0)
					iReturn ++;

				break;
			}
		}
	}

	return iReturn;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsCritter);
bool IsCritter(ExprContext* pContext)
{
	Entity *pSrcEnt = GetSource(pContext);

	return pSrcEnt ? pSrcEnt->pCritter != NULL : false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsOfFaction);
bool IsOfFaction(ExprContext *pContext, const char *pchFactionName)
{
	Entity *pSrcEnt = GetSource(pContext);
	CritterFaction *pFaction = GET_REF(pSrcEnt->hFaction);
	CritterFaction *pSubFaction = GET_REF(pSrcEnt->hSubFaction);

	if(pFaction && pchFactionName && stricmp(pFaction->pchName,pchFactionName) == 0)
		return true;
	else if (pSubFaction && pchFactionName && stricmp(pSubFaction->pchName,pchFactionName) == 0)
		return true;

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(HasPuppetOfClass);
bool HasPuppetOfClass(ExprContext* pContext,const char *pchClassName)
{
	Entity* e = GetSource(pContext);
	CharacterClass *pClassCheck = RefSystem_ReferentFromString("CharacterClass", pchClassName);

	if (e && e->pChar && pClassCheck) {
		CharacterClass *pClass = GET_REF(e->pChar->hClass);

		if (pClass == pClassCheck) {
			return true;
		}

		if(e->pSaved && e->pSaved->pPuppetMaster)
		{
			int i;
			for(i=0;i<eaSize(&e->pSaved->pPuppetMaster->ppPuppets);i++)
			{
				// Only look at ACTIVE puppets. Inactive puppets do not count.
				if(e->pSaved->pPuppetMaster->ppPuppets[i]->eState == PUPPETSTATE_ACTIVE
					&& e->pSaved->pPuppetMaster->curID != e->pSaved->pPuppetMaster->ppPuppets[i]->curID)
				{
					// THIS IS BAD - Since this is called from inside transactions (which itself is BAD),
					//  its success requires the ObjectDB to have filled in the relevant subscriptions,
					//  which is NOT guaranteed.
					//Entity *pPuppet = GET_REF(e->pSaved->pPuppetMaster->ppPuppets[i]->hEntity);
					char idBuf[128];
					Entity *pPuppet = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(e->pSaved->pPuppetMaster->ppPuppets[i]->curID, idBuf));
					pClass = pPuppet && pPuppet->pChar ? GET_REF(pPuppet->pChar->hClass) : NULL;

					if(pClass == pClassCheck) {
						return true;
					}
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(OwnsNode);
bool OwnsNode(ExprContext* pContext, const char *pchNodeName)
{
	Entity *pSrcEnt = GetSource(pContext);
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNodeName);
	
	if(pNodeDef)
		return character_FindPowerTreeNode(pSrcEnt->pChar,pNodeDef,NULL) ? true : false;

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsOfClass);
bool IsOfClass(ExprContext* pContext,const char *pchClassName)
{
	Entity *pSrcEnt = GetSource(pContext);
	CharacterClass *pClass = pSrcEnt && pSrcEnt->pChar ? GET_REF(pSrcEnt->pChar->hClass) : NULL;

	if(pClass && pchClassName)
		return stricmp(pClass->pchName,pchClassName) == 0;

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsOfClassType);
bool IsOfClassType(ExprContext* pContext,const char *pchClassTypeName)
{
	Entity *pSrcEnt = GetSource(pContext);
	CharacterClass *pClass = pSrcEnt && pSrcEnt->pChar ? GET_REF(pSrcEnt->pChar->hClass) : NULL;
	CharClassTypes eType = StaticDefineIntGetInt(CharClassTypesEnum,pchClassTypeName);

	if(pClass)
		return pClass->eType == eType;

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(IsOfClassCategory);
bool IsOfClassCategory(ExprContext* pContext,const char *pchClassCategoryName)
{
	Entity *pSrcEnt = GetSource(pContext);
	CharacterClass *pClass = pSrcEnt && pSrcEnt->pChar ? GET_REF(pSrcEnt->pChar->hClass) : NULL;
	CharClassCategory eCategory = StaticDefineIntGetInt(CharClassCategoryEnum,pchClassCategoryName);

	if(pClass)
		return pClass->eCategory == eCategory;

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(Level);
int GetCharacterLevel(ExprContext* pContext)
{
	Entity *pSrcEnt = GetSource(pContext);

	if (pSrcEnt)
		return entity_CalculateFullExpLevelSlow(pSrcEnt);

	return 0;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TrainingLevel);
int GetCharacterTrainingLevel(ExprContext *pContext)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	int iResult = 0;

	if(pSrc)
	{
		iResult = character_FindTrainingLevel(pSrc);
	}

	return iResult;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TrainingLevelIn);
int GetCharacterTrainingLevelIn(ExprContext *pContext, const char *pchTableName)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	int iResult = 0;

	if(pSrc)
	{
		iResult = character_Find_TableTrainingLevel(pSrc, pchTableName);
	}

	return iResult;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EnhPointsSpent);
int GetCharacterEnhPointsSpent(ExprContext *pContext)
{
	Entity *pSrcEnt = GetSource(pContext);
	return entity_PointsSpent(CONTAINER_NOCONST(Entity, pSrcEnt),"EnhPoints");
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(PointsSpentIn);
int GetCharacterPointsSpentIn(ExprContext *pContext, const char *pchName)
{
	Entity *pSrcEnt = GetSource(pContext);
	return entity_PointsSpent(CONTAINER_NOCONST(Entity, pSrcEnt),pchName);
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreePointsSpent);
int GetCharacterTreePointsSpent(ExprContext *pContext)
{
	Entity *pSrcEnt = GetSource(pContext);
	return entity_PointsSpent(CONTAINER_NOCONST(Entity, pSrcEnt),"TreePoints");
}

// Returns the Entity's points remaining of the given point type
AUTO_EXPR_FUNC(PTECharacter);
int PointsRemaining(ExprContext *pContext, const char *pchPoints)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	int iResult = 0;
	if(pEnt)
	{
		iResult = entity_PointsRemaining(NULL, CONTAINER_NOCONST(Entity, pEnt), NULL, pchPoints);
	}
	return iResult;
}

// Returns the Entity's points spent of the given point type
AUTO_EXPR_FUNC(PTECharacter);
int PointsSpent(ExprContext *pContext, const char *pchName)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	return entity_PointsSpent(CONTAINER_NOCONST(Entity, pEnt),pchName);
}

// Returns the Entity's points permanently spent of the given point type
AUTO_EXPR_FUNC(PTECharacter);
int PointsSpentPermanent(ExprContext *pContext, const char *pchPoints)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	if(pEnt && pEnt->pChar)
	{
		CharacterPointSpent *pSpent = eaIndexedGetUsingString(&pEnt->pChar->ppPointSpentPowerTrees,pchPoints);
		if(pSpent)
		{
			return pSpent->iSpent;
		}
	}
	return 0;
}

// Returns the points returned of the given type this far into the respec in the context
AUTO_EXPR_FUNC(PTERespec);
int PointsRespeced(ExprContext *pContext, const char *pchPoints)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	PowerTreeSteps *pSteps = exprContextGetVarPointerUnsafe(pContext,"Steps");
	MultiVal *pMV = exprContextGetSimpleVar(pContext,"StepIndex");
	int iStepIndex = pMV ? MultiValGetInt(pMV, NULL) : -1;
	int iRespeced = 0;
	if(pEnt && pSteps)
	{
		while(iStepIndex >= 0)
		{
			PowerTreeStep *pstep = pSteps->ppSteps[iStepIndex--];

			if(pstep->pchEnhancement)
			{
				// Enhancement cost
				PowerDef *pdefEnh = powerdef_Find(pstep->pchEnhancement);
				PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
				if(pdefEnh && pdefNode)
				{
					// Find the PTNodeEnhancementDef
					int i;
					for(i=eaSize(&pdefNode->ppEnhancements)-1; i>=0; i--)
					{
						if(pdefEnh==GET_REF(pdefNode->ppEnhancements[i]->hPowerDef))
						{
							if(!stricmp(pdefNode->ppEnhancements[i]->pchCostTable,pchPoints))
							{
								iRespeced += pdefNode->ppEnhancements[i]->iCost;
							}
							break;
						}
					}
				}
			}
			else if(pstep->pchNode)
			{
				// Node cost
				PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
				if(pdefNode && !stricmp(pdefNode->ppRanks[pstep->iRank]->pchCostTable,pchPoints))
				{
					ANALYSIS_ASSUME(pdefNode != NULL);
					iRespeced += entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pdefNode, pstep->iRank);
				}
			}
			else
			{
				// Tree cost, currently always 0
			}
		}
	}

	return iRespeced;
}

AUTO_EXPR_FUNC(PTERespec);
int StepIsPermanent(ExprContext *pContext)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	PowerTreeSteps *pSteps = exprContextGetVarPointerUnsafe(pContext,"Steps");
	MultiVal *pMV = exprContextGetSimpleVar(pContext,"StepIndex");
	int iStepIndex = pMV ? MultiValGetInt(pMV, NULL) : -1;
	if(pEnt && pSteps && iStepIndex >= 0)
	{
		PowerTreeStep *pStep = pSteps->ppSteps[iStepIndex];
		if (pStep)
		{
			return pStep->bStepIsLocked;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(PTERespec);
bool CharacterPathContainsNode(ExprContext *pContext)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	PowerTreeSteps *pSteps = exprContextGetVarPointerUnsafe(pContext,"Steps");
	MultiVal *pMV = exprContextGetSimpleVar(pContext,"StepIndex");
	int iStepIndex = pMV ? MultiValGetInt(pMV, NULL) : -1;
	CharacterPath** eaPaths = NULL;

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &eaPaths);
	if(pEnt && pSteps && iStepIndex >= 0 && eaSize(&eaPaths) > 0)
	{
		int iPath = 0;
		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			PowerTreeStep *pStep = pSteps->ppSteps[iStepIndex];
			if (pStep->pchEnhancement == NULL	// Make sure it's not an enhancement
				&& pStep->iRank == 0)			// Make sure it's not a rank
			{
				int i, j, k;
				PTNodeDef *pPTNodeDef = powertreenodedef_Find(pStep->pchNode);
				for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++)
				{
					CharacterPathSuggestedPurchase *pSuggestedPurchase = eaPaths[iPath]->eaSuggestedPurchases[i];
					for (j = 0; j < eaSize(&pSuggestedPurchase->eaChoices); j++)
					{
						CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[j];
						for(k = 0; k < eaSize(&pChoice->eaSuggestedNodes); k++)
						{
							CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[k];
							PTNodeDef *pPathPTNodeDef = GET_REF(pSuggestedNode->hNodeDef);
							if (pPathPTNodeDef == pPTNodeDef)
								return true;
						}
					}
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(PTERespec);
bool CharacterPathNodeIsChoice(ExprContext *pContext)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	PowerTreeSteps *pSteps = exprContextGetVarPointerUnsafe(pContext,"Steps");
	MultiVal *pMV = exprContextGetSimpleVar(pContext,"StepIndex");
	int iStepIndex = pMV ? MultiValGetInt(pMV, NULL) : -1;
	CharacterPath** eaPaths = NULL;

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &eaPaths);
	if(pEnt && pSteps && iStepIndex >= 0 && eaSize(&eaPaths) > 0)
	{
		int iPath = 0;
		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			PowerTreeStep *pStep = pSteps->ppSteps[iStepIndex];
			if (pStep->pchEnhancement == NULL	// Make sure it's not an enhancement
				&& pStep->iRank == 0)			// Make sure it's not a rank
			{
				int i, j, k;
				PTNodeDef *pPTNodeDef = powertreenodedef_Find(pStep->pchNode);
				for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++)
				{
					CharacterPathSuggestedPurchase *pSuggestedPurchase = eaPaths[iPath]->eaSuggestedPurchases[i];
					for (j = 0; j < eaSize(&pSuggestedPurchase->eaChoices); j++)
					{
						CharacterPathChoice *pChoice = pSuggestedPurchase->eaChoices[j];
						for(k = 0; k < eaSize(&pChoice->eaSuggestedNodes); k++)
						{
							CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[k];
							PTNodeDef *pPathPTNodeDef = GET_REF(pSuggestedNode->hNodeDef);
							if (pPathPTNodeDef == pPTNodeDef)
								return eaSize(&pChoice->eaSuggestedNodes) > 1;
						}
					}
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(PTERespec);
bool NodeIsInPowerTree(ExprContext *pContext, char *pchTreeName)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	PowerTreeSteps *pSteps = exprContextGetVarPointerUnsafe(pContext,"Steps");
	MultiVal *pMV = exprContextGetSimpleVar(pContext,"StepIndex");
	int iStepIndex = pMV ? MultiValGetInt(pMV, NULL) : -1;
	if(pEnt && pSteps && iStepIndex >= 0)
	{
		PowerTreeStep *pStep = pSteps->ppSteps[iStepIndex];
		if (stricmp(pStep->pchTree, pchTreeName) == 0)
		{
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(PTERespec, reward, player, Mission);
bool PlayerHasCharacterPath(ExprContext *pContext)
{
	Entity *pEnt = exprContextGetSelfPtr(pContext);
	return entity_HasAnyCharacterPath(pEnt);
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GADAttribInt);
int exprEntGetGADAttribInt(ExprContext *pContext, const char* pchAttrib)
{
	GameAccountData *pData = exprContextGetVarPointerUnsafe(pContext, "GameAccount");
	return(gad_GetAttribInt(pData, pchAttrib));
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GADAttribString);
SA_RET_OP_STR const char* exprEntGetGADAttribString(ExprContext *pContext, const char* pchAttrib)
{
	GameAccountData *pData = exprContextGetVarPointerUnsafe(pContext, "GameAccount");
	return(gad_GetAttribString(pData, pchAttrib));
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GADIsLifetime);
S32 exprEntGetIsLifetime(ExprContext *pContext)
{
	GameAccountData *pData = exprContextGetVarPointerUnsafe(pContext, "GameAccount");
	return(pData && pData->bLifetimeSubscription);
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GADGetDaysSubscribbed);
U32 exprEntGetGetDaysSubscribed(ExprContext *pContext)
{
	U32 iDays = 0;
	GameAccountData *pData = exprContextGetVarPointerUnsafe(pContext, "GameAccount");
	if(pData)
	{
		iDays = pData->iDaysSubscribed;
	}
	
	return iDays;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GADIsPress);
S32 exprEntGetIsPress(ExprContext *pContext)
{
	GameAccountData *pData = exprContextGetVarPointerUnsafe(pContext, "GameAccount");
	return(pData && pData->bPress);
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetOwnerNumericItemValue);
int EntGetOwnerNumericItemValue(ExprContext *pContext, ACMD_EXPR_PARTITION iPartitionIdx, const char* pchItemName)
{
	Entity *pSrcEnt = GetSource(pContext);

	if(pSrcEnt)
	{
		if(pSrcEnt->pSaved && pSrcEnt->pSaved->conOwner.containerID)
		{
			Entity *pOwner = entFromContainerID(iPartitionIdx,pSrcEnt->pSaved->conOwner.containerType,pSrcEnt->pSaved->conOwner.containerID);

			if(pOwner)
				return inv_GetNumericItemValue(pOwner, pchItemName);
		}
		else
		{
			return inv_GetNumericItemValue(pSrcEnt, pchItemName);
		}
	}

	return 0;
}

//this expression already exists in the "Player" scope, however the Player pointer is not available in this context
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetNumericItemValue);
int EntGetNumericItemValue(ExprContext *pContext, const char* pchItemName)
{
	Entity *pSrcEnt = GetSource(pContext);

	if(pSrcEnt)
		return inv_GetNumericItemValue(pSrcEnt, pchItemName);

	return 0;
}

//this expression already exists in the "Player" scope, however the Player pointer is not available in this context
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetNumericItemValueScaled);
int EntGetNumericItemValueScaled(ExprContext *pContext, const char* pchItemName)
{
	Entity *pSrcEnt = GetSource(pContext);

	if(pSrcEnt)
		return inv_GetNumericItemValueScaled(pSrcEnt, pchItemName);

	return 0;
}


AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreesOwned);
int GetCharacterTrees(ExprContext* pContext)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;

	if(pSrc)
		return eaSize(&pSrc->ppPowerTrees);

	return 0;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(GetAttrib);
ExprFuncReturnVal exprPTEGetAttrib(ExprContext* pContext, const char* pchAttrib)
{
	Entity *pSrcEnt = GetSource(pContext);
	return exprEntGetAttrib(pSrcEnt, pchAttrib);
}


AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreeOwned);
int GetCharacterOwnedTree(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) const char *pchPowerTree)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	int i;

	if(!pSrc)
		return 0;

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
		if(!strcmp(pTreeDef->pchName,pchPowerTree))
		{
			return 1;
		}
	}

	return 0;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreeNotOwned);
int GetCharacterNotOwnedTree(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeDef) const char *pchPowerTree)
{
	return !GetCharacterOwnedTree(pContext, pchPowerTree);
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreesOfType);
int GetCharacterTreesOfType(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeTypeDef) const char *pchTreeType)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeTypeDef",pchTreeType);
	int i;
	int iReturn = 0;

	if(!pSrc)
		return 0;

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
		if(GET_REF(pTreeDef->hTreeType) == pType)
		{
			iReturn++;
		}
	}

	return iReturn;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(TreesNotOfType);
int GetCharacterTreesNotOfType(ExprContext *pContext, ACMD_EXPR_DICT(PowerTreeTypeDef) const char *pchTreeType)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	PTTypeDef *pType = RefSystem_ReferentFromString("PowerTreeType",pchTreeType);
	int i;
	int iReturn = 0;

	if(!pSrc)
		return 0;

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
		if(GET_REF(pTreeDef->hTreeType) != pType)
		{
			iReturn++;
		}
	}

	return iReturn;
}

int SortLinksByRefName(const PowerTreeLink **ppLink1, const PowerTreeLink **ppLink2)
{
	const char *pName1 = REF_STRING_FROM_HANDLE((*ppLink1)->hTree);
	const char *pName2 = REF_STRING_FROM_HANDLE((*ppLink2)->hTree);

	int iRetVal = stricmp(pName1, pName2);
	
	if (iRetVal)
	{
		return iRetVal;
	}
	
	return (int)(*ppLink1)->eType - (int)(*ppLink2)->eType;
}

static void PowerTreeAddDepRelationship(PowerTreeDef *pChild, PowerTreeDef *pParent)
{
	PowerTreeLink *pLink;
	S32 i;
	for (i = 0; i < eaSize(&pChild->ppLinks); i++)
		if (pChild->ppLinks[i]->eType == kPowerTreeRelationship_DependsOn && GET_REF(pChild->ppLinks[i]->hTree) == pParent)
			return;

	pLink = StructCreate(parse_PowerTreeLink);
	pLink->eType = kPowerTreeRelationship_DependsOn;
	SET_HANDLE_FROM_REFERENT("PowerTreeDef", pParent, pLink->hTree);
	eaPush(&pChild->ppLinks, pLink);

	eaQSort(pChild->ppLinks, SortLinksByRefName);

	pLink = StructCreate(parse_PowerTreeLink);
	pLink->eType = kPowerTreeRelationship_DependencyOf;
	SET_HANDLE_FROM_REFERENT("PowerTreeDef", pChild, pLink->hTree);
	eaPush(&pParent->ppLinks, pLink);

	eaQSort(pParent->ppLinks, SortLinksByRefName);
}

AUTO_EXPR_FUNC_STATIC_CHECK;
ExprFuncReturnVal GetCharacterNodesOfTypeCheck(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_DICT(PTNodeTypeDef) const char *pchNodeType, ACMD_EXPR_ERRSTRING errEstr)
{
	PTNodeTypeDef *pType = RefSystem_ReferentFromString("PTNodeTypeDef",pchNodeType);
	PowerTreeDef *pDef = exprContextGetUserPtr(pContext, parse_PowerTreeDef);
	PowerTreeDef *pOtherDef;
	RefDictIterator iter; 
	S32 j, k, l;

	if (piOut)
		*piOut = 0;
	if (!pType)
	{
		ErrorFilenamef(exprContextGetBlameFile(pContext), "No definition for power tree node type %s found", pchNodeType);
		return ExprFuncReturnError;
	}

	if (!pDef)
		return ExprFuncReturnFinished;

	RefSystem_InitRefDictIterator("PowerTreeDef", &iter);

	// When this expression is used in a power tree's requirement expression, we can
	// use it to figure out what trees actually satisfy the dependency, and set up
	// links to and from them.
	while (pOtherDef = RefSystem_GetNextReferentFromIterator(&iter))
	{
		if (pOtherDef == pDef)
			continue;
		for (j = 0; j < eaSize(&pOtherDef->ppGroups); j++)
		{
			for (k = 0; k < eaSize(&pOtherDef->ppGroups[j]->ppNodes); k++)
			{
				PTNodeDef *pNodeDef = pOtherDef->ppGroups[j]->ppNodes[k];
				PTNodeTypeDef *pCompType = pNodeDef ? GET_REF(pNodeDef->hNodeType) : NULL;
				if (pCompType == pType)
					PowerTreeAddDepRelationship(pDef, pOtherDef);
				else if (pCompType)
				{
					for(l = 0; l < eaSize(&pCompType->ppchSubTypes); l++)
					{
						if (RefSystem_ReferentFromString(g_hPowerTreeNodeTypeDict,pCompType->ppchSubTypes[l]) == pType)
							PowerTreeAddDepRelationship(pDef, pOtherDef);
					}
					//for(l = 0; l < eaSize(&pCompType->ppSubTypes); l++)
					//{
					//	if (pCompType->ppSubTypes[l] == pType)
					//		PowerTreeAddDepRelationship(pDef, pOtherDef);
					//}
				}
			}
		}
	}
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(NodesOfType) ACMD_EXPR_STATIC_CHECK(GetCharacterNodesOfTypeCheck);
ExprFuncReturnVal GetCharacterNodesOfType(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_DICT(PTNodeTypeDef) const char *pchNodeType, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	PTNodeTypeDef *pType = RefSystem_ReferentFromString("PTNodeTypeDef",pchNodeType);
	int i;
	int iReturn =0;

	if(!pSrc)
	{
		*piOut = 0;
		return ExprFuncReturnFinished;
	}

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pSrc->ppPowerTrees[i]->ppNodes);n++)
		{
			PTNodeDef *pNodeDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->hDef);

			if(pNodeDef && IS_HANDLE_ACTIVE(pNodeDef->hNodeType))
			{
				PTNodeTypeDef *pCompType = GET_REF(pNodeDef->hNodeType);
				if(pCompType == pType)
					iReturn++;
				else if(pCompType)
				{
					int t;

					for(t=0;t<eaSize(&pCompType->ppchSubTypes);t++)
					{
						if(RefSystem_ReferentFromString(g_hPowerTreeNodeTypeDict,pCompType->ppchSubTypes[t]) == pType)
							iReturn++;
					}
					//for(t=0;t<eaSize(&pCompType->ppSubTypes);t++)
					//{
					//	if(pCompType->ppSubTypes[t] == pType)
					//		iReturn++;
					//}
				}
			}
		}
	}

	*piOut = iReturn;
	return ExprFuncReturnFinished;

}

//TODO(BH): May need to be split into enhpoints and treepoints or something...
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(CostOfNodesOfType)  ACMD_EXPR_STATIC_CHECK(GetCharacterNodesOfTypeCheck);
ExprFuncReturnVal GetCharacterCostOfNodesOfType(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_DICT(PTNodeTypeDef) const char *pchNodeType, ACMD_EXPR_ERRSTRING errEstr)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	PTNodeTypeDef *pType = RefSystem_ReferentFromString("PTNodeTypeDef",pchNodeType);
	int i, j;
	int iReturn =0;

	if(!pSrc)
	{
		*piOut = 0;
		return ExprFuncReturnFinished;
	}

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pSrc->ppPowerTrees[i]->ppNodes);n++)
		{
			PTNodeDef *pNodeDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->hDef);

			if ( pSrc->ppPowerTrees[i]->ppNodes[n]->bEscrow )
				continue;

			if(pNodeDef && IS_HANDLE_ACTIVE(pNodeDef->hNodeType))
			{
				PTNodeTypeDef *pCompType = GET_REF(pNodeDef->hNodeType);
				
				if(!pCompType)
					continue;
				
				if(pCompType == pType)
				{
					for (j = pSrc->ppPowerTrees[i]->ppNodes[n]->iRank; j >= 0; --j)
					{
						int iCost = entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pSrc),pNodeDef,j);
						if(iCost>=0)
						{
							iReturn += iCost;
						}
					}
				}
				else
				{
					int t;

//					for(t=0;t<eaSize(&pCompType->ppSubTypes);t++)
//					{
//						if(pCompType->ppSubTypes[t] == pType)
//						{
					for(t=0;t<eaSize(&pCompType->ppchSubTypes);t++)
					{
						if(RefSystem_ReferentFromString(g_hPowerTreeNodeTypeDict,pCompType->ppchSubTypes[t]) == pType)
						{
							for (j = pSrc->ppPowerTrees[i]->ppNodes[n]->iRank; j >= 0; --j)
							{
								int iCost = entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pSrc),pNodeDef,j);
								if(iCost>=0)
								{
									iReturn += iCost;
								}
							}
						}
					}
				}
			}
		}
	}

	*piOut = iReturn;
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(NodesOfPurpose);
ExprFuncReturnVal GetCharacterNodesOfPurpose(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_ENUM(PowerPurpose) const char *pchPurposeName)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	int i, j;
	int iReturn =0;

	int ePurpose = StaticDefineIntGetInt(PowerPurposeEnum,pchPurposeName);

	if(!pSrc)
	{
		*piOut = 0;
		return ExprFuncReturnFinished;
	}

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pSrc->ppPowerTrees[i]->ppNodes);n++)
		{
			PTNodeDef *pNodeDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->hDef);

			if(pNodeDef && IS_HANDLE_ACTIVE(pNodeDef->hNodeType))
			{
				for(j = eaSize(&pSrc->ppPowerTrees[i]->ppNodes[n]->ppPowers)-1; j >= 0; j--)
				{
					PowerDef *ppowerDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->ppPowers[j]->hDef);
					if(ppowerDef && ePurpose == ppowerDef->ePurpose)
						iReturn++;
				}
			}
		}
	}

	*piOut = iReturn;
	return ExprFuncReturnFinished;
}

//TODO(BH): May need to be split into enhpoints and treepoints or something...
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(CostOfNodesOfPurpose);
ExprFuncReturnVal GetCharacterCostOfNodesOfPurpose(ExprContext *pContext, ACMD_EXPR_INT_OUT piOut, ACMD_EXPR_ENUM(PowerPurpose) const char *pchPurposeName)
{
	Entity *pSrcEnt = GetSource(pContext);
	Character *pSrc = pSrcEnt ? pSrcEnt->pChar : NULL;
	int i, j, k;
	int iReturn =0;

	int ePurpose = StaticDefineIntGetInt(PowerPurposeEnum,pchPurposeName);

	if(!pSrc)
	{
		*piOut = 0;
		return ExprFuncReturnFinished;
	}

	for(i=0;i<eaSize(&pSrc->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pSrc->ppPowerTrees[i]->ppNodes);n++)
		{
			PTNodeDef *pNodeDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->hDef);

			if ( pSrc->ppPowerTrees[i]->ppNodes[n]->bEscrow )
				continue;

			if(pNodeDef && IS_HANDLE_ACTIVE(pNodeDef->hNodeType))
			{
				for(j = eaSize(&pSrc->ppPowerTrees[i]->ppNodes[n]->ppPowers)-1; j >= 0; j--)
				{
					PowerDef *ppowerDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[n]->ppPowers[j]->hDef);
					if(ppowerDef && ePurpose == ppowerDef->ePurpose)
					{
						for (k = pSrc->ppPowerTrees[i]->ppNodes[n]->iRank; k >= 0; --k)
						{
							int iCost = entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pSrc),pNodeDef,k);
							if(iCost>=0)
							{
								iReturn += iCost;
							}
						}
					}
				}
			}
		}
	}

	*piOut = iReturn;
	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(HasPuppetWithSpecies);
bool HasPuppetWithSpecies(ExprContext* context, ACMD_EXPR_RES_DICT(SpeciesDef) const char* pchSpecies)
{
	Entity* e = GetSource(context);
	SpeciesDef *pSpeciesDef;
	SpeciesDef *pSpeciesDefArg = RefSystem_ReferentFromString("SpeciesDef", pchSpecies);

	if(e && e->pChar) {
		pSpeciesDef = GET_REF(e->pChar->hSpecies);
		if(pSpeciesDef && pSpeciesDefArg && pSpeciesDef == pSpeciesDefArg) {
			return true;
		}

		// Because power trees live only on the main entity, and not the puppets
		// we must check to see if this player has the ability to match the species
		// input
		if(e->pSaved && e->pSaved->pPuppetMaster)
		{
			int i;

			for(i=0;i<eaSize(&e->pSaved->pPuppetMaster->ppPuppets);i++)
			{
				// Only look at ACTIVE puppets. Inactive puppets do not count towards the species check.
				// if you expect this to work differently, talk to Michael McCarry
				if(e->pSaved->pPuppetMaster->ppPuppets[i]->eState == PUPPETSTATE_ACTIVE
					&& e->pSaved->pPuppetMaster->curID != e->pSaved->pPuppetMaster->ppPuppets[i]->curID)
				{
					// THIS IS BAD - Since this is called from inside transactions (which itself is BAD),
					//  its success requires the ObjectDB to have filled in the relevant subscriptions,
					//  which is NOT guaranteed.
					//Entity *pPuppet = GET_REF(e->pSaved->pPuppetMaster->ppPuppets[i]->hEntity);
					char idBuf[128];
					Entity *pPuppet = RefSystem_ReferentFromString(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYSAVEDPET),ContainerIDToString(e->pSaved->pPuppetMaster->ppPuppets[i]->curID, idBuf));
					pSpeciesDef = pPuppet && pPuppet->pChar ? GET_REF(pPuppet->pChar->hSpecies) : NULL;

					if(pSpeciesDef && pSpeciesDefArg && pSpeciesDef == pSpeciesDefArg)
						return true;
				}
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(PlayerIsSpecies);
bool CharacterIsSpecies(ExprContext* context, ACMD_EXPR_RES_DICT(SpeciesDef) const char* pchSpecies)
{
	Entity* e = GetSource(context);
	SpeciesDef *pSpeciesDef;
	SpeciesDef *pSpeciesDefArg = RefSystem_ReferentFromString("SpeciesDef", pchSpecies);

	if(e && e->pChar) {
		pSpeciesDef = GET_REF(e->pChar->hSpecies);
		if(pSpeciesDef && pSpeciesDefArg && pSpeciesDef == pSpeciesDefArg) {
			return true;
		}
	}

	return false;
}

// Returns whether an entity has completed a mission.  Mission name is not validated statically.
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntIsMissionCompleted);
S32 exprEntIsMissionCompleted(ExprContext* context, const char *pchMission)
{
	Entity* e = GetSource(context);
	if (e && e->pPlayer)
	{
		Mission *pMission = mission_FindMissionFromRefString(e->pPlayer->missionInfo, pchMission);
		S32 iCount = mission_GetNumTimesCompletedByName(e->pPlayer->missionInfo, pchMission);
		return iCount + ((pMission && pMission->state == MissionState_Succeeded) ? 1 : 0);
	}
	return 0;
}

// Returns the number of times an entity has completed a mission.
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetNumTimesMissionCompleted);
S32 exprEntGetNumTimesMissionCompleted(ExprContext* context, const char *pchMission)
{
	Entity* e = GetSource(context);
	if (e && e->pPlayer)
	{
		return mission_GetNumTimesCompletedByName(e->pPlayer->missionInfo, pchMission);
	}
	return 0;
}


AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetGuildStat);
S32 exprGetEntGuildStat(ExprContext* context, const char *pchStatName)
{
	// Get the entity
	Entity *pEnt = GetSource(context);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild && pGuild->pGuildStatsInfo)
	{
		GuildStat *pGuildStat = NULL;
		S32 iGuildStatIndex = eaIndexedFindUsingString(&pGuild->pGuildStatsInfo->eaGuildStats, pchStatName);
		if (iGuildStatIndex >= 0)
		{
			return pGuild->pGuildStatsInfo->eaGuildStats[iGuildStatIndex]->iValue;
		}
		else
		{
			// Try to get the default for this specific stat
			GuildStatDef *pGuildStatDef = RefSystem_ReferentFromString(g_GuildStatDictionary, pchStatName);

			return pGuildStatDef ? pGuildStatDef->iInitialValue : 0;
		}
	}
	return 0;
}

// Returns the number of members from the given class in the player's guild
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGuildMemberClassCount);
S32 exprGetEntGuildMemberClassCount(ExprContext* context, const char *pchClassName)
{
	S32 iMemberCount = 0;

	// Get the entity
	Entity *pEnt = GetSource(context);
	Guild *pGuild = guild_GetGuild(pEnt);

	if (pEnt && pGuild && eaSize(&pGuild->eaMembers) > 0)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pGuild->eaMembers, GuildMember, pGuildMember)
		{
			if (pGuildMember && stricmp(pGuildMember->pchClassName, pchClassName) == 0)
			{
				++iMemberCount;
			}
		}
		FOR_EACH_END
	}

	return iMemberCount;
}

// Indicates if the guild is assigned the same theme as the given theme
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGuildIsTheme);
bool exprGetEntGuildIsTheme(ExprContext* context, const char *pchThemeName)
{
	// Get the entity
	Entity *pEnt = GetSource(context);
	Guild *pGuild = guild_GetGuild(pEnt);

	return pchThemeName && pchThemeName[0] && pGuild && IS_HANDLE_ACTIVE(pGuild->hTheme) && stricmp(REF_STRING_FROM_HANDLE(pGuild->hTheme), pchThemeName) == 0;
}

// Returns true/false if a character's account has a given permission token
AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(HasPermissionToken);
bool exprHasPermissionToken(ExprContext* context, ACMD_EXPR_ENUM(GameTokenType) const char *pchType, const char *pchKey, const char *pchValue)
{
	GameAccountData *pData = exprContextGetVarPointerUnsafe(context, "GameAccount");
	GameTokenType eType = StaticDefineIntGetInt(GameTokenTypeEnum, pchType);
	if(pData && eType >= 0)
	{
		return GamePermission_HasTokenKeyType(pData, eType, pchKey, pchValue);
	}

	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetRespecNumericCostInternal);
S32 PowerTree_GetRespecNumericCostInternal(Entity *pEntity, const char *pchRespecGroupTypeName)
{
	int eGroupType = StaticDefineIntGetInt(PTRespecGroupTypeEnum, pchRespecGroupTypeName);

	if(pEntity)
	{
		return PowerTree_GetNumericRespecCost(CONTAINER_NOCONST(Entity, pEntity), eGroupType);
	}

	return 0;
}

// Does this group have free respecs in the tree
// Must be able to do respecs on this group
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(HasFreeRespecInGroup);
bool PowerTree_HasFreeRespecInGroup(Entity *pEntity, const char *pchRespecGroupTypeName)
{
	int eGroupType = StaticDefineIntGetInt(PTRespecGroupTypeEnum, pchRespecGroupTypeName);
	bool bAnswer = false;

	if(pEntity && pEntity->pChar && PowerTree_CanRespecGroupWithNumeric(eGroupType))
	{
		S32 i;
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		// Get everything
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pEntity->pChar),pSteps, false, eGroupType);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(pEntity),pEntity->pChar,pSteps,0);

		// Check for no cost step
		for(i = 0 ; i < eaSize(&pSteps->ppSteps); ++i)
		{
			if(pSteps->ppSteps[i]->pchNode)
			{
				if(pSteps->ppSteps[i]->iCostRespec == 0)
				{
					bAnswer = true;
					break;
				}
			}
		}

		StructDestroy(parse_PowerTreeSteps,pSteps);

	}

	return bAnswer;
}

AUTO_EXPR_FUNC(PTECharacter) ACMD_NAME(EntGetRespecGroupNumericCost);
int EntGetRespecGroupNumericCost(ExprContext *pContext, ACMD_EXPR_ENUM(PTRespecGroupType) const char *pchRespecGroupTypeName)
{
	Entity *pEntity = GetSource(pContext);

	return PowerTree_GetRespecNumericCostInternal(pEntity, pchRespecGroupTypeName);
	
}
