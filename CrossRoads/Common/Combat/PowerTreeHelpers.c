#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "AutoGen/EntitySavedData_h_ast.h"
#include "EString.h"
#include "Expression.h"
#include "file.h"
#include "Stringcache.h"
#include "Player.h"

#include "Character.h"
#include "Powers.h"
#include "AutoGen/Powers_h_ast.h"
#include "PowerTree.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/Character_h_ast.h"

#include "PowerHelpers.h"
#include "PowerTreeEval.h"
#include "PowerTreeHelpers.h"

#include "AutoTransDefs.h"
#include "RewardCommon.h"
#include "itemCommon.h"
#include "inventoryCommon.h"
#include "CharacterClass.h"
#include "PowerVars.h"
#include "GameAccountDataCommon.h"
#include "mission_common.h"

#include "resourceInfo.h"

#include "Entity_h_ast.h"
#include "Player_h_ast.h"
#include "chatCommon.h"
#include "GameAccountData_h_ast.h"
#include "mission_common_h_ast.h"

#include "AutoGen/PowerTreeHelpers_h_ast.h"

#ifdef GAMESERVER
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "GameAccountData\GameAccountData.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


/***************************************************************************

Please note that the vast majority of these functions should no longer be
AUTO_TRANS_HELPERs.  However, stripping that cleanly is a lot of drudgery,
so it hasn't been done.

***************************************************************************/


// Static flag to disable most rules for purchasing Powers in PowerTrees
//  Since this effects ALL the relevant helpers in the process, it's NOT
//  global, and setting it on a live server generates an error instead.
static S32 s_bPowerTreesDisablePurchaseRules = false;

// Disables most rules for purchasing Powers in PowerTrees
AUTO_COMMAND;
void PowerTrees_DisablePurchaseRules(int bDisable)
{
	if(IsServer() && isProductionMode())
	{
		Errorf("Not allowed to change PowerTree purchase rules in production mode on the server");
		return;
	}
	s_bPowerTreesDisablePurchaseRules = bDisable;
}


AUTO_TRANS_HELPER;
bool RankIsEnhancementHelper(ATH_ARG SA_PARAM_NN_VALID PTNodeRankDef *pRankDef)
{
	//The default table for enhancements is EnhPoints
	if(!stricmp(pRankDef->pchCostTable, "EnhPoints"))
	{
		return true;
	}
	return false;
}

// Returns the PowerTree owned by the Entity with the given def, if it exists
AUTO_TRANS_HELPER;
NOCONST(PowerTree) *entity_FindPowerTreeHelper(ATH_ARG NOCONST(Entity) *pEnt, PowerTreeDef *pTreeDef)
{
	int i;
	
	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || !pTreeDef)
		return NULL;
	
	for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1; i>=0; i--)
	{
		if(GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef) == pTreeDef)
			return pEnt->pChar->ppPowerTrees[i];
	}

	return NULL;
}

// Returns the PTNode in the PowerTree with the given def, if it exists.  Matches references
AUTO_TRANS_HELPER;
NOCONST(PTNode) *powertree_FindNodeHelper(ATH_ARG NOCONST(PowerTree) *pTree, PTNodeDef *pNodeDef)
{
	int i;
	PTNodeDef *pNodeDefRef;

	if(ISNULL(pTree) || !pNodeDef)
		return NULL;

	// Switch to the reference if there is one.  This does NOT do an IS_HANDLE_ACTIVE test because
	//  that involves a function call, which does a stash lookup, etc, etc
	pNodeDefRef = GET_REF(pNodeDef->hNodeClone);
	if(pNodeDefRef)
	{
		pNodeDef = pNodeDefRef;
	}

	for(i=eaSize(&pTree->ppNodes)-1; i>=0; i--)
	{
		PTNodeDef *pNodeDefOwned = GET_REF(pTree->ppNodes[i]->hDef);
		
		// Match if the owned node is an exact match, or its reference is an exact match
		if(pNodeDefOwned==pNodeDef
			|| (pNodeDefOwned 
				&& GET_REF(pNodeDefOwned->hNodeClone) == pNodeDef))
		{
			return pTree->ppNodes[i];
		}
	}

	return NULL;
}

// Returns the PTNode owned by the Entity with the given def, if it exists
AUTO_TRANS_HELPER;
NOCONST(PTNode) *entity_FindPowerTreeNodeHelper(ATH_ARG NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef)
{
	int i;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || !pNodeDef)
		return NULL;
	
	for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1; i>=0; i--)
	{
		// This could be implemented directly, instead of calling the helper on each tree, but the
		//  helper does all the reference work, which is just complicated enough that I don't want
		//  to copy/paste it.
		NOCONST(PTNode) *pNode = powertree_FindNodeHelper(pEnt->pChar->ppPowerTrees[i],pNodeDef);
		if(pNode)
		{
			return pNode;
		}
	}

	return NULL;
}

// Returns the Enhancement Power in the PTNode with the given def, if it exists
AUTO_TRANS_HELPER;
NOCONST(Power) *powertreenode_FindEnhancementHelper(ATH_ARG NOCONST(PTNode) *pNode, PowerDef *pEnhDef)
{
	int i;

	if(ISNULL(pNode) || !pEnhDef)
		return NULL;

	for(i=eaSize(&pNode->ppEnhancements)-1; i>=0; i--)
	{
		if(GET_REF(pNode->ppEnhancements[i]->hDef) == pEnhDef)
			return pNode->ppEnhancements[i];
	}

	return NULL;
}

// Returns the EnhancementTracker in the PTNode with the given def, if it exists
AUTO_TRANS_HELPER;
NOCONST(PTNodeEnhancementTracker) *powertreenode_FindEnhancementTrackerHelper(ATH_ARG NOCONST(PTNode) *pNode, PowerDef *pEnhDef)
{
	int i;

	if(ISNULL(pNode) || !pEnhDef)
		return NULL;

	for(i=eaSize(&pNode->ppEnhancementTrackers)-1; i>=0; i--)
	{
		if(GET_REF(pNode->ppEnhancementTrackers[i]->hDef) == pEnhDef)
			return pNode->ppEnhancementTrackers[i];
	}

	return NULL;
}

// Returns the rank of the Enhancement in the PTNode with the given def, if it exists, otherwise returns 0
AUTO_TRANS_HELPER;
int powertreenode_FindEnhancementRankHelper(ATH_ARG NOCONST(PTNode) *pNode, PowerDef *pEnhDef)
{
	int r = 0;
	NOCONST(PTNodeEnhancementTracker) *ptracker = powertreenode_FindEnhancementTrackerHelper(pNode,pEnhDef);
	if(ptracker)
	{
		r = eaSize(&ptracker->ppPurchases);
	}
	return r;
}

// Returns the fake entity, if valid. Otherwise returns pEnt. If bCreateFakeEnt is true, create the fake entity if it's NULL.
static NOCONST(Entity)* entity_GetEntityToModify(NOCONST(Entity)* pEnt, NOCONST(Entity)** ppFakeEnt, bool bCreateFakeEnt)
{
	if (ppFakeEnt)
	{
		if (bCreateFakeEnt && !(*ppFakeEnt))
		{
			(*ppFakeEnt) = StructCloneWithCommentNoConst(parse_Entity, pEnt, __FUNCTION__);
		}
		if (*ppFakeEnt)
		{
			return *ppFakeEnt;
		}
	}
	return pEnt;
}

//this now returns 0 or 1 to correspond to which entity is correct rather than returning the entity
AUTO_TRANS_HELPER
ATR_LOCKS(pBuyer, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppuppetmaster.Curid");
int entity_trh_UseBuyerLevel(ATH_ARG NOCONST(Entity)* pBuyer, ContainerID iPetContID)
{
	if(NONNULL(pBuyer) 
		&& NONNULL(pBuyer->pSaved) 
		&& NONNULL(pBuyer->pSaved->pPuppetMaster)
		&& pBuyer->pSaved->pPuppetMaster->curID != iPetContID)
	{
		bool bFound = false;
		int i;
		for ( i = 0; i < eaSize(&pBuyer->pSaved->ppOwnedContainers); i++ )
		{
			if ( pBuyer->pSaved->ppOwnedContainers[i]->conID == iPetContID )
			{
				bFound = true;
				break;
			}
		}
		if ( bFound )
		{
			for(i=0;i<eaSize(&pBuyer->pSaved->pPuppetMaster->ppPuppets);i++)
			{
				if( pBuyer->pSaved->pPuppetMaster->ppPuppets[i]->curID == iPetContID )
				{
					//If the buyer is not the current puppet, return true
					return true;
				}
			}
		}
	}
	return false;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pBuyer, ".Psaved.Ppownedcontainers, .Psaved.Ppuppetmaster.Pppuppets, .Psaved.Ppuppetmaster.Curid, .Pchar.Ilevelexp")
ATR_LOCKS(pEnt, ".Pchar.Ilevelexp");
S32 entity_trh_GetExpLevelOfCorrectBuyer(ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (pEnt->myEntityType == GLOBALTYPE_ENTITYSAVEDPET
		&& entity_trh_UseBuyerLevel(pBuyer, pEnt->myContainerID))
	{
		return entity_trh_GetSavedExpLevel(pBuyer);
	}
	else
		return entity_trh_GetSavedExpLevel(pEnt);
}

AUTO_TRANS_HELPER;
int entity_PointsEarned(ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints)
{
	int iPoints = 0;

	if(pchPoints)
	{
		if(powertable_Find(pchPoints))
		{
			int iLevel = entity_trh_GetSavedExpLevel(pEnt);
			iPoints = (int)entity_PowerTableLookupAtHelper(pEnt,pchPoints,iLevel - 1);
		}
		else
		{
			iPoints = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pEnt,pchPoints);
		}
	}

	return iPoints;
}

AUTO_TRANS_HELPER;
int entity_PointsSpentInTreeUnderLevel(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, const char *pchPoints, int iLevelCap)
{
	int n,e,d;
	int iPointsSpent = 0;

	PERFINFO_AUTO_START_FUNC();

	n = !pTree ? -1 : eaSize(&pTree->ppNodes)-1;
	for(; n>=0; n--)
	{
		PTNodeDef *pNodeDef = GET_REF(pTree->ppNodes[n]->hDef);

		if(!pNodeDef || pTree->ppNodes[n]->bEscrow)
			continue;

		for(e=eaSize(&pTree->ppNodes[n]->ppEnhancements)-1; e>=0; e--)
		{
			for(d=eaSize(&pNodeDef->ppEnhancements)-1; d>=0; d--)
			{
				
				if(GET_REF(pNodeDef->ppEnhancements[d]->hPowerDef) == GET_REF(pTree->ppNodes[n]->ppEnhancements[e]->hDef)
					&& stricmp(pNodeDef->ppEnhancements[d]->pchCostTable, pchPoints)==0)
				{
					int iLevel = eaSize(&pTree->ppNodes[n]->ppEnhancementTrackers[e]->ppPurchases);
					iPointsSpent += iLevel * pNodeDef->ppEnhancements[d]->iCost;
				}
			}
		}

		if(pNodeDef->bHasCosts)
		{
			if(pNodeDef->bRankCostTablesVary)
			{
				for(d=pTree->ppNodes[n]->iRank; d>=0; d--)
				{
					if(iLevelCap > -1 && pNodeDef->ppRanks[d]->pRequires->iTableLevel > iLevelCap)
						continue;

					if(0==stricmp(pNodeDef->ppRanks[d]->pchCostTable, pchPoints))
					{
						int iCost;
						if(NONNULL(pEnt) && NONNULL(pEnt->pChar))
						{
							iCost = entity_PowerTreeNodeRankCostHelper(pEnt->pChar,pNodeDef,d);
						}
						else
						{
							iCost = entity_PowerTreeNodeRankCostHelper(NULL,pNodeDef,d);
						}
						
						if(iCost>0)
						{
							iPointsSpent += iCost;
						}
					}
				}
			}
			else
			{
				PTNodeRankDef *pRankDef = pNodeDef->ppRanks[0];

				if(iLevelCap > -1 && pRankDef->pRequires->iTableLevel > iLevelCap)
					continue;

				if(pRankDef && 0==stricmp(pRankDef->pchCostTable, pchPoints))
				{
					int iCost;
					if(NONNULL(pEnt) && NONNULL(pEnt->pChar))
					{
						iCost = entity_PowerTreeNodeRanksCostHelper(pEnt->pChar,pNodeDef,pTree->ppNodes[n]->iRank);
					}
					else
					{
						iCost = entity_PowerTreeNodeRanksCostHelper(NULL,pNodeDef,pTree->ppNodes[n]->iRank);
					}

					if(iCost>0)
					{
						iPointsSpent += iCost;
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return iPointsSpent;
}

AUTO_TRANS_HELPER;
int entity_PointsSpentInTree(ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, const char *pchPoints)
{
	return entity_PointsSpentInTreeUnderLevel(pEnt,pTree,pchPoints,-1);
}

AUTO_TRANS_HELPER;
int entity_PointsSpent(ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints)
{
	int i, iPointsSpent = 0;
	PERFINFO_AUTO_START_FUNC();
	if(!ISNULL(pEnt) && !ISNULL(pEnt->pChar))
	{
		for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1; i>=0; i--)
		{
			iPointsSpent += entity_PointsSpentInTree(pEnt, pEnt->pChar->ppPowerTrees[i], pchPoints);
		}
	}
	PERFINFO_AUTO_STOP();
	return iPointsSpent;
}

AUTO_TRANS_HELPER;
int entity_PointsSpentTryNumeric(ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, const char *pchPoints)
{	
	int i, j, iPointsSpent = 0;
	char** eaSpentNumerics = NULL;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar))
	{
		return 0;
	}
	
	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		PowerTreeDef* pTreeDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);
		PTTypeDef* pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;

		if ( pTreeTypeDef && pTreeTypeDef->bSpentPointsNonDynamic && pTreeTypeDef->pchSpentPointsNumeric && pTreeTypeDef->pchSpentPointsNumeric[0] )
		{
			S32 iValue, iCount = eaSize( &eaSpentNumerics );
			char* pchNumeric = pTreeTypeDef->pchSpentPointsNumeric;

			for ( j = 0; j < iCount; j++ )
			{
				if ( stricmp( eaSpentNumerics[j], pchNumeric ) == 0 )
				{
					break;
				}
			}

			if ( j < iCount )
				continue;

			if ( NONNULL(pBuyer) )
				iValue = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pBuyer,pchNumeric);
			else
				iValue = inv_trh_GetNumericValue(ATR_EMPTY_ARGS,pEnt,pchNumeric);

			if ( iValue > 0 )
			{
				eaPush( &eaSpentNumerics, pchNumeric );
				iPointsSpent += iValue;
				continue;
			}
		}

		iPointsSpent += entity_PointsSpentInTree( pEnt, pEnt->pChar->ppPowerTrees[i], pchPoints );
	}

	eaDestroy(&eaSpentNumerics);

	return iPointsSpent;
}

// Returns the maximum number of a points the player can invest in the tree. A value of zero indicates that there is no max. 
int entity_GetMaxSpendablePointsInTree(Entity* pEnt, PowerTreeDef* pTreeDef, const char* pchPoints)
{
	if (pEnt && 
		pTreeDef && 
		pTreeDef->pchMaxSpendablePointsCostTable && pTreeDef->pchMaxSpendablePointsCostTable[0] && 
		stricmp(pTreeDef->pchMaxSpendablePointsCostTable, pchPoints)==0)
	{
		int iEarnedPoints = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchPoints);
		int iMaxPoints = (int)floorf(pTreeDef->fMaxSpendablePoints * iEarnedPoints);
		if (pTreeDef->iMinCost)
		{
			F32 fDiv = iMaxPoints / (F32)pTreeDef->iMinCost;
			iMaxPoints = ceilf(fDiv) * pTreeDef->iMinCost;
		}
		return iMaxPoints;
	}
	return 0;
}

AUTO_TRANS_HELPER;
int entity_PointsRemaining(ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, PowerTreeDef *pTreeDef, const char *pchPoints)
{
	int iRemaining = 0;
	PERFINFO_AUTO_START_FUNC();
	if(NONNULL(pBuyer))
	{
		int iMaxSpendablePoints = entity_GetMaxSpendablePointsInTree(CONTAINER_RECONST(Entity, pBuyer), pTreeDef, pchPoints);
		if (iMaxSpendablePoints)
		{
			PowerTree* pTree = character_FindTreeByDefName(CONTAINER_RECONST(Character, pEnt->pChar), pTreeDef->pchName);
			if (pTree)
			{
				iRemaining = iMaxSpendablePoints - entity_PointsSpentInTree(pBuyer, CONTAINER_NOCONST(PowerTree, pTree), pchPoints);
			}
			MIN1(iRemaining, entity_PointsEarned(pBuyer, pchPoints) - entity_PointsSpentTryNumeric(pBuyer, pEnt, pchPoints));
		}
		else
		{
			iRemaining = entity_PointsEarned(pBuyer, pchPoints) - entity_PointsSpentTryNumeric(pBuyer, pEnt, pchPoints);
		}
	}
	else
	{
		int iMaxSpendablePoints = entity_GetMaxSpendablePointsInTree(CONTAINER_RECONST(Entity, pEnt), pTreeDef, pchPoints);
		if (iMaxSpendablePoints)
		{
			PowerTree* pTree = character_FindTreeByDefName(CONTAINER_RECONST(Character, pEnt->pChar), pTreeDef->pchName);
			if (NONNULL(pEnt->pChar))
			{
				iRemaining = iMaxSpendablePoints - entity_PointsSpentInTree(pEnt, CONTAINER_NOCONST(PowerTree, pTree), pchPoints);
			}
			MIN1(iRemaining, entity_PointsEarned(pEnt, pchPoints) - entity_PointsSpentTryNumeric(pBuyer, pEnt, pchPoints));
		}
		else
		{
			iRemaining = entity_PointsEarned(pEnt, pchPoints) - entity_PointsSpentTryNumeric(pBuyer, pEnt, pchPoints);
		}
	}
	PERFINFO_AUTO_STOP();
	return iRemaining;
}

AUTO_TRANS_HELPER;
S32 EntityPTPurchaseReqsHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree ) *pTree, PTPurchaseRequirements *pRequirements)
{
	if(ISNULL(pEnt) || ISNULL(pEnt->pChar))
	{
		return false;
	}

	// Ignore requirements if purchase rules are disabled
	if(s_bPowerTreesDisablePurchaseRules)
		return true;

	PERFINFO_AUTO_START_FUNC();

	if( pRequirements->pExprPurchase )
	{
		//Setup then eval the expression
		MultiVal mVal;
		ExprContext *pContext = ptpurchaserequirements_GetContext();

		PERFINFO_AUTO_START("Expression",1);

		exprContextSetPointerVar(pContext,"Source",(Entity*)pEnt,parse_Entity, true, true);
		exprContextSetPointerVar(pContext,"GameAccount", entity_trh_GetGameAccount(pEnt), parse_GameAccountData, false, true);
		exprContextSetSelfPtr(pContext, (Entity*)pEnt);
		exprContextSetPartition(pContext, PARTITION_IN_TRANSACTION);
		exprEvaluateTolerateInvalidUsage(pRequirements->pExprPurchase,pContext,&mVal);

		PERFINFO_AUTO_STOP();
		
		if(MultiValGetInt(&mVal,NULL) <= 0)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if(pRequirements->iMinPointsSpentInThisTree || pRequirements->iMaxPointsSpentInThisTree)
	{
		int iPointsSpent = entity_PointsSpentInTree(pEnt, pTree, pRequirements->pchTableName);
		if(pRequirements->iMinPointsSpentInThisTree)
		{
			if (iPointsSpent < pRequirements->iMinPointsSpentInThisTree)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		if(pRequirements->iMaxPointsSpentInThisTree)
		{
			if (iPointsSpent >= pRequirements->iMaxPointsSpentInThisTree)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	if(pRequirements->iMinPointsSpentInAnyTree || pRequirements->iMaxPointsSpentInAnyTree)
	{
		int iPointsSpent = entity_PointsSpent(pEnt, pRequirements->pchTableName);
		if(pRequirements->iMinPointsSpentInAnyTree)
		{
			if (iPointsSpent < pRequirements->iMinPointsSpentInAnyTree)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		if(pRequirements->iMaxPointsSpentInAnyTree)
		{
			if (iPointsSpent >= pRequirements->iMaxPointsSpentInAnyTree)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}

	if(pRequirements->iTableLevel)
	{
		int iExpLevel;
		PERFINFO_AUTO_START("iTableLevel",1);

		PERFINFO_AUTO_START("entity_trh_GetExpLevelOfCorrectBuyer",1);
		iExpLevel = entity_trh_GetExpLevelOfCorrectBuyer(pBuyer,pEnt);
		PERFINFO_AUTO_STOP();

		if(iExpLevel < pRequirements->iTableLevel
			|| (pRequirements->pchTableName
				&& (entity_Find_TableTrainingLevel(pBuyer, pEnt, pRequirements->pchTableName)+1 < pRequirements->iTableLevel)))
		{
			PERFINFO_AUTO_STOP();
			PERFINFO_AUTO_STOP();
			return false;
		}
		PERFINFO_AUTO_STOP();
	}

	if(IS_HANDLE_ACTIVE(pRequirements->hGroup))
	{
		int i,count;
		PTGroupDef *pGroupDef = GET_REF(pRequirements->hGroup);

		if(!pEnt->pChar->ppPowerTrees || !pGroupDef)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		count = 0;
		for(i=eaSize(&pGroupDef->ppNodes)-1;i>=0;i--)
		{
			PTNode *pNode = (PTNode*)character_FindPowerTreeNodeHelper(pEnt->pChar,NULL,pGroupDef->ppNodes[i]->pchNameFull);

			if(pNode)
			{
				if ( pNode->bEscrow )
					continue;

				count += pNode->iRank + 1;
			}
		}

		DBGPOWERTREE_printf("Total count for group %s: %d",pGroupDef->pchGroup,count);
		if(count < pRequirements->iGroupRequired)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

	}
	PERFINFO_AUTO_STOP();
	return true;
}

AUTO_TRANS_HELPER;
S32 entity_IsTrainingNode(ATH_ARG NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef)
{
	// If training this node, don't allow it to be purchased or ranked up
	if ( eaSize(&pEnt->pChar->ppTraining)>0 )
	{
		int i;
		for ( i = eaSize(&pEnt->pChar->ppTraining)-1; i >= 0; i-- )
		{
			switch ( pEnt->pChar->ppTraining[i]->eType )
			{
				case CharacterTrainingType_Replace:
				case CharacterTrainingType_ReplaceEscrow:	
				{
					if ( pNodeDef == GET_REF(pEnt->pChar->ppTraining[i]->hOldNodeDef) )
					{
						return true;
					}
				}
			}
		}
	}
	return false;
}

AUTO_TRANS_HELPER;
S32 entity_CanBuyPowerTreeNodeIgnorePointsRankHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank)
{
	S32 bCanBuy = true;

	// Early exit, makes analysis happy
	if(!pNodeDef)
		return false;

	if ( entity_IsTrainingNode(pEnt, pNodeDef) )
	{
		DBGPOWERTREE_printf("Power Node (%s) is currently being trained!\n", pNodeDef->pchNameFull);
		return false;
	}

	// Quick check for the def and desired rank actually existing
	if(iRank >= eaSize(&pNodeDef->ppRanks))
		bCanBuy = false;

	ANALYSIS_ASSUME(pNodeDef->ppRanks);	// More making analysis happy, this would be caught during validation

	// Check requirements on the group (assuming we're not just buying a new rank of a node we already own)
	if(bCanBuy && iRank < 1)
		bCanBuy = EntityPTPurchaseReqsHelper(ATR_PASS_ARGS,pBuyer,pEnt,pTree,pGroup->pRequires);

	// Check the node's specific requirement for another node at some rank
	if(bCanBuy)
	{
		PTNodeDef *pNodeDefRequired = GET_REF(pNodeDef->hNodeRequire);

		if(pNodeDefRequired)
		{
			NOCONST(PTNode) *pNodeRequired = entity_FindPowerTreeNodeHelper(pEnt,pNodeDefRequired);

			// Can buy if we own it and it's rank (0-based) is at least the required rank (1-based)
			bCanBuy = (pNodeRequired && !pNodeRequired->bEscrow && (pNodeRequired->iRank >= (pNodeDef->iRequired-1)));
		}
	}

	// Check the requirements on the rank
	if(bCanBuy && !pNodeDef->ppRanks[iRank]->bIgnoreRequires)
		bCanBuy = EntityPTPurchaseReqsHelper(ATR_PASS_ARGS,pBuyer,pEnt,pTree,pNodeDef->ppRanks[iRank]->pRequires);

	return bCanBuy;
}

AUTO_TRANS_HELPER;
bool entity_CanBuyPowerTreeNodeHelper(ATR_ARGS, int iPartitionIdx, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, PTGroupDef *pGroup, PTNodeDef *pNodeDef, int iRank, S32 bRequireNextRank, S32 bCheckGroupMax, S32 bSlave, S32 bIsTraining)
{
	int rtn=0;
	bool bCanBuy = true;

	// Can't buy slaved nodes unless you set the bSlave flag to true, which is only set to true via the SlaveHelper
	if(pNodeDef->bSlave && !bSlave)
	{
		DBGPOWERTREE_printf("Power Node is slaved to a MasterNode!\n");
		return false;
	}

	if(!bIsTraining && (pNodeDef->eFlag & kNodeFlag_RequireTraining))
	{
		DBGPOWERTREE_printf("Power Node can only be purchased in PowerStores!\n");
		return false;
	}

	//Are there any ranks left to upgrade to?
	if(eaSize(&pNodeDef->ppRanks) <= iRank || iRank < 0)
	{
		DBGPOWERTREE_printf("Power Node at Max Rank!\n");
		return false;
	}

	if(eaSize(&pNodeDef->ppRanks))
	{
		//Does the player have points to spend?
		PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
		NOCONST(PowerTree) *pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);
		NOCONST(PTNode) *pNode = powertree_FindNodeHelper(pTree, pNodeDef);
		int iCost;
		int iCurrentRank = (pNode && !pNode->bEscrow) ? pNode->iRank : -1;
		PTTypeDef *pTypeDef = GET_REF(pTreeDef->hTreeType);

		if (bRequireNextRank && pNode && pNode->bEscrow)
			iRank = 0;

		iCost = entity_PowerTreeNodeRankCostHelper(pEnt->pChar, pNodeDef, iRank);

		if (!pTreeDef)
		{
			DBGPOWERTREE_printf("Unable to find power tree for node %s\n", pNodeDef->pchNameFull);
			return false;
		}

		if ((bRequireNextRank && iCurrentRank != iRank-1) ||
			(!bRequireNextRank && iCurrentRank >= iRank))
		{
			DBGPOWERTREE_printf("Trying to buy rank %d but has rank %d\n", iRank, iCurrentRank);
			return false;
		}

		if(iCost < 0)
		{
			DBGPOWERTREE_printf("Can't determine appropriate cost");
			return false;
		}

		if(iCost > 0)
		{
			int iPoints = entity_PointsRemaining(pBuyer, pEnt, pTreeDef, pNodeDef->ppRanks[iRank]->pchCostTable);
			if(iPoints < iCost)
			{
				DBGPOWERTREE_printf("Player can not afford power");
				return false;
			}
			
			if(RankIsEnhancementHelper(pNodeDef->ppRanks[iRank]) && pNodeDef->iCostMaxEnhancement > 0)
			{
				S32 iSpent = entity_PowerTreeNodeEnhPointsSpentHelper(pEnt,pNodeDef);
				if(iSpent + iCost > pNodeDef->iCostMaxEnhancement)
				{
					DBGPOWERTREE_printf("Character is not allowed to spend that many enhancement points on that node\n");
					return false;
				}
			}
		}

		if (!pTree)
		{
			if (!entity_CanBuyPowerTreeHelper(iPartitionIdx,pEnt,pTreeDef,false))
			{
				DBGPOWERTREE_printf("Unable to purchase tree %s for node %s during %s", pTreeDef->pchName, pNodeDef->pchNameFull, __FUNCTION__);
				return false;
			}
		}
		else if (bCheckGroupMax && !pNode && pGroup->iMax > 0)
		{
			S32 iCount = powertree_CountOwnedInGroupHelper(pTree, pGroup);
			if(iCount >= pGroup->iMax)
			{
				DBGPOWERTREE_printf("Group is maxed out");
				return false;
			}
		}

		//If power purchases are limited to the character path
		if(gConf.bCharacterPathMustBeFollowed
			&& !s_bPowerTreesDisablePurchaseRules
			&& pNodeDef->ppRanks[iRank]->pchCostTable 
			&& !(pNodeDef->eFlag & kNodeFlag_AutoBuy) 
			&& (!pTypeDef || pTypeDef->bDisregardPath) )
		{
			if( entity_trh_HasAnyCharacterPath(pEnt) )	
			{
				//If the character has a path, get the next suggested node
				int i, j;
				bool bFound = false;
				CharacterPathSuggestedPurchase *pPurchase = NULL;
				CharacterPath** pPaths = NULL;

				eaStackCreate(&pPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
				entity_GetChosenCharacterPaths((Entity*)pEnt, &pPaths);

				for (j = 0; j < eaSize(&pPaths); j++)
				{
					CharacterPathChoice *pChoice = CharacterPath_GetNextChoiceFromCostTable((Entity*)pEnt, pPaths[j], pNodeDef->ppRanks[iRank]->pchCostTable, true);

					//But first make sure that cost table is specified... if it is, the purchases must follow the path
					for(i = eaSize(&pPaths[j]->eaSuggestedPurchases)-1; i>=0; i--)
					{
						if(stricmp(pPaths[j]->eaSuggestedPurchases[i]->pchPowerTable, pNodeDef->ppRanks[iRank]->pchCostTable)==0)
						{
							pPurchase = pPaths[j]->eaSuggestedPurchases[i];
							break;
						}
					}

					if(pPurchase && pChoice)
					{
						for(i = eaSize(&pChoice->eaSuggestedNodes)-1; i >= 0; i--)
						{
							PTNodeDef *pNextNodeDef = GET_REF(pChoice->eaSuggestedNodes[i]->hNodeDef);
							if(pNextNodeDef == pNodeDef)
							{
								bFound = true;
								break;
							}
						}
					}
					if (bFound)
						break;
				}
				//If the character path has the cost table specified (pPurchase) and the next node for purchase isn't the node specified, then return false
				if(!bFound)
				{
					DBGPOWERTREE_printf("Node [%s] is not the next in any of the CharacterPaths that this character must follow.\n",
						pNodeDef->pchName);
					return false;
				}
			}
		}

		PERFINFO_AUTO_START("CanBuyIgnorePoints",1);
		bCanBuy &= entity_CanBuyPowerTreeNodeIgnorePointsRankHelper(ATR_PASS_ARGS, pBuyer, pEnt, pTree, pGroup, pNodeDef, iRank);
		PERFINFO_AUTO_STOP();
		return bCanBuy;
	}

	return false;
}

AUTO_TRANS_HELPER;
bool entity_CanBuyPowerTreeGroupHelper(ATR_ARGS, int iPartitionIdx, ATH_ARG NOCONST(Entity) *pBuyer, ATH_ARG NOCONST(Entity) *pEnt, PTGroupDef *pGroupDef)
{
	int rtn=0;
	bool bCanBuy = true;

	PowerTreeDef *pTreeDef = powertree_TreeDefFromGroupDef(pGroupDef);
	NOCONST(PowerTree) *pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);

	if (!pTreeDef)
	{
		DBGPOWERTREE_printf("Unable to find power tree for group %s\n", pGroupDef->pchNameFull);
		return false;
	}

	if (!pTree)
	{
		if (!entity_CanBuyPowerTreeHelper(iPartitionIdx, pEnt,pTreeDef,false))
		{
			DBGPOWERTREE_printf("Unable to purchase tree %s for group %s during %s", pTreeDef->pchName, pGroupDef->pchNameFull, __FUNCTION__);
			return false;
		}
	}

	return EntityPTPurchaseReqsHelper(ATR_PASS_ARGS,pBuyer,pEnt,pTree,pGroupDef->pRequires);

}

NOCONST(PTNode)* entity_PowerTreeNodeAddHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const ItemChangeReason *pReason);
S32 entity_PowerTreeNodeSubHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(PowerTree) *pTree, ATH_ARG NOCONST(PTNode) *pNode, S32 bAlwaysRefundPoints, S32 bEscrow, const ItemChangeReason *pReason);


// Attempts to set the rank of a Node, which is assumed to be slaved.  Returns true if successful.  Rank of -1 means remove entirely.
AUTO_TRANS_HELPER;
S32 entity_PowerTreeNodeSlaveHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(PowerTree) *pTree, PTNodeDef *pNodeDef, S32 iRankMaster, const ItemChangeReason *pReason)
{
	S32 bSuccess = true;
	NOCONST(PTNode) *pNode;
	int iRankSlave;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pTree) || !pNodeDef)
		return false;

	pNode = eaIndexedGetUsingString(&pTree->ppNodes,pNodeDef->pchNameFull);
	iRankSlave = (pNode && !pNode->bEscrow) ? pNode->iRank : -1;

	// Can't set above the number of actual ranks
	if(pNodeDef && iRankMaster >= eaSize(&pNodeDef->ppRanks))
		iRankMaster = eaSize(&pNodeDef->ppRanks) - 1;

	while(bSuccess && iRankMaster!=iRankSlave)
	{
		if(iRankSlave<iRankMaster)
		{
			bSuccess = !!entity_PowerTreeNodeAddHelper(ATR_PASS_ARGS,pPayer,pEnt,pTree,pNodeDef,pReason);
			iRankSlave++;
		}
		else
		{
			// Currently too high, try decrease a rank
			bSuccess = entity_PowerTreeNodeSubHelper(ATR_PASS_ARGS,pEnt,pPayer,pTree,pNode,false,false,pReason);
			iRankSlave--;
			pNode = eaIndexedGetUsingString(&pTree->ppNodes,pNodeDef->pchNameFull);
		}
	}

	return bSuccess;
}

// Goes through all the Entity's Character's PowerTrees and makes sure all MasterNode nodes have properly matching slave nodes
AUTO_TRANS_HELPER;
S32 entity_PowerTreesSlaveHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pPayer, const ItemChangeReason *pReason)
{
	S32 i,j,k;
	S32 bSuccess = true;

	PERFINFO_AUTO_START_FUNC();

	// Go through all the owned trees
	for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1;i>=0;i--)
	{
		NOCONST(PowerTree) *pTree = pEnt->pChar->ppPowerTrees[i];
		PowerTreeDef *pTreeDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);

		if (!pTreeDef)
			continue;

		// Go through all the groups in the tree
		for(j=eaSize(&pTreeDef->ppGroups)-1;j>=0;j--)
		{
			// Default MasterRank to -1, which means you can't own any of the slave nodes
			S32 iMasterRank = -1;

			PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];

			if(!pGroupDef->bHasMasterNode)
				continue;

			// Find the MasterNode and find its rank
			for(k=eaSize(&pGroupDef->ppNodes)-1;k>=0;k--)
			{
				if(pGroupDef->ppNodes[k]->eFlag & kNodeFlag_MasterNode)
				{
					NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pEnt->pChar,NULL,pGroupDef->ppNodes[k]->pchNameFull);
					if(pNode && !pNode->bEscrow)
					{
						iMasterRank = pNode->iRank;
					}
					break;
				}
			}

			// Go through all the non-MasterNodes and make sure their rank matches iMasterRank (which is -1 if you don't own the MasterNode at all)
			//  This is much simpler than the AutoBuy loop because it doesn't care about the reference stuff at this level
			for(k=eaSize(&pGroupDef->ppNodes)-1;k>=0;k--)
			{
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[k];

				if(pNodeDef->eFlag & kNodeFlag_MasterNode)
					continue;
				bSuccess &= entity_PowerTreeNodeSlaveHelper(ATR_PASS_ARGS,pEnt,pPayer,pTree,pNodeDef,iMasterRank,pReason);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return bSuccess;
}


// Younger steps go to front (if A is younger than B, return 1)
static int CmpPowerTreeSteps(const PowerTreeStep **pStepA, const PowerTreeStep **pStepB)
{
	// Compare timestamp
	if((*pStepA)->uiTimestamp < (*pStepB)->uiTimestamp)
		return 1;
	if((*pStepA)->uiTimestamp > (*pStepB)->uiTimestamp)
		return -1;

	if ((*pStepA)->uiOrderIndex < (*pStepB)->uiOrderIndex)
		return 1;
	if((*pStepA)->uiOrderIndex > (*pStepB)->uiOrderIndex)
		return -1;

	// Still the same, look at rank
	return (*pStepA)->iRank - (*pStepB)->iRank;
}

// Lower cost steps go to front (if A is lower cost than B, return 1)
static int CmpPowerTreeStepsByTableLevel(const PowerTreeStep **pStepA, const PowerTreeStep **pStepB)
{
	PTNodeDef *pNodeDefA = powertreenodedef_Find((*pStepA)->pchNode);
	PTNodeDef *pNodeDefB = powertreenodedef_Find((*pStepB)->pchNode);

	if (pNodeDefA && eaSize(&pNodeDefA->ppRanks) > 0 && pNodeDefA->ppRanks[(*pStepA)->iRank]->pRequires &&
		pNodeDefB && eaSize(&pNodeDefB->ppRanks) > 0 && pNodeDefB->ppRanks[(*pStepB)->iRank]->pRequires)
	{
		int iTableLevelA = pNodeDefA->ppRanks[(*pStepA)->iRank]->pRequires->iTableLevel;
		int iTableLevelB = pNodeDefB->ppRanks[(*pStepB)->iRank]->pRequires->iTableLevel;

		if (iTableLevelA < iTableLevelB)
			return 1;
		if (iTableLevelA > iTableLevelB)
			return -1;
	}

	return CmpPowerTreeSteps(pStepA, pStepB);
}

S32 GetPowerTreeSteps_TotalCost(PowerTreeSteps *pSteps)
{
	S32 iStepIdx, iTotalCost = 0;
	for(iStepIdx = eaSize(&pSteps->ppSteps)-1; iStepIdx >= 0; iStepIdx--)
	{
		iTotalCost += pSteps->ppSteps[iStepIdx]->iCostRespec;
	}

	return(iTotalCost);
}

AUTO_TRANS_HELPER;
void trhEntity_RespecGetPowersAndTrees(ATH_ARG NOCONST(Entity) *e, RespecPowerPlayer ***pppPowerDefs, RespecPowerTreePlayer *** pppPowerTreeDefs)
{
	S32 i;

	if(NONNULL(e) && NONNULL(e->pChar))
	{
		for(i=eaSize(&e->pChar->ppPowerTrees)-1; i>=0; i--)
		{
			S32 j;
			NOCONST(PowerTree) *ptree = e->pChar->ppPowerTrees[i];
			if(NONNULL(ptree))
			{
				PowerTreeDef *pPtreeDef = GET_REF(ptree->hDef);
				if(NONNULL(pPtreeDef))
				{
					RespecPowerTreePlayer *pTreePlayer = StructCreate(parse_RespecPowerTreePlayer);
					pTreePlayer->pPowerTreeDef = pPtreeDef;
					pTreePlayer->iPurchaseTime = ptree->uiTimeCreated;
					eaPush(pppPowerTreeDefs, pTreePlayer);
				}

				for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
				{
					S32 k;
					PTNode *pnode = (PTNode *)ptree->ppNodes[j];

					if(ISNULL(pnode) || eaSize(&pnode->ppPowers) == 0)
						continue;

					if(pnode->bEscrow)
						continue;

					for(k=eaSize(&pnode->ppPowers)-1; k>=0; k--)
					{
						PowerDef *pdef = pnode->ppPowers[k] ? GET_REF(pnode->ppPowers[k]->hDef) : NULL;
						if(NONNULL(pdef))
						{
							RespecPowerPlayer *pPowerPlayer = StructCreate(parse_RespecPowerPlayer);
							pPowerPlayer->pPowerDef = pdef;
							pPowerPlayer->iPurchaseTime = pnode->ppPowers[k]->uiTimeCreated;
							eaPush(pppPowerDefs, pPowerPlayer);
						}
					}
					for(k=eaSize(&pnode->ppEnhancements)-1; k>=0; k--)
					{
						PowerDef *pdef = pnode->ppEnhancements[k] ? GET_REF(pnode->ppEnhancements[k]->hDef) : NULL;
						if(NONNULL(pdef))
						{
							RespecPowerPlayer *pPowerPlayer = StructCreate(parse_RespecPowerPlayer);
							pPowerPlayer->pPowerDef = pdef;
							pPowerPlayer->iPurchaseTime = pnode->ppEnhancements[k]->uiTimeCreated;
							eaPush(pppPowerDefs, pPowerPlayer);
						}
					}

				}
			}
		}
	}
}

#define INVALID_RESPEC_TIME 0xffffffff

AUTO_TRANS_HELPER;
U32 trhEntity_GetValidRespecTime(ATH_ARG NOCONST(Entity) *e, U32 bCheckForcedRespec)
{
	U32 bRespecTime = INVALID_RESPEC_TIME;
	S32 iNumRespecs = eaSize(&g_eaRespecs);
	bool bDonePowers = false;
	RespecPowerTreePlayer **ppPowerTreeDefs = NULL;
	RespecPowerPlayer **ppPowerDefs = NULL;
	U32 iTime = timeServerSecondsSince2000();
		
	if(NONNULL(e) && 
		NONNULL(e->pPlayer) &&
		NONNULL(e->pChar) &&
		iNumRespecs)
	{
		S32 iRespecIdx;
		
		bCheckForcedRespec = !!bCheckForcedRespec;
		
		for(iRespecIdx = 0; (iTime < bRespecTime && iRespecIdx < iNumRespecs); iRespecIdx++)
		{
			Respec *pRespec = g_eaRespecs[iRespecIdx];
			
			if( pRespec && 
				pRespec->bIsForcedRespec == bCheckForcedRespec && 
				pRespec->uiDerivedRespecTime < bRespecTime &&
				e->pPlayer->iCreatedTime <= pRespec->uiDerivedRespecTime &&
				((!pRespec->bIsForcedRespec && e->pChar->uiLastFreeRespecTime < pRespec->uiDerivedRespecTime) ||
				 (pRespec->bIsForcedRespec && e->pChar->uiLastForcedRespecTime < pRespec->uiDerivedRespecTime)))
			{
				switch(pRespec->eRespecType)
				{
				case kRespecType_All:
					{
						bRespecTime = pRespec->uiDerivedRespecTime;
						break;
					}
				case kRespecType_PowersAndTrees:
					{
						S32 i, j;

						if(!bDonePowers)
						{
							// create list of powers and nodes that the entity has
							trhEntity_RespecGetPowersAndTrees(e, &ppPowerDefs, &ppPowerTreeDefs);
							bDonePowers = true;
						}

						for(i = 0;  (bRespecTime != pRespec->uiDerivedRespecTime && i < eaSize(&pRespec->eaTrees)); ++i)
						{
							for(j = 0; j < eaSize(&ppPowerTreeDefs); ++j)
							{
								if(GET_REF(pRespec->eaTrees[i]->respecTreeDef) == ppPowerTreeDefs[j]->pPowerTreeDef && pRespec->uiDerivedRespecTime >= ppPowerTreeDefs[j]->iPurchaseTime)
								{
									bRespecTime = pRespec->uiDerivedRespecTime;
									break;
								}
							}
						}

						for(i = 0; (bRespecTime != pRespec->uiDerivedRespecTime && i < eaSize(&pRespec->eaPowers)); ++i)
						{
							for(j = 0; j < eaSize(&ppPowerDefs); ++j)
							{
								if(GET_REF(pRespec->eaPowers[i]->respecPowerDef) == ppPowerDefs[j]->pPowerDef && pRespec->uiDerivedRespecTime >= ppPowerDefs[j]->iPurchaseTime)
								{
									bRespecTime = pRespec->uiDerivedRespecTime;
									break;
								}
							}
						}

						break;
					}
				}
			}
		}
	}

	eaDestroyStruct(&ppPowerTreeDefs, parse_RespecPowerTreePlayer);
	eaDestroyStruct(&ppPowerDefs, parse_RespecPowerPlayer);

	return bRespecTime;
}

AUTO_TRANS_HELPER;
U32 trhEntity_GetForcedRespecTime(ATH_ARG NOCONST(Entity) *e)
{
	return trhEntity_GetValidRespecTime(e, true);
}

AUTO_TRANS_HELPER;
U32 trhEntity_GetFreeRespecTime(ATH_ARG NOCONST(Entity) *e)
{
	return trhEntity_GetValidRespecTime(e, false);
}

AUTO_TRANS_HELPER;
bool trhCharacter_UseForceRespecFromSchedule(ATH_ARG NOCONST(Entity)* e)
{
	if(ISNULL(e) || timeServerSecondsSince2000() < trhEntity_GetForcedRespecTime(e))
	{
		return false;
	}

	e->pChar->uiLastForcedRespecTime = timeServerSecondsSince2000();

	return true;
}

AUTO_TRANS_HELPER;
bool trhCharacter_UseFreeRespecFromSchedule(ATH_ARG NOCONST(Entity)* e)
{
	if(ISNULL(e) || timeServerSecondsSince2000() < trhEntity_GetFreeRespecTime(e))
	{
		return false;
	}

	e->pChar->uiLastFreeRespecTime = timeServerSecondsSince2000();

	return true;
}

// Fills the steps structure with the steps for the Character's PowerTrees, in order of newest to oldest
AUTO_TRANS_HELPER;
void character_GetPowerTreeSteps(ATH_ARG NOCONST(Character) *pchar, PowerTreeSteps *pSteps, bool bDoingStepRetCon, PTRespecGroupType eRespecGroupType)
{
	int i,j,k,l,c;
	PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);

	PERFINFO_AUTO_START_FUNC();

	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		NOCONST(PowerTree) *pTree = pchar->ppPowerTrees[i];
		PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);
		PTTypeDef *pPTTypeDef;

		if(!pTreeDef)
			continue;

		pPTTypeDef = GET_REF(pTreeDef->hTreeType);

		// Note that some power trees don't have a power tree type, NULL check is required
		if(bDoingStepRetCon && pPTTypeDef && pPTTypeDef->bOnlyRespecUsingFull)
		{
			// This tree can't be retconed here, requires full retcon
			continue;
		}

		if(eRespecGroupType != kPTRespecGroup_ALL)
		{
			bool bFound = false;
			S32 i1;

			if(!pPTTypeDef)
			{
				// no type so don't include
				continue;
			}

			for(i1 = 0; i1 < eaiSize(&pPTTypeDef->eaiRespecGroupType); ++i1)
			{
				if(pPTTypeDef->eaiRespecGroupType[i1] == eRespecGroupType)
				{
					bFound = true;
					break;
				}
			}

			if(!bFound)
			{
				// targeted respec, wrong group therefore skip it
				continue;
			}
		}

		pStep->pchTree = pTreeDef->pchName;
		pStep->pchNode = NULL;
		pStep->pchEnhancement = NULL;
		pStep->iRank = 0;
		pStep->uiTimestamp = pTree->uiTimeCreated;
		pStep->uiOrderIndex = 0;
		pStep->bStepIsLocked = pTree->bStepIsLocked;

		// Unbuy of actual tree (if it's not autobuy)
		if(!pTreeDef->bAutoBuy)
			eaPush(&pSteps->ppSteps,StructClone(parse_PowerTreeStep,pStep));

		for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
		{
			NOCONST(PTNode) *pNode = pTree->ppNodes[j];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

			if(!pNodeDef)
				continue;

			if (pNode->bEscrow)
				continue;

			pStep->pchNode = pNodeDef->pchNameFull;
			pStep->pchEnhancement = NULL;

			// Don't include AutoBuy or slaves
			if((!(pNodeDef->eFlag & kNodeFlag_AutoBuy) || pNodeDef->bForcedAutoBuy) && !pNodeDef->bSlave)
			{
				for(k=0,c=0; k<eaSize(&pNode->ppPurchaseTracker); k++)
				{
					// If this node def is not autobuy, push the step. Otherwise if it is autobuy, only push the step 
					// if it is a non-forced autobuy step with a cost. Forced autobuy and free autobuy ranks will be
					// handled by the autobuy system
					if(!(pNodeDef->eFlag & kNodeFlag_AutoBuy) || (!pNodeDef->ppRanks[k]->bForcedAutoBuy && pNodeDef->ppRanks[k]->iCost))
					{
						pStep->iRank = k;
						pStep->uiOrderIndex = pNode->ppPurchaseTracker[k]->uiOrderCreated;
						pStep->bStepIsLocked = pNode->ppPurchaseTracker[k]->bStepIsLocked;
						
						if(k<eaSize(&pNodeDef->ppRanks) && !pNodeDef->ppRanks[k]->bEmpty)
						{
							pStep->uiTimestamp = pNode->ppPowers[c++]->uiTimeCreated;
						}
						else
						{
							pStep->uiTimestamp = pNode->ppPurchaseTracker[k]->uiTimeCreated;
						}

						eaPush(&pSteps->ppSteps,StructClone(parse_PowerTreeStep,pStep));
					}
					// Even if we skipped pushing the step, we still might need to increment
					// the node's powers array index in order to get the right time created
					else if(k<eaSize(&pNodeDef->ppRanks) && !pNodeDef->ppRanks[k]->bEmpty)
					{
						c++;
					}
				}
			}

			for(k=eaSize(&pNode->ppEnhancementTrackers)-1; k>=0; k--)
			{
				PowerDef *pPowerDef = GET_REF(pNode->ppEnhancementTrackers[k]->hDef);

				if(!pPowerDef)
					continue;

				pStep->pchEnhancement = pPowerDef->pchName;

				for(l=0; l<eaSize(&pNode->ppEnhancementTrackers[k]->ppPurchases); l++)
				{
					pStep->iRank = l;
					pStep->uiTimestamp = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiTimeCreated;
					pStep->uiOrderIndex = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiOrderCreated;
					pStep->bStepIsLocked = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->bStepIsLocked;
					eaPush(&pSteps->ppSteps,StructClone(parse_PowerTreeStep,pStep));
				}
			}
		}
	}

	if (gConf.bSortPowerTreeStepsByTableLevel)
		eaQSort(pSteps->ppSteps,CmpPowerTreeStepsByTableLevel);
	else
		eaQSort(pSteps->ppSteps,CmpPowerTreeSteps);

	StructDestroy(parse_PowerTreeStep,pStep);

	PERFINFO_AUTO_STOP();
}

// Fills the steps structure with the steps for the Character's PowerTrees, in order of newest to oldest
//  IDENTICAL to character_GetPowerTreeSteps, except it doesn't push the trees or the first rank of a node 
AUTO_TRANS_HELPER;
void character_GetPowerTreeAdvantages(ATH_ARG NOCONST(Character) *pchar, PowerTreeSteps *pSteps)
{
	int i,j,k,l;
	PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);

	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		NOCONST(PowerTree) *pTree = pchar->ppPowerTrees[i];
		PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);

		if(!pTreeDef)
			continue;

		pStep->pchTree = pTreeDef->pchName;
		pStep->pchNode = NULL;
		pStep->pchEnhancement = NULL;
		pStep->iRank = 0;
		pStep->uiTimestamp = pTree->uiTimeCreated;

		for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
		{
			NOCONST(PTNode) *pNode = pTree->ppNodes[j];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

			if(!pNodeDef)
				continue;

			if (pNode->bEscrow)
				continue;

			pStep->pchNode = pNodeDef->pchNameFull;
			pStep->pchEnhancement = NULL;

			if(!(pNodeDef->eFlag & kNodeFlag_AutoBuy) && !pNodeDef->bSlave)
			{
				for(k=eaSize(&pNode->ppPowers)-1; k>=1; k--)
				{
					pStep->iRank = k;
					pStep->uiTimestamp = pNode->ppPowers[k]->uiTimeCreated;
					eaPush(&pSteps->ppSteps,StructClone(parse_PowerTreeStep,pStep));
				}
			}

			for(k=eaSize(&pNode->ppEnhancementTrackers)-1; k>=0; k--)
			{
				PowerDef *pPowerDef = GET_REF(pNode->ppEnhancementTrackers[k]->hDef);

				if(!pPowerDef)
					continue;

				pStep->pchEnhancement = pPowerDef->pchName;

				for(l=0; l<eaSize(&pNode->ppEnhancementTrackers[k]->ppPurchases); l++)
				{
					pStep->iRank = l;
					pStep->uiTimestamp = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiTimeCreated;
					pStep->uiOrderIndex = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiOrderCreated;
					eaPush(&pSteps->ppSteps,StructClone(parse_PowerTreeStep,pStep));
				}
			}
		}
	}

	if (gConf.bSortPowerTreeStepsByTableLevel)
		eaQSort(pSteps->ppSteps,CmpPowerTreeStepsByTableLevel);
	else
		eaQSort(pSteps->ppSteps,CmpPowerTreeSteps);

	StructDestroy(parse_PowerTreeStep,pStep);
}

// Fills in the Character's ppPointsSpent with points spent by the Character's PowerTrees
AUTO_TRANS_HELPER;
void character_UpdatePointsSpentPowerTrees(ATH_ARG NOCONST(Character) *pchar, S32 bOnlyLowerPoints)
{
	int i,j,k;
	NOCONST(CharacterPointSpent) **eaOldPoints = NULL;
	
	if(bOnlyLowerPoints)
	{
		eaIndexedEnableNoConst(&eaOldPoints,parse_CharacterPointSpent);
		for(i = 0; i < eaSize(&pchar->ppPointSpentPowerTrees); ++i)
		{	
			NOCONST(CharacterPointSpent) *pPoints = StructCloneNoConst(parse_CharacterPointSpent, pchar->ppPointSpentPowerTrees[i]);
			eaIndexedAdd(&eaOldPoints, pPoints);
		}
	}
	else
	{
		eaDestroyStructNoConst(&pchar->ppPointSpentPowerTrees,parse_CharacterPointSpent);
	}

	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		NOCONST(PowerTree) *pTree = pchar->ppPowerTrees[i];
		for(j=eaSize(&pTree->ppNodes)-1; j>=0; j--)
		{
			NOCONST(PTNode) *pNode = pTree->ppNodes[j];
			PTNodeDef *pdefNode = GET_REF(pNode->hDef);

			if(pNode->bEscrow || !pdefNode)
				continue;

			// Add the cost for each rank
			for(k=pNode->iRank; k>=0; k--)
			{
				int iCost = entity_PowerTreeNodeRankCostHelper(pchar, pdefNode, k);
				if(iCost>0)
				{
					PTNodeRankDef *pdefRank = pdefNode->ppRanks[k];
					NOCONST(CharacterPointSpent) *pSpent = eaIndexedGetUsingString(&pchar->ppPointSpentPowerTrees,pdefRank->pchCostTable);
					if(!pSpent)
					{
						pSpent = StructCreateNoConst(parse_CharacterPointSpent);
						pSpent->pchPoint = StructAllocString(pdefRank->pchCostTable);
						if(!pchar->ppPointSpentPowerTrees)
						{
							eaIndexedEnableNoConst(&pchar->ppPointSpentPowerTrees,parse_CharacterPointSpent);
						}
						eaIndexedAdd(&pchar->ppPointSpentPowerTrees,pSpent);
					}
					pSpent->iSpent += iCost;
				}
			}

			// Add the cost for each enhancement
			for(k=eaSize(&pdefNode->ppEnhancements)-1; k>=0; k--)
			{
				PTNodeEnhancementDef *pdefNodeEnh = pdefNode->ppEnhancements[k];
				int iRankEnh = powertreenode_FindEnhancementRankHelper(pNode,GET_REF(pdefNodeEnh->hPowerDef));
				if(iRankEnh > 0)
				{
					NOCONST(CharacterPointSpent) *pSpent = eaIndexedGetUsingString(&pchar->ppPointSpentPowerTrees,pdefNodeEnh->pchCostTable);
					if(!pSpent)
					{
						pSpent = StructCreateNoConst(parse_CharacterPointSpent);
						pSpent->pchPoint = StructAllocString(pdefNodeEnh->pchCostTable);
						if(!pchar->ppPointSpentPowerTrees)
						{
							eaIndexedEnableNoConst(&pchar->ppPointSpentPowerTrees,parse_CharacterPointSpent);
						}
						eaIndexedAdd(&pchar->ppPointSpentPowerTrees,pSpent);
					}
					pSpent->iSpent += (iRankEnh * pdefNodeEnh->iCost);
				}
			}
		}
	}

	if(bOnlyLowerPoints)
	{
		for(i = 0; i < eaSize(&pchar->ppPointSpentPowerTrees); ++i)
		{
			NOCONST(CharacterPointSpent) *pSpent = eaIndexedGetUsingString(&eaOldPoints,pchar->ppPointSpentPowerTrees[i]->pchPoint);
			if(pSpent)
			{
				if(pSpent->iSpent < pchar->ppPointSpentPowerTrees[i]->iSpent)
				{
					// old value was lower
					pchar->ppPointSpentPowerTrees[i]->iSpent = pSpent->iSpent;
				}
			}
			else
			{
				// There wasn't an old value so new value must be zero
				pchar->ppPointSpentPowerTrees[i]->iSpent = 0;
			}
		}

		eaDestroyStructNoConst(&eaOldPoints, parse_CharacterPointSpent);
	}
}

AUTO_TRANS_HELPER;
void character_LockAllPowerTrees(ATH_ARG NOCONST(Character) *pChar)
{
	int i, j, k, l;

	for (i = eaSize(&pChar->ppPowerTrees)-1; i >= 0; i--)
	{
		NOCONST(PowerTree) *pTree = pChar->ppPowerTrees[i];
		pTree->bStepIsLocked = true;
		for (j = eaSize(&pTree->ppNodes)-1; j >= 0; j--)
		{
			NOCONST(PTNode) *pNode = pTree->ppNodes[j];
			for(k=0; k<eaSize(&pNode->ppPurchaseTracker); k++)
			{
				pNode->ppPurchaseTracker[k]->bStepIsLocked = true;
			}

			for(k = eaSize(&pNode->ppEnhancementTrackers)-1; k >= 0; k--)
			{
				for(l=0; l<eaSize(&pNode->ppEnhancementTrackers[k]->ppPurchases); l++)
				{
					pNode->ppEnhancementTrackers[k]->ppPurchases[l]->bStepIsLocked = true;
				}
			}
		}
	}
}

int character_RespecPointsSpentRequirement(int iPartitionIdx, Character *pChar) 
{
	if(g_PowerTreeRespecConfig.pExprRequiredPointsSpent)
	{
		MultiVal *pMV = MultiValCreate();
		ExprContext *pContext = powerTreeEval_GetContextRespec();
		exprContextSetSelfPtr(pContext,pChar->pEntParent);
		exprContextSetPartition(pContext, iPartitionIdx);

		exprEvaluate(g_PowerTreeRespecConfig.pExprRequiredPointsSpent,pContext,pMV);
		return MultiValGetInt(pMV,NULL);
	}

	return 0;
}

// Calculates the iCostRespec for each step in the steps structure.  Costs all the steps if iStepsToCost is 0.
void character_PowerTreeStepsCostRespec(int iPartitionIdx, Character *pchar, PowerTreeSteps *pSteps, S32 iStepsToCost)
{
	int i;

	MultiVal *pMV = MultiValCreate();
	ExprContext *pContext = powerTreeEval_GetContextRespec();
	exprContextSetSelfPtr(pContext,pchar->pEntParent);
	exprContextSetPointerVar(pContext,"Steps",pSteps,parse_PowerTreeSteps,false,false);
	exprContextSetPartition(pContext, iPartitionIdx);
	
	if(!iStepsToCost)
		iStepsToCost = eaSize(&pSteps->ppSteps);

	for(i=0; i<iStepsToCost; i++)
	{
		// Evaluate some expression here, cleverly updating the unspent point values
		PowerTreeStep *pStep = pSteps->ppSteps[i];
		
		// Default cost is 0
		pStep->iCostRespec = 0;

		if(g_PowerTreeRespecConfig.pExprCostStep)
		{
			if(pStep->pchEnhancement)
			{
				// Cost for an enhancement
				exprContextSetIntVar(pContext, "StepIndex", i);
				exprEvaluate(g_PowerTreeRespecConfig.pExprCostStep, pContext, pMV);
				pStep->iCostRespec = MultiValGetInt(pMV, NULL);
			}
			else if(pStep->pchNode)
			{
				// Cost for a node rank
				exprContextSetIntVar(pContext, "StepIndex", i);
				exprEvaluate(g_PowerTreeRespecConfig.pExprCostStep, pContext, pMV);
				pStep->iCostRespec = MultiValGetInt(pMV, NULL);
			}
			else
			{
				// Cost for a tree (currently always free)
			}
		}

		if(i==0)
		{
			if(g_PowerTreeRespecConfig.pExprCostBase)
			{
				exprContextSetIntVar(pContext, "StepIndex", i);
				exprEvaluate(g_PowerTreeRespecConfig.pExprCostBase,pContext,pMV);
				pStep->iCostRespec += MultiValGetInt(pMV,NULL);
			}
		}
	}

	MultiValDestroy(pMV);
}

AUTO_TRANS_HELPER;
void entity_PowerTreeResetSpentNumericsOnFakeEnt( ATH_ARG NOCONST(Entity) *pEnt, NOCONST(Entity) *pFakeEnt )
{
	S32 i;
	for ( i = eaSize( &pEnt->pChar->ppPowerTrees ) - 1; i >= 0; i-- )
	{
		NOCONST(PowerTree)* pTree = pEnt->pChar->ppPowerTrees[i];
		PowerTreeDef* pTreeDef = GET_REF(pTree->hDef);
		PTTypeDef* pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;

		if ( pTreeTypeDef && pTreeTypeDef->pchSpentPointsNumeric && pTreeTypeDef->pchSpentPointsNumeric[0] )
		{
			const char* pchNumeric = pTreeTypeDef->pchSpentPointsNumeric;

			inv_ent_trh_SetNumeric(ATR_EMPTY_ARGS,pFakeEnt,true,pchNumeric,0,NULL);
		}
	}
}

// Returns true if the node in this step is on the entity's path, or if the entity does not have a path,
// or if the node does not appear on a power tree in the path.
S32 entity_PowerTreeValidateNodeOnPath(NOCONST(Entity) *pEnt, PowerTreeStep *pStep)
{
	CharacterClass *pClass = GET_REF(pEnt->pChar->hClass);
	PTNodeDef *pNodeDef = powertreenodedef_Find(pStep->pchNode);
	bool bFound = false;
	int i, j, k, iPath;
	CharacterPath** eaPaths = NULL;

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_trh_GetChosenCharacterPaths(pEnt, &eaPaths);

	for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
	{
		// Check to see if any of this node's rank's cost tables show up in the path
		for(i = eaSize(&pNodeDef->ppRanks) - 1; i >= 0; i--)
		{
			PTNodeRankDef *pRankDef = pNodeDef->ppRanks[i];
			if (pClass && pRankDef)
			{
				for(j = eaSize(&eaPaths[iPath]->eaSuggestedPurchases) - 1; j >= 0; j--)
				{
					CharacterPathSuggestedPurchase *pPurchase = eaPaths[iPath]->eaSuggestedPurchases[j];
					if (pPurchase)
					{
						if (stricmp(pPurchase->pchPowerTable, pRankDef->pchCostTable) == 0)
						{
							bFound = true;
							break;
						}
					}
				}

				if (bFound)
				{
					break;
				}
			}
		}
	}

	// If none of the cost tables show up in the path, then the node is valid
	if (!bFound)
		return true;

	for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
	{
		// Check to see if the node exists in the character's path
		for(i = eaSize(&eaPaths[iPath]->eaSuggestedPurchases) - 1; i >= 0; i--)
		{
			CharacterPathSuggestedPurchase *pPurchase = eaPaths[iPath]->eaSuggestedPurchases[i];
			if (pPurchase)
			{
				for(j = eaSize(&pPurchase->eaChoices) - 1; j >= 0; j--)
				{
					CharacterPathChoice *pChoice = pPurchase->eaChoices[j];
					if (pChoice)
					{
						for(k = eaSize(&pChoice->eaSuggestedNodes) - 1; k >= 0; k--)
						{
							CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[k];
							if (pSuggestedNode)
							{
								pNodeDef = GET_REF(pSuggestedNode->hNodeDef);
								if (pNodeDef && (stricmp(pNodeDef->pchNameFull, pStep->pchNode) == 0))
								{
									// The node exists in the path, so the node is valid
									return true;
								}
							}
						}
					}
				}
			}
		}
	}
	// The node was not found in the path, so the node is invalid
	return false;
}

// Returns true if the Entity's PowerTrees are valid in the state defined by pSteps
//  ppchFailure can be provided as an estring for detailed explanation in the case of failure.
S32 entity_PowerTreesValidateSteps(int iPartitionIdx, Entity *pEnt, Entity *pPayer, NOCONST(Entity) *pentFake, PowerTreeSteps *pSteps, PowerTreeValidateResults *pResults, bool bGetAllFailedSteps)
{
	S32 i,bValid = true;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("CreateFake",1);

	PERFINFO_AUTO_START("StructCreates",1);

	pentFake->pChar = StructCreateNoConst(parse_Character);

	PERFINFO_AUTO_STOP_START("CopyHandles",1); // StructCreates
		
	// Make a fake Inventory by copying class, and species, making an empty Player, and calling the Verify function, then directly copying in XP
	COPY_HANDLE(pentFake->pChar->hClass,pEnt->pChar->hClass);
	COPY_HANDLE(pentFake->pChar->hPath,pEnt->pChar->hPath);
	eaCopyStructsDeConst(&pEnt->pChar->ppSecondaryPaths, &pentFake->pChar->ppSecondaryPaths, parse_AdditionalCharacterPath);
	COPY_HANDLE(pentFake->pChar->hSpecies,pEnt->pChar->hSpecies);

	if(pPayer)
	{
		COPY_HANDLE(pentFake->hFaction,pPayer->hFaction);
		COPY_HANDLE(pentFake->hSubFaction,pPayer->hSubFaction);
		COPY_HANDLE(pentFake->hAllegiance,pPayer->hAllegiance);
		COPY_HANDLE(pentFake->hSubAllegiance,pPayer->hSubAllegiance);
	}
	else
	{
		COPY_HANDLE(pentFake->hFaction,pEnt->hFaction);
		COPY_HANDLE(pentFake->hSubFaction,pEnt->hSubFaction);
		COPY_HANDLE(pentFake->hAllegiance,pEnt->hAllegiance);
		COPY_HANDLE(pentFake->hSubAllegiance,pEnt->hSubAllegiance);
	}

	PERFINFO_AUTO_STOP(); // CopyHandles

	if (pEnt->pSaved && pEnt->pSaved->pPuppetMaster)
	{
		PERFINFO_AUTO_START("PuppetMaster",1);
		pentFake->pSaved = StructCreateNoConst(parse_SavedEntityData);
		pentFake->pSaved->pPuppetMaster = StructCloneDeConst(parse_PuppetMaster,pEnt->pSaved->pPuppetMaster);
		PERFINFO_AUTO_STOP();
	}

	if(pEnt->pPlayer)
	{
		PERFINFO_AUTO_START("Player",1);
		pentFake->pPlayer = StructCreateNoConst(parse_Player);

		//Copy the game account data handle
		COPY_HANDLE(pentFake->pPlayer->pPlayerAccountData->hData, pEnt->pPlayer->pPlayerAccountData->hData);

		StructCopyAllDeConst(parse_MissionInfo, pEnt->pPlayer->missionInfo, pentFake->pPlayer->missionInfo);
		PERFINFO_AUTO_STOP();
	}
		
	// TODO(JW): Why is this here?
	PERFINFO_AUTO_START("VerifyInventory",1);
	inv_ent_trh_VerifyInventoryData(ATR_EMPTY_ARGS,pentFake,true,true,NULL);
	PERFINFO_AUTO_STOP();

	// TODO(JW): Why is this here?
	if (pEnt->pCritter)
	{
		PERFINFO_AUTO_START("Critter",1);
		pentFake->pCritter = StructCreateNoConst(parse_Critter);
		PERFINFO_AUTO_STOP();
	}
	
	PERFINFO_AUTO_START("CopyNumerics",1);
	inv_trh_CopyNumerics(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, (pPayer) ? pPayer : pEnt), pentFake, NULL);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("ResetSpent",1);
	entity_PowerTreeResetSpentNumericsOnFakeEnt(CONTAINER_NOCONST(Entity, pEnt), pentFake);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_STOP_START("Reconstruct",1); // CreateFake

	// Work from oldest to newest, attempting to reconstruct the PowerTrees
	for(i=eaSize(&pSteps->ppSteps)-1; i>=0; i--)
	{
		PowerTreeStep *pStep = pSteps->ppSteps[i];
		if(pStep->pchEnhancement)
		{
			if(!entity_PowerTreeNodeEnhanceHelper(iPartitionIdx,pentFake,pStep->pchTree,pStep->pchNode,pStep->pchEnhancement,true,NULL))
			{
				if(bValid && pResults)
				{
					estrPrintf(&pResults->estrFailure,"Doppelganger step %d (%d) unable to purchase enhancement %s for node %s of tree %s",eaSize(&pSteps->ppSteps)-i,pStep->uiTimestamp,pStep->pchEnhancement,pStep->pchNode,pStep->pchTree);
				}
				bValid = false;
				if (bGetAllFailedSteps && pResults) {
					eaPush(&pResults->ppFailedSteps, StructClone(parse_PowerTreeStep, pStep));
				} else {
					break;
				}
			}
		}
		else if(pStep->pchNode)
		{
			if(!entity_PowerTreeValidateNodeOnPath(pentFake, pStep) ||
			   !entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,pentFake,NULL,pStep->pchTree,pStep->pchNode,false,true,true,NULL))
			{
				if(bValid && pResults)
				{
					NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pentFake->pChar, NULL, pStep->pchNode);
					int iRank = pNode ? pNode->iRank+1 : 0;
					estrPrintf(&pResults->estrFailure,"Doppelganger step %d (%d) unable to increase rank (current rank %d) of node %s of tree %s",eaSize(&pSteps->ppSteps)-i,pStep->uiTimestamp,iRank,pStep->pchNode,pStep->pchTree);
				}
				bValid = false;
				if (bGetAllFailedSteps && pResults) {
					eaPush(&pResults->ppFailedSteps, StructClone(parse_PowerTreeStep, pStep));
				} else {
					break;
				}
			}
		}
		else if(pStep->pchTree)
		{
			if(!entity_PowerTreeModifyHelper(iPartitionIdx,pentFake,NULL,pStep->pchTree,true,NULL))
			{
				if(bValid && pResults)
				{
					estrPrintf(&pResults->estrFailure,"Doppelganger step %d (%d) unable to purchase tree %s",eaSize(&pSteps->ppSteps)-i,pStep->uiTimestamp,pStep->pchTree);
				}
				bValid = false;
				if (bGetAllFailedSteps && pResults) {
					eaPush(&pResults->ppFailedSteps, StructClone(parse_PowerTreeStep, pStep));
				} else {
					break;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return bValid;
}

// Returns true if the Entity's PowerTrees are valid in the current state.
//  ppchFailure can be provided as an estring for detailed explanation in the case of failure.
S32 entity_PowerTreesValidate(int iPartitionIdx, Entity *pEnt, Entity *pPayer, PowerTreeValidateResults *pResults)
{
	S32 i,j,k, bValid = true;
	PowerTreeSteps *pSteps;
	NOCONST(Entity) *pentFake;
	
	// No Entity/Character at all is always valid
	if(!pEnt->pChar)
		return bValid;

	// No PowerTrees at all is always valid
	if(!eaSize(&pEnt->pChar->ppPowerTrees))
		return bValid;

	PERFINFO_AUTO_START_FUNC();

	// Make a fake Entity/Character
	pentFake = StructCreateWithComment(parse_Entity, "Fake ent created during entity_PowerTreesValidate");

	// Get the steps to recreate the PowerTrees
	pSteps = StructCreate(parse_PowerTreeSteps);
	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pEnt->pChar),pSteps, false, kPTRespecGroup_ALL);

	bValid = entity_PowerTreesValidateSteps(iPartitionIdx, pEnt, pPayer, pentFake, pSteps, pResults, false);

	PERFINFO_AUTO_STOP_START("Match",1);

	if(bValid)
	{
		// We skipped AutoBuy in the process, go ahead and call it now
		entity_PowerTreeAutoBuyHelper(iPartitionIdx,pentFake,NULL,NULL);

		// Still need to confirm that you own what you should own, nothing more
		for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1; bValid && i>=0; i--)
		{
			NOCONST(PowerTree) *pTreeCompare;
			PowerTreeDef *pdefTree = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);
				
			if(!(pdefTree && (pTreeCompare=entity_FindPowerTreeHelper(pentFake,pdefTree))))
			{
				// Fake version doesn't own this Tree
				bValid = false;
				if(pResults)
				{
					estrPrintf(&pResults->estrFailure,"Doppelganger does not own tree %s, which is owned by original",REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->hDef));
				}
				break;
			}

			for(j=eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes)-1; bValid && j>=0; j--)
			{
				NOCONST(PTNode) *pNodeCompare;
				PTNodeDef *pdefNode = GET_REF(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->hDef);

				if(!(pdefNode && (pNodeCompare=powertree_FindNodeHelper(pTreeCompare,pdefNode))))
				{
					// Fake version doesn't own this Node in the matching Tree, break if it's not in escrow
					if(!pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->bEscrow)
					{
						bValid = false;
						if(pResults)
						{
							estrPrintf(&pResults->estrFailure,"Doppelganger does not own node %s, which is owned by original",REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->hDef));
						}
						break;
					}
					continue;
				}

				if(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->iRank != pNodeCompare->iRank)
				{
					// Fake version owns a different rank of this Node in the matching Tree
					bValid = false;
					if(pResults)
					{
						estrPrintf(&pResults->estrFailure,"Doppelganger node %s is rank %d, original is rank %d",REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->hDef),pNodeCompare->iRank+1,pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->iRank+1);
					}
					break;
				}

				for(k=eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers)-1; k>=0; k--)
				{
					NOCONST(PTNodeEnhancementTracker) *pEnhCompare;
					PowerDef *pdefEnh = GET_REF(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers[k]->hDef);

					if(!(pdefEnh && (pEnhCompare=powertreenode_FindEnhancementTrackerHelper(pNodeCompare,pdefEnh))))
					{
						// Fake version doesn't own this Enhancement in the matching Node, break if not in escrow
						bValid = false;
						if(pResults)
						{
							estrPrintf(&pResults->estrFailure,"Doppelganger does not own enhancement %s for node %s, which is owned by original",REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers[k]->hDef),REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->hDef));
						}
						break;
					}

					if(eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers[k]->ppPurchases) != eaSize(&pEnhCompare->ppPurchases))
					{
						// Fake version owns a different number of purchases of the Enhancement in the matching Node
						bValid = false;
						if(pResults)
						{
							estrPrintf(&pResults->estrFailure,"Doppelganger enhancement %s for node %s is rank %d, original is rank %d",REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers[k]->hDef),REF_STRING_FROM_HANDLE(pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->hDef),eaSize(&pEnhCompare->ppPurchases),eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes[j]->ppEnhancementTrackers[k]->ppPurchases));
						}
						break;
					}
				}
			}
		}
	}

	StructDestroy(parse_PowerTreeSteps,pSteps);
	StructDestroyNoConst(parse_Entity,pentFake);
	PERFINFO_AUTO_STOP();
	return bValid;
}

AUTO_TRANS_HELPER;
U32 character_PowerTreeGetCreationIndexHelper( ATH_ARG NOCONST(Character) *pChar, PTNodeDef *pNodeDefIgnore )
{
	S32 i,j,k,l;
	U32 uiHighIndex = 0;
	
	for ( i = eaSize( &pChar->ppPowerTrees ) - 1; i >= 0; i-- )
	{
		for ( j = eaSize( &pChar->ppPowerTrees[i]->ppNodes ) - 1; j >= 0; j-- )
		{
			NOCONST(PTNode)* pNode = pChar->ppPowerTrees[i]->ppNodes[j];

			if ( GET_REF(pNode->hDef) == pNodeDefIgnore )
				continue;

			if ( pNode->bEscrow )
				continue;

			for ( k = 0; k < eaSize(&pNode->ppPurchaseTracker); k++ )
			{
				if ( pNode->ppPurchaseTracker[k]->uiOrderCreated > uiHighIndex )
				{
					uiHighIndex = pNode->ppPurchaseTracker[k]->uiOrderCreated;
				}
			}

			for ( k = 0; k < eaSize(&pNode->ppEnhancementTrackers); k++ )
			{
				for ( l = 0; l < eaSize(&pNode->ppEnhancementTrackers[k]->ppPurchases); l++ )
				{
					if ( pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiOrderCreated > uiHighIndex )
					{
						uiHighIndex = pNode->ppEnhancementTrackers[k]->ppPurchases[l]->uiOrderCreated;
					}
				}
			}
		}
	}

	return uiHighIndex + 1;
}

// Adds or increases the rank of a PowerTree Node
//  If the Node was in escrow, this unlocks it and adds the 0-rank Power
AUTO_TRANS_HELPER;
NOCONST(PTNode)* entity_PowerTreeNodeAddHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(PowerTree) *pTree, SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const ItemChangeReason *pReason)
{
	NOCONST(PowerTree)* pTreeFound = NULL;
	NOCONST(PTNode)* pNode;
	NOCONST(PowerPurchaseTracker)* pTracker;
	PowerDef *pPowerDef = NULL;
	S32 iRankNew;
	PowerTreeDef *pTreeDef;
	PTTypeDef* pTreeTypeDef;


	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pTree) || !pNodeDef)
		return NULL;

	pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, &pTreeFound, pNodeDef->pchNameFull);

	// Make sure the PowerTree we were told to put this in is the same as the PowerTree we found the PTNode in
	if(pNode && pTreeFound!=pTree)
		return NULL;

	iRankNew = (pNode && !pNode->bEscrow) ? pNode->iRank + 1 : 0;

	// Make sure the new rank is a legal number
	if(iRankNew >= eaSize(&pNodeDef->ppRanks))
		return NULL;

	ANALYSIS_ASSUME(pNodeDef->ppRanks && pNodeDef->ppRanks[iRankNew]);

	// If it's not marked empty, get the PowerDef
	if(!pNodeDef->ppRanks[iRankNew]->bEmpty)
	{
		pPowerDef = GET_REF(pNodeDef->ppRanks[iRankNew]->hPowerDef);
		// This shouldn't ever fail, but we'll return gracefully if it does.
		if(!pPowerDef)
			return NULL;
	}

	if(!pNode)
	{
		// Create a new node and put it in the tree
		pNode = CONTAINER_NOCONST(PTNode, powertreenode_create(pNodeDef));
		eaIndexedEnableNoConst(&pTree->ppNodes, parse_PTNode);
		eaPush(&pTree->ppNodes, pNode);
	}

	// If this gives us a Power, create it and put it in the node
	if(pPowerDef)
	{
		int iNodePowers = eaSize(&pNode->ppPowers);
		NOCONST(Power)* pPower = entity_CreatePowerHelper(pEnt, pPowerDef, 0);

		// If we need to copy any data from the previous power, do that now
		if(iNodePowers > 0)
		{
			NOCONST(Power) *ppowPrev = pNode->ppPowers[iNodePowers-1];
			if(ppowPrev)
			{
				power_SetHueHelper(pPower,ppowPrev->fHue);
				power_SetEmitHelper(pPower,REF_STRING_FROM_HANDLE(ppowPrev->hEmit));
				power_SetEntCreateCostumeHelper(pPower,ppowPrev->iEntCreateCostume);
			}
		}

		eaPush(&pNode->ppPowers,pPower);
	}

	//create a tracker for this rank
	pTracker = StructCreateNoConst(parse_PowerPurchaseTracker);
	pTracker->uiTimeCreated = timeSecondsSince2000();
	pTracker->uiOrderCreated = character_PowerTreeGetCreationIndexHelper(pEnt->pChar,iRankNew==0?pNodeDef:NULL);
	eaPush(&pNode->ppPurchaseTracker, pTracker);

	pNode->iRank = iRankNew;

	if ( pNode->bEscrow )
		pNode->bEscrow = false;

	// If this PowerTree has a "spent" numeric, we have to actually "spend" them by
	//  adding the cost in the numeric to the pPayer.  The pEnt also spends them for
	//  record-keeping.
	pTreeDef = GET_REF(pTree->hDef);
	pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;
	if(pTreeTypeDef && pTreeTypeDef->pchSpentPointsNumeric && pTreeTypeDef->pchSpentPointsNumeric[0])
	{
		S32 iCost = entity_PowerTreeNodeRankCostHelper(pEnt->pChar, pNodeDef, iRankNew);
		if(iCost)
		{
			inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,true,pTreeTypeDef->pchSpentPointsNumeric,iCost,pReason);

			if(NONNULL(pPayer))
				inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pPayer,true,pTreeTypeDef->pchSpentPointsNumeric,iCost,pReason);
		}
	}

	// Run slave helper (safe since slaving disregards requirements)
	if(pNodeDef->eFlag&kNodeFlag_MasterNode)
	{
		S32 bSlaveSuccess = entity_PowerTreesSlaveHelper(ATR_PASS_ARGS,pEnt,pPayer,pReason);
		if(!bSlaveSuccess)
		{
			if(entIsServer() || pEnt->myContainerID!=0)
				devassertmsg(bSlaveSuccess,"PowerTreesSlaveHelper Failed");
			return NULL;
		}
	}

#ifdef GAMESERVER
	if(pEnt->myEntityType!=GLOBALTYPE_NONE) // Don't bother with QueuedRemoteCommands for fake Entities
	{
		// If the new node rank has a trainer unlock node specified, then notify that it has been unlocked
		if(GET_REF(pNodeDef->ppRanks[iRankNew]->hTrainerUnlockNode))
		{
			TrainerUnlockCBData* pData = StructCreate(parse_TrainerUnlockCBData);
			COPY_HANDLE(pData->hNodeDef,pNodeDef->ppRanks[iRankNew]->hTrainerUnlockNode);
			pData->uiTrainerType = pEnt->myEntityType;
			pData->uiTrainerID = pEnt->myContainerID;
			QueueRemoteCommand_RemoteTrainerUnlockNotify(ATR_RESULT_SUCCESS,0,0,pData);
			StructDestroy(parse_TrainerUnlockCBData,pData);
		}

		QueueRemoteCommand_powertree_PlayAnimListOnGrantPower(ATR_RESULT_SUCCESS, pEnt->myEntityType, pEnt->myContainerID, REF_STRING_FROM_HANDLE(pTree->hDef), pNodeDef->pchNameFull);	
	}
#endif

	return pNode;
}

// Increases the rank of a PowerTree Node
// This DOES modify the Entity, but it is NOT an ATH.
NOCONST(PTNode) *entity_PowerTreeNodeIncreaseRankHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char *pchTree, const char *pchNode, S32 bSlave, S32 bIsTraining, S32 bSkipAutoBuy, PowerTreeSteps *pSteps)
{
	PowerTreeDef *pTreeDef;
	PTNodeDef *pNodeDef;
	NOCONST(PowerTree) *pTree;
	NOCONST(PTNode) *pNode;
	NOCONST(Power) *pPower = NULL;
	PTGroupDef *pGroupDef = NULL;
	PowerDef *pPowerDef = NULL;
	S32 i,c;
	int iRankNew;

	PERFINFO_AUTO_START_FUNC();

	pTreeDef = powertreedef_Find(pchTree);
	pNodeDef = powertreenodedef_Find(pchNode);

	if (!pTreeDef || !pNodeDef)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);

	if(!pTree)
	{
		//Character does not own tree, try to buy it
		if(!entity_PowerTreeModifyHelper(iPartitionIdx,pEnt,pPayer,pchTree,true,pSteps))
		{
			PERFINFO_AUTO_STOP();
			return NULL;
		}
		pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);
	}

	if (!pTree)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, &pTree, pchNode);

	//Check to see if the power tree has changed due to cloned nodes
	if(pNode)
		pNodeDef = GET_REF(pNode->hDef);

	for(i=eaSize(&pTreeDef->ppGroups)-1;i>=0;i--)
	{
		pGroupDef = pTreeDef->ppGroups[i];
		for(c=eaSize(&pGroupDef->ppNodes)-1;c>=0;c--)
		{
			if(pNodeDef == pGroupDef->ppNodes[c])
				break;
		}
		if(c>=0)
			break;
	}

	iRankNew = (pNode && !pNode->bEscrow) ? pNode->iRank + 1 : 0;

	if(pEnt && !entity_CanBuyPowerTreeNodeHelper(ATR_EMPTY_ARGS,iPartitionIdx,pPayer,pEnt,pGroupDef,pNodeDef,iRankNew,true,true,bSlave,bIsTraining))
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	pNode = entity_PowerTreeNodeAddHelper(ATR_EMPTY_ARGS,pPayer,pEnt,pTree,pNodeDef,NULL);
	if(!pNode)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	if(pSteps)
	{
		PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
		pStep->pchTree = pTreeDef->pchName;
		pStep->pchNode = pNodeDef->pchNameFull;
		eaPush(&pSteps->ppSteps,pStep);
	}

	// AutoBuy
	if(pEnt)
	{
		// Don't AutoBuy if this was an AutoBuy, to avoid excessive recursion.  Technically
		//  data could be created where AutoBuys are dependent on other AutoBuys, in which case
		//  this shortcut is wrong, but that case doesn't make much sense and should be avoided.
		// Also don't AutoBuy if this is a slave, since the master node will call AutoBuy itself
		//  once the slave helper is done.
		// Also (obviously) don't AutoBuy if we've been specifically told to skip it.  The ONLY
		//  time this should happen is on a direct call from the validation helper.
		if(!bSkipAutoBuy && !(pNodeDef->eFlag&kNodeFlag_AutoBuy) && !pNodeDef->bSlave)
		{
			PTNodeDef *pFailNodeDef = NULL;
			int iFailRank = -1;
			int iAutoBuy = entity_PowerTreeAutoBuyHelperEx(iPartitionIdx, pEnt, pPayer, pSteps, &pFailNodeDef, &iFailRank, NULL);
			if(iAutoBuy<0)
			{
				if(entIsServer() || pEnt->myContainerID!=0)
				{
					ErrorDetailsf("Failed to buy NodeDef '%s' at rank %d", 
						SAFE_MEMBER(pFailNodeDef, pchName), iFailRank);
					devassertmsg(iAutoBuy>=0,"AutoBuyHelper Failed");
				}
				PERFINFO_AUTO_STOP();
				return NULL;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return pNode;
}

// Auto buys any free PowerTree Nodes for the Entity.
// This DOES modify the Entity, but it is NOT an ATH.
int entity_PowerTreeAutoBuyHelperEx(int iPartitionIdx,NOCONST(Entity)* pEnt, NOCONST(Entity)* pPayer, PowerTreeSteps *pSteps, PTNodeDef **ppFailNodeDef, int *piFailRank, NOCONST(Entity) **ppFakeEnt)
{
	int i,c,j;
	int iReturn=0;
	DictionaryEArrayStruct *pDefs = resDictGetEArrayStruct(g_hPowerTreeDefDict);
	PowerTreeDef **ppDefs = (PowerTreeDef**)pDefs->ppReferents;
	NOCONST(Entity) *pEntCheck = entity_GetEntityToModify(pEnt,ppFakeEnt,false);

	PERFINFO_AUTO_START_FUNC();

	// Try to buy any AutoBuy Trees
	for(i=0;i<eaSize(&pDefs->ppReferents);i++)
	{
		if(ppDefs[i]->bAutoBuy && !entity_FindPowerTreeHelper(pEntCheck,ppDefs[i]))
		{
			if(entity_PowerTreeModifyHelperEx(iPartitionIdx,pEnt,pPayer,ppDefs[i]->pchName,true,pSteps,ppFakeEnt))
			{
				pEntCheck = entity_GetEntityToModify(pEnt,ppFakeEnt,false);
				iReturn++;
			}
		}
	}

	for(i=eaSize(&pEnt->pChar->ppPowerTrees)-1;i>=0;i--)
	{
		NOCONST(PowerTree) *pTree = pEntCheck->pChar->ppPowerTrees[i];
		PowerTreeDef *pTreeDef = GET_REF(pEntCheck->pChar->ppPowerTrees[i]->hDef);

		if (!pTreeDef)
			continue;

		for(j=eaSize(&pTreeDef->ppGroups)-1;j>=0;j--)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];
			for(c=eaSize(&pGroupDef->ppNodes)-1;c>=0;c--)
			{
				NOCONST(PTNode) *pNode = NULL;
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[c];
				
				//pEntCheck may have changed during the last iteration of this loop!
				// Need to update pTree to make sure we're looking in the right place.
				pTree = pEntCheck->pChar->ppPowerTrees[i];

				if(pNodeDef->bCloneSystem)
				{
					// We have to search across the entire Character (I guess?)
					pNode = character_FindPowerTreeNodeHelper(pEntCheck->pChar,NULL,pNodeDef->pchNameFull);
				}
				else
				{
					// We know it must be one of the nodes right in this tree
					int n;
					for(n=eaSize(&pTree->ppNodes)-1; n>=0; n--)
					{
						if(GET_REF(pTree->ppNodes[n]->hDef)==pNodeDef)
						{
							pNode = pTree->ppNodes[n];
							break;
						}
					}
				}

				ANALYSIS_ASSUME(pNodeDef);

				if(pNodeDef->eFlag & kNodeFlag_AutoBuy || (pNode && pNode->bEscrow))
				{
					int iRank = 0;
					
					if(pNode && pNodeDef->bCloneSystem)
					{
						// The helper may have found a "clone" of the pNodeDef we actually provided,
						//  so to handle that case, update the pNodeDef and pGroupDef based on the
						//  node we got back.
						char *pchgroupName;

						estrStackCreate(&pchgroupName);
						pNodeDef = GET_REF(pNode->hDef);

						powertree_GroupNameFromNodeDef(pNodeDef,&pchgroupName);

						pGroupDef = RefSystem_ReferentFromString(g_hPowerTreeGroupDefDict,pchgroupName);

						estrDestroy(&pchgroupName);
					}

					if(pNode && !pNode->bEscrow)
						iRank = pNode->iRank + 1;

					if(pNodeDef->ppRanks && iRank < eaSize(&pNodeDef->ppRanks) && (pNodeDef->ppRanks[iRank]->iCost==0 || pNodeDef->ppRanks[iRank]->bForcedAutoBuy))
					{
						PERFINFO_AUTO_START("CanBuyTestAndResult",1);
						if(entity_CanBuyPowerTreeNodeHelper(ATR_EMPTY_ARGS,iPartitionIdx,pPayer,pEntCheck,pGroupDef,pNodeDef,iRank,true,true,false,false))
						{
							pEntCheck = entity_GetEntityToModify(pEnt,ppFakeEnt,true);
							if(entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,pEntCheck,pPayer,pTreeDef->pchName,pNodeDef->pchNameFull,false,false,false,pSteps))
							{
								iReturn++;
								if(iRank+1 < eaSize(&pNodeDef->ppRanks) && pNodeDef->ppRanks[iRank+1]->iCost==0)
								{
									// Increment c so we try this node again, makes sure we get all the ranks we are allowed
									//  in one AutoBuy pass.
									c++;
								}
							}
							else
							{
								if (ppFailNodeDef)
									(*ppFailNodeDef) = pNodeDef;
								if (piFailRank)
									(*piFailRank) = iRank;
								PERFINFO_AUTO_STOP();
								PERFINFO_AUTO_STOP();
								return -1;
							}
						}
						PERFINFO_AUTO_STOP();
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return iReturn;
}

// Runs the entity_PowerTreeAutoBuyHelper, returns whether anything was purchased.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeAutoBuySteps(int iPartitionIdx, Entity *pEnt, Entity *pPayer, PowerTreeSteps *pSteps)
{
	bool bReturn = false;
	if(pEnt && pEnt->pChar)
	{
		NOCONST(Entity)* pFakeEnt = NULL;

		PERFINFO_AUTO_START_FUNC();

		if(entity_PowerTreeAutoBuyHelperEx(iPartitionIdx, CONTAINER_NOCONST(Entity, pEnt), NULL, pSteps, NULL, NULL, &pFakeEnt) > 0)
		{
			if(pSteps)
				pSteps->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;
			
			bReturn = true;

			// Final validation
			if(bReturn && !entity_PowerTreesValidate(iPartitionIdx,(Entity*)pFakeEnt,pPayer,NULL))
				bReturn = false;
		}

		StructDestroyNoConst(parse_Entity,pFakeEnt);

		PERFINFO_AUTO_STOP();
	}

	return bReturn;
}

// Runs the entity_PowerTreeNodeEnhanceHelper, returns whether it was successful.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeNodeEnhanceSteps(int iPartitionIdx,
									 Entity *pEnt,
									 const char *pchTree,
									 const char *pchNode,
									 const char *pchEnhancement,
									 PowerTreeSteps *pSteps)
{
	bool bReturn = false;
	if(pEnt && pEnt->pChar)
	{
		NOCONST(Entity)* pFakeEnt = StructCloneWithCommentDeConst(parse_Entity, pEnt, __FUNCTION__);

		if(entity_PowerTreeNodeEnhanceHelper(iPartitionIdx, pFakeEnt, pchTree, pchNode, pchEnhancement, true, pSteps))
		{
			if(pSteps)
				pSteps->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;

			bReturn = true;

			// Final validation
			if(bReturn && !entity_PowerTreesValidate(iPartitionIdx,(Entity*)pFakeEnt,NULL,NULL))
				bReturn = false;
		}

		StructDestroyNoConst(parse_Entity,pFakeEnt);
	}

	return bReturn;
}

// Runs the entity_PowerTreeNodeIncreaseRankHelper, returns whether it was successful.
//  If pSteps is not NULL it is filled in by the helper.
S32 entity_PowerTreeNodeIncreaseRankSteps(int iPartitionIdx,
										  Entity *pEnt,
										  const char *pchTree,
										  const char *pchNode,
										  PowerTreeSteps *pSteps)
{
	bool bReturn = false;
	if(pEnt && pEnt->pChar)
	{
		NOCONST(Entity)* pFakeEnt = StructCloneWithCommentDeConst(parse_Entity, pEnt, __FUNCTION__);

		if(entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx, pFakeEnt, NULL, pchTree, pchNode, false, false, false, pSteps))
		{
			if(pSteps)
				pSteps->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;

			bReturn = true;

			// Final validation, passes pPayer along, which is only used to copy some values off of
			if(bReturn && !entity_PowerTreesValidate(iPartitionIdx,(Entity*)pFakeEnt,NULL,NULL))
				bReturn = false;
		}

		StructDestroyNoConst(parse_Entity,pFakeEnt);
	}

	return bReturn;
}

// Checks if the Entity can buy the specified steps.  If pPayer is specified it is passed along.
//  If pStepsResult is not NULL it is filled in with the actual steps that need to be bought.
// The NoClone option is for when this is called from another function which is already
//  operating on fake Entities, which means this function doesn't clone the provided Entities
//  AND the side-effects of any changes hang around on the provided Entities.
S32 entity_PowerTreeStepsBuySteps(int iPartitionIdx,
								  Entity *pEnt,
								  Entity *pPayer,
								  PowerTreeSteps *pStepsRequested,
								  PowerTreeSteps *pStepsResult,
								  S32 bNoClone)
{
	bool bReturn = false;
	if(pEnt && pEnt->pChar && pStepsRequested)
	{
		int i, s = eaSize(&pStepsRequested->ppSteps);
		NOCONST(Entity)* pFakeEnt;
		NOCONST(Entity)* pFakePayer;
		S32 bIsTraining = pStepsRequested->bIsTraining;
		
		if(bNoClone)
		{
			pFakeEnt = CONTAINER_NOCONST(Entity, pEnt);
			pFakePayer = CONTAINER_NOCONST(Entity, pPayer);
		}
		else
		{
			pFakeEnt = StructCloneWithCommentDeConst(parse_Entity, pEnt, __FUNCTION__);
			pFakePayer = pPayer ? StructCloneWithCommentDeConst(parse_Entity, pPayer, __FUNCTION__) : NULL;
		}

		for(i=0; i<s; i++)
		{
			PowerTreeStep *pStep = pStepsRequested->ppSteps[i];

			// This is a little confusing, but because the default value for iRank on a PowerTreeStep is 0,
			//  a 0 could either mean you're attempting to set something to 0 (aka the first rank), or you're
			//  simply attempting to modify it by one rank.  Due to existing usage, this function assumes that
			//  the iRank field in the requested steps is specifying a set-to.  If that's NOT the way you're using
			//  it, you either need to change what you're providing, or you need add a flag to change this
			//  function's behavior such that it takes a bunch of steps that are all iRank 0 that simply mean
			//  a one-rank increase.

			// It's also important when making your steps for this function that the iRank on a Node is 0-based,
			//  and thus so is the iRank for a step for a Node, whereas the rank from the enhancement helper is
			//  1-based, and thus those step's iRanks expect to be 1-based.

			if(pStep->pchEnhancement)
			{
				NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(pFakeEnt->pChar,NULL,pStep->pchNode);
				PowerDef *pdefEnh = powerdef_Find(pStep->pchEnhancement);
				int iRankCurrent;

				if(!pNode || !pdefEnh)
					break;

				iRankCurrent = powertreenode_FindEnhancementRankHelper(pNode,pdefEnh);

				// You are NOT allowed to set the rank to something less or equal to what the rank already is!
				if(pStep->iRank <= iRankCurrent)
					break;

				while(pStep->iRank != iRankCurrent)
				{
					int iRankNew;

					// Attempt to increase the rank, fail on obvious failure
					if(!entity_PowerTreeNodeEnhanceHelper(iPartitionIdx,pFakeEnt,pStep->pchTree,pStep->pchNode,pStep->pchEnhancement,true,pStepsResult))
						break;

					// Get the new rank, fail if it didn't change
					iRankNew = powertreenode_FindEnhancementRankHelper(pNode,pdefEnh);
					if(iRankNew==iRankCurrent)
						break;

					iRankCurrent = iRankNew;
				}

				// Fail if we didn't get to where we wanted
				if(pStep->iRank != iRankCurrent)
					break;
			}
			else if(pStep->pchNode && pStep->bEscrow)
			{
				bool bEscrow = true;
				// Escrow is super-special (and more than a little dumb).  It tries to add the Node, and considers
				//  failure a success, and success a failure.  Assuming adding failed, it adds it anyway, but with
				//  the escrow flag enabled on the node.  Yeah really.
				NOCONST(PowerTree)* pTree = NULL;
				NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(pFakeEnt->pChar,NULL,pStep->pchNode);
				
				// Fail if we've already got the Node or the Step is malformed in any way
				if(pNode || pStep->iRank!=0)
					break;

				// Attempt to increase the rank, fail on SUCCESS (but it may make the PowerTree for us)
				if(entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,pFakeEnt,pFakePayer,pStep->pchTree,pStep->pchNode,false,bIsTraining,false,pStepsResult))
					bEscrow = false;

				// Get the PowerTree we're going to insert this into anyway, fail if it's missing
				pTree = eaIndexedGetUsingString(&pFakeEnt->pChar->ppPowerTrees,pStep->pchTree);
				if(!pTree)
					break;

				// Make the new escrow Node (which is basically empty, except for the escrow flag), and add it
				//  to the PowerTree
				pNode = CONTAINER_NOCONST(PTNode, powertreenode_create(powertreenodedef_Find(pStep->pchNode)));
				pNode->bEscrow = !!bEscrow;
				eaIndexedEnableNoConst(&pTree->ppNodes, parse_PTNode);
				eaPush(&pTree->ppNodes, pNode);

				// Push a clone of the Step into the results
				if(pStepsResult)
					eaPush(&pStepsResult->ppSteps,StructClone(parse_PowerTreeStep,pStep));
			}
			else if(pStep->pchNode)
			{
				NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(pFakeEnt->pChar,NULL,pStep->pchNode);
				int iRankCurrent = pNode && !pNode->bEscrow ? pNode->iRank : -1;
				
				// You are NOT allowed to set the rank to something less or equal to what the rank already is!
				if(pStep->iRank <= iRankCurrent)
					break;

				while(pStep->iRank != iRankCurrent)
				{
					int iRankNew;

					// Attempt to increase the rank, fail on obvious failure
					if(!entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,pFakeEnt,pFakePayer,pStep->pchTree,pStep->pchNode,false,bIsTraining,false,pStepsResult))
						break;

					if(!pNode)
						pNode = character_FindPowerTreeNodeHelper(pFakeEnt->pChar,NULL,pStep->pchNode);

					// Get the new rank, fail if it didn't change
					iRankNew = pNode && !pNode->bEscrow ? pNode->iRank : -1;
					if(iRankNew==iRankCurrent)
						break;
					
					iRankCurrent = iRankNew;
				}

				// Fail if we didn't get to where we wanted
				if(pStep->iRank != iRankCurrent)
					break;
			}
			else if(pStep->pchTree)
			{
				if(!entity_PowerTreeModifyHelper(iPartitionIdx,pFakeEnt,pFakePayer,pStep->pchTree,true,pStepsResult))
					break;
			}
		}

		if(i==s)
		{
			if(pStepsResult)
				pStepsResult->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;

			bReturn = true;

			// Final validation, passes pPayer along, which is only used to copy some values off of
			if(bReturn && !entity_PowerTreesValidate(iPartitionIdx,(Entity*)pFakeEnt,(Entity*)pFakePayer,NULL))
				bReturn = false;
		}

		if(!bNoClone)
		{
			StructDestroyNoConst(parse_Entity,pFakeEnt);
			if(pFakePayer)
				StructDestroyNoConst(parse_Entity,pFakePayer);
		}
	}

	return bReturn;
}



// Checks if the Entity can respec the specified Steps, followed by optional purchases of the specified Steps.
//  If the results Steps are not NULL they are filled in with the actual Steps that need to be respec'd and added.
// This uses pPayer ONLY for validation, because it's always been that way, and changing it could have unintended
//  side-effects
// The NoClone option is for when this is called from another function which is already
//  operating on fake Entities, which means this function doesn't clone the provided Entities
//  AND the side-effects of any changes hang around on the provided Entities.
// TODO See if we can avoid StructCopy of pPayer
S32 entity_PowerTreeStepsRespecSteps(int iPartitionIdx,
									 Entity *pEnt,
									 Entity *pPayer,
									 PowerTreeSteps *pStepsRequestedRespec,
									 PowerTreeSteps *pStepsRequestedBuy,
									 PowerTreeSteps *pStepsResultRespec,
									 PowerTreeSteps *pStepsResultBuy,
									 S32 bNoClone,
									 S32 bStepRespec)
{
	bool bReturn = false;
	if(pEnt && pEnt->pChar && pStepsRequestedRespec)
	{
		int i, s = eaSize(&pStepsRequestedRespec->ppSteps);
		NOCONST(Entity)* pFakeEnt;
		NOCONST(Entity)* pFakePayer;
		GameAccountData *pData = entity_GetGameAccount(pEnt);
		NOCONST(GameAccountData)* pFakeData = StructCloneDeConst(parse_GameAccountData, pData);
		S32 bUpdatePointSpent = false;

		// Variables for doing the numeric item/bag lookup.
		ItemDef *pCostNumeric = GET_REF(g_PowerTreeRespecConfig.hNumeric);

		if(bNoClone)
		{
			pFakeEnt = CONTAINER_NOCONST(Entity, pEnt);
			pFakePayer = CONTAINER_NOCONST(Entity, pPayer);
		}
		else
		{
			pFakeEnt = StructCloneWithCommentDeConst(parse_Entity, pEnt, __FUNCTION__);
			pFakePayer = pPayer ? StructCloneWithCommentDeConst(parse_Entity, pPayer, __FUNCTION__) : NULL;
		}

		for(i=0; i<s; i++)
		{
			PowerTreeStep *pStep = pStepsRequestedRespec->ppSteps[i];

			PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef",pStep->pchTree);

			if(pTreeDef && pTreeDef->eRespec == kPowerTreeRespec_None)
				continue;

			// If this has a cost, try to pay it
			if(pStep->iCostRespec > 0)
			{
#ifdef GAMESERVER
				//Try to use the free respec token, before using the numeric
				if (trhCharacter_UseForceRespecFromSchedule(pFakeEnt))
				{
					//Yay, its free for you!
				}
				else if(g_PowerTreeRespecConfig.bForceUseFreeRespec
					&& (trhCharacter_UseFreeRespecFromSchedule(pFakeEnt) 
					|| slGAD_trh_ChangeAttribClamped_Force(ATR_EMPTY_ARGS,pFakeData, MicroTrans_GetRespecTokensGADKey(), -1, 0, 100000)
					|| inv_ent_trh_AddNumeric(ATR_EMPTY_ARGS,pFakeEnt,true,MicroTrans_GetRespecTokensKeyID(),-1,NULL)))
				{
					//Yay, its free for you!
				}
				else if(pCostNumeric && !inv_ent_trh_AddNumeric(ATR_EMPTY_ARGS, pFakeEnt, true, pCostNumeric->pchName, -pStep->iCostRespec, NULL))
				{
					break;
				}
				bUpdatePointSpent = true;
#endif
			}
			else if(pStep->iCostRespec < 0)
			{
				// Negative cost means disallowed - shouldn't get this far, but check it anyway
				break;
			}

			// Yes, these passes NULL for the pPayer, because it's always been that way, and changing it could
			//  have unintended side-effects.  This also works different than entity_PowerTreeStepsBuySteps(),
			//  because existing usage is that the requested steps are already in one-per-adjustment format.

			if(pStep->pchEnhancement)
			{
				if(!entity_PowerTreeNodeEnhanceHelper(iPartitionIdx,pFakeEnt,pStep->pchTree,pStep->pchNode,pStep->pchEnhancement,false,pStepsResultRespec))
					break;
			}
			else if(pStep->pchNode)
			{
				if(!entity_PowerTreeNodeDecreaseRankHelper(pFakeEnt,pStep->pchTree,pStep->pchNode,false,pStep->bEscrow,pStepsResultRespec))
					break;
				
			}
			else if(pStep->pchTree)
			{
				// Attempts to respec out of a PowerTree that isn't set to Remove are simply ignored.
				// This isn't treated as failure because this function is sometimes provided a straight
				// copy of the Steps on an existing Character, rather than a filtered list of specific
				// Steps to take.
				if(!(pTreeDef && pTreeDef->eRespec == kPowerTreeRespec_Remove))
					continue;

				if(!entity_PowerTreeModifyHelper(iPartitionIdx,pFakeEnt,NULL,pStep->pchTree,false,pStepsResultRespec))
					break;
			}

			// Save the Step's cost to the last Step in the list (regardless of if we thought it would be free)
			if(pStepsResultRespec && pStep->iCostRespec > 0)
			{
				int iLast = eaSize(&pStepsResultRespec->ppSteps);
				if(iLast > 0)
				{
					// Use += to accumulate cost, just in case something weird happens we don't want to overwrite
					pStepsResultRespec->ppSteps[iLast-1]->iCostRespec += pStep->iCostRespec;
				}
			}
		}

		if(i==s)
		{
			if(pStepsResultRespec)
			{
				pStepsResultRespec->uiPowerTreeModCount = pEnt->pChar->uiPowerTreeModCount;
				pStepsResultRespec->bUpdatePointsSpent = pStepsRequestedRespec->bUpdatePointsSpent;
			}

			bReturn = true;
		}

		// Remove all AutoBuy Nodes, as they may not be valid anymore, appended to the end of the respec Steps
		if(bReturn)
		{
			for(i=eaSize(&pFakeEnt->pChar->ppPowerTrees)-1 ; i>=0; i--)
			{
				NOCONST(PowerTree) *pTree = pFakeEnt->pChar->ppPowerTrees[i];
				PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);
				PTTypeDef *pTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;
				bool bSkipIfStepRespec = pTypeDef ? pTypeDef->bOnlyRespecUsingFull : false;
				int n;

				if(pTreeDef->eRespec == kPowerTreeRespec_None)
					continue;

				for(n=eaSize(&pTree->ppNodes)-1; n>=0; n--)
				{
					NOCONST(PTNode) *pNode = pTree->ppNodes[n];
					PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

					if(pNodeDef && pNodeDef->eFlag & kNodeFlag_AutoBuy)
					{
						int iRank = pNode->iRank;

						while(iRank>=0)
						{
							int iCost = entity_PowerTreeNodeRankCostHelper(pFakeEnt->pChar, pNodeDef, iRank);
							if(iCost > 0)
								break;

							if(!entity_PowerTreeNodeDecreaseRankHelper(pFakeEnt,pTreeDef->pchName,pNodeDef->pchNameFull,false,false,pStepsResultRespec))
							{
								// We shouldn't have any problems here, but in case we do, fail completely
								bReturn = false;
								break;
							}

							iRank--;
						}
					}
				}

				// Remove auto-bought trees that have the remove respec type as well as nodes.
				// According to the comments on the PowerTreeRespec enum, the only power trees
				// we should be able to remove by respeccing should have the Remove respec type
				if(pTreeDef->bAutoBuy && pTreeDef->eRespec == kPowerTreeRespec_Remove && (!bStepRespec || !bSkipIfStepRespec))
				{
					if(!entity_PowerTreeModifyHelperEx(0, pFakeEnt, pFakePayer, pTreeDef->pchName, 0, pStepsResultRespec, NULL))
					{
						// We shouldn't have any problems here, but in case we do, fail completely
						bReturn = false;
						break;
					}
				}
			}
		}

		// Buy anything requested
		if(bReturn && pStepsRequestedBuy)
			bReturn = entity_PowerTreeStepsBuySteps(iPartitionIdx,(Entity*)pFakeEnt,(Entity*)pFakePayer,pStepsRequestedBuy,pStepsResultBuy,true);

		// Re-AutoBuy everything
		if(bReturn)
			entity_PowerTreeAutoBuyHelper(iPartitionIdx,pFakeEnt,NULL,pStepsResultBuy);

		// Ugly custom test for STO to force the result to have spent a certain number of the Skillpoint numeric
		if(bReturn && entity_PointsSpent(pFakeEnt,"Skillpoint") < pStepsRequestedRespec->iRespecSkillpointSpentMin)
			bReturn = false;

		// Final validation, passes pPayer along, which is only used to copy some values off of
		if(bReturn && !entity_PowerTreesValidate(iPartitionIdx,(Entity*)pFakeEnt,(Entity*)pFakePayer,NULL))
			bReturn = false;

		StructDestroyNoConst(parse_GameAccountData,pFakeData);
		if(!bNoClone)
		{
			StructDestroyNoConst(parse_Entity,pFakeEnt);
			if(pFakePayer)
				StructDestroyNoConst(parse_Entity,pFakePayer);
		}
	}

	return bReturn;
}



// STO-specific function to check if the Entity can replace a Node with another Node to be escrow'd.
//  If the results Steps are not NULL they are filled in with the actual Steps that need to be respec'd and added.
S32 entity_PowerTreeNodeReplaceEscrowSteps(int iPartitionIdx,
										   Entity *pEnt,
										   Entity *pPayer,
										   const char *pchOldTree,
										   const char *pchOldNode,
										   const char *pchNewTree,
										   const char *pchNewNode,
										   PowerTreeSteps *pStepsResultRespec,
										   PowerTreeSteps *pStepsResultBuy)
{
	PowerTreeSteps *pStepsRespec = StructCreate(parse_PowerTreeSteps);
	PowerTreeSteps *pStepsBuy = StructCreate(parse_PowerTreeSteps);
	PowerTreeStep *pStep;
	S32 bEscrow = false;
	
	S32 bSuccess = entity_CanReplacePowerTreeNodeInEscrow(iPartitionIdx,CONTAINER_NOCONST(Entity, pPayer),CONTAINER_NOCONST(Entity, pEnt),pchOldTree,pchOldNode,pchNewTree,pchNewNode,true);

	// Build a set of respec Steps for removing the old Node
	if(bSuccess)
	{
		NOCONST(PowerTree)* pTree = NULL;
		NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pEnt)->pChar, &pTree, pchOldNode);
		if(!pNode)
		{
			bSuccess = false;
		}
		else if(pNode->bEscrow)
		{
			// This very specifically is allowed to remove a Node that is in escrow, which is not generally legal
			pStep = StructCreate(parse_PowerTreeStep);
			pStep->pchTree = pchOldTree;
			pStep->pchNode = pchOldNode;
			pStep->bEscrow = true;
			eaPush(&pStepsRespec->ppSteps,pStep);
		}
		else
		{
			int iRank = pNode->iRank;
			for(; iRank >= 0; iRank--)
			{
				pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pchOldTree;
				pStep->pchNode = pchOldNode;
				eaPush(&pStepsRespec->ppSteps,pStep);
			}
		}
		bEscrow = true; // The replacement Node must also be placed in escrow
	}

	// Build a set of buy Steps for adding the new Node
	if(bSuccess)
	{
		NOCONST(PowerTree)* pTree = NULL;
		NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pEnt)->pChar, &pTree, pchNewNode);
		if(pNode)
		{
			bSuccess = false;
		}
		else
		{
			// Make the Tree if it's not already owned
			if(!pTree && pchOldTree!=pchNewTree)
			{
				pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pchNewTree;
				eaPush(&pStepsBuy->ppSteps,pStep);
			}

			pStep = StructCreate(parse_PowerTreeStep);
			pStep->pchTree = pchNewTree;
			pStep->pchNode = pchNewNode;
			pStep->bEscrow = !!bEscrow;
			eaPush(&pStepsBuy->ppSteps,pStep);
		}
	}

	if(bSuccess)
	{
		bSuccess = entity_PowerTreeStepsRespecSteps(iPartitionIdx,pEnt,pPayer,pStepsRespec,pStepsBuy,pStepsResultRespec,pStepsResultBuy,false, false);
	}

	StructDestroy(parse_PowerTreeSteps,pStepsRespec);
	StructDestroy(parse_PowerTreeSteps,pStepsBuy);

	return bSuccess;
}




AUTO_TRANS_HELPER;
int PowerTreeNodeDelete(ATH_ARG NOCONST(PowerTree) *pTree, NOCONST(PTNode) *pNode)
{
	int index = eaFindAndRemove(&pTree->ppNodes,pNode);
	if(verify(index>=0))
	{
		int i;
		for(i=eaSize(&pNode->ppPowers)-1; i>=0; i--)
		{
			StructDestroyNoConstSafe(parse_Power,&pNode->ppPowers[i]);
			eaRemove(&pNode->ppPowers,i);
		}
		StructDestroyNoConstSafe(parse_PTNode, &pNode);
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
S32 entity_PowerTreeNodeSubHelper(ATR_ARGS, ATH_ARG NOCONST(Entity) *pEnt, ATH_ARG NOCONST(Entity) *pPayer, ATH_ARG NOCONST(PowerTree) *pTree, ATH_ARG NOCONST(PTNode) *pNode, S32 bAlwaysRefundPoints, S32 bEscrow, const ItemChangeReason *pReason)
{
	PowerTreeDef *pTreeDef;
	PTTypeDef *pTypeDef;
	PTNodeDef *pNodeDef;
	S32 iRank;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pTree) || ISNULL(pNode))
		return false;

	// You're only allowed to decrease the rank of something in escrow if you 
	//  explicitly request it.  You should KNOW if you need to request it or not,
	//  you should NOT just be passing in the value of pNode->bEscrow.
	if(pNode->bEscrow != !!bEscrow)
		return false;

	iRank = pNode->iRank;
	pNodeDef = GET_REF(pNode->hDef);

	pTreeDef = GET_REF(pTree->hDef);
	pTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;

	// Refund the "spent" numeric
	if(pTypeDef
		&& pTypeDef->pchSpentPointsNumeric
		&& *pTypeDef->pchSpentPointsNumeric
		&& (bAlwaysRefundPoints || !pTypeDef->bSpentPointsNonDynamic))
	{
		S32 iCost = entity_PowerTreeNodeRankCostHelper(pEnt->pChar, pNodeDef, iRank);
		if(iCost > 0)
		{
			const char* pchNumeric = pTypeDef->pchSpentPointsNumeric;

			inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,true,pchNumeric,-iCost,pReason);

			if(NONNULL(pPayer))
			{
				inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pPayer,true,pchNumeric,-iCost,pReason);
			}
		}
	}

	if(iRank==0)
	{
		// Decreasing a rank 0 node means delete it
		if(!PowerTreeNodeDelete(pTree,pNode))
		{
			return false;
		}
	}
	else
	{
		// Delete the last power in the list if that was the power this rank granted
		if(pNodeDef && !pNodeDef->ppRanks[iRank]->bEmpty)
		{
			int i;
			PowerDef *pPowerDef = GET_REF(pNodeDef->ppRanks[iRank]->hPowerDef);
			
			if(!pPowerDef)
				return false;

			for(i=eaSize(&pNode->ppPowers)-1; i>=0; i--)
			{
				if(pPowerDef==GET_REF(pNode->ppPowers[i]->hDef))
					break;
			}

			if(i<0)
				return false;

			// TODO(MM): Should we alert/error here if the power being removed isn't the last one in the list?

			// If we need to copy any data to the previous power, do that now
			if(i>0)
			{
				NOCONST(Power) *ppowPrev = pNode->ppPowers[i-1];
				if(ppowPrev)
				{
					power_SetHueHelper(ppowPrev,pNode->ppPowers[i]->fHue);
					power_SetEmitHelper(ppowPrev,REF_STRING_FROM_HANDLE(pNode->ppPowers[i]->hEmit));
					power_SetEntCreateCostumeHelper(ppowPrev,pNode->ppPowers[i]->iEntCreateCostume);
				}
			}

			StructDestroyNoConstSafe(parse_Power,&pNode->ppPowers[i]);
			eaRemove(&pNode->ppPowers,i);
		}

		// Actually remove a purchase tracker and rank
		StructDestroy(parse_PowerPurchaseTracker,eaPop(&pNode->ppPurchaseTracker));
		pNode->iRank--;
	}

	// Update any slaves
	if(pNodeDef && pNodeDef->eFlag&kNodeFlag_MasterNode)
	{
		S32 bSlaveSuccess = entity_PowerTreesSlaveHelper(ATR_PASS_ARGS,pEnt,pPayer,pReason);
		if(!bSlaveSuccess)
		{
			if(entIsServer() || pEnt->myContainerID!=0)
				devassertmsg(bSlaveSuccess,"PowerTreesSlaveHelper Failed");
			return false;
		}
	}

	return true;
}

// Decreases the rank of a PowerTree Node
// This DOES modify the Entity, but it is NOT an ATH.
S32 entity_PowerTreeNodeDecreaseRankHelper(NOCONST(Entity) *pEnt, const char *pchTree, const char *pchNode, bool bAlwaysRefundPoints, S32 bEscrow, PowerTreeSteps *pSteps)
{
	NOCONST(PowerTree) *pTree = NULL;
	NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, &pTree, pchNode);
	PowerTreeDef *pTreeDef = pTree ? GET_REF(pTree->hDef) : NULL;
	PTNodeDef *pNodeDef = pNode ? GET_REF(pNode->hDef) : NULL;

	// You're only allowed to decrease the rank of something in escrow if you 
	//  explicitly request it.  You should KNOW if you need to request it or not,
	//  you should NOT just be passing in the value of pNode->bEscrow.
	S32 bSuccess = entity_PowerTreeNodeSubHelper(ATR_EMPTY_ARGS,pEnt,NULL,pTree,pNode,bAlwaysRefundPoints,bEscrow,NULL);

	if(bSuccess && pSteps)
	{
		PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
		pStep->pchTree = pTreeDef->pchName;
		pStep->pchNode = pNodeDef->pchNameFull;
		pStep->bEscrow = bEscrow;
		eaPush(&pSteps->ppSteps,pStep);
	}
	else if (!bSuccess)
	{
		DBGPOWERTREE_printf("Failed PowerTreeNodeDecreaseRank: Tree(%s) Node(%s)\n", pchTree, pchNode);
	}

	return bSuccess;
}

AUTO_TRANS_HELPER;
S32 entity_FindTrainingLevelHelper(ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt)
{
	return entity_Find_TableTrainingLevel(pBuyer, pEnt, "TreePoints");
}

AUTO_TRANS_HELPER;
S32 entity_FindEnhancementLevelHelper(ATH_ARG NOCONST(Entity)* pEnt)
{
	return entity_Find_TableTrainingLevel(NULL, pEnt, "EnhPoints");
}

AUTO_TRANS_HELPER;
S32 entity_Find_TableTrainingLevel(ATH_ARG NOCONST(Entity)* pBuyer, ATH_ARG NOCONST(Entity)* pEnt, const char *pchTableName)
{
	int iReturn =-1;
	int iExpLevel;
	S32 bPointsAreNumeric = false;
	PowerTable *ptable;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("entity_trh_GetExpLevelOfCorrectBuyer",1);
	iExpLevel = entity_trh_GetExpLevelOfCorrectBuyer(pBuyer,pEnt);
	PERFINFO_AUTO_STOP();

	ptable = powertable_Find(pchTableName);
	if(!ptable)
	{
		char *pchActualTableName = estrStackCreateFromStr(pchTableName);
		estrAppend2(&pchActualTableName, "_NumericItem_LevelTable");
		ptable = powertable_Find(pchActualTableName);
		estrDestroy(&pchActualTableName);
		bPointsAreNumeric = true;
	}

	if(ptable)
	{
		S32 i = 0, s = eafSize(&ptable->pfValues), iPoints;

		if (ptable->pchNumericOverride)
			iPoints = ptable->bUsePointsEarned ? entity_PointsEarned(pEnt, ptable->pchNumericOverride) : entity_PointsSpent(pEnt, ptable->pchNumericOverride);
		else
			iPoints = ptable->bUsePointsEarned ? entity_PointsEarned(pEnt, pchTableName) : entity_PointsSpent(pEnt, pchTableName);

		while(i < s && ptable->pfValues[i] <= iPoints)
			i++;

		//STO style skill points
		if(bPointsAreNumeric)
		{
			iReturn = MAX(i-1, 0);
		}
		//Champions style, power points, enhancement points etc
		else
		{
			iReturn = i;
		}
	}

	if(iReturn > iExpLevel)
		iReturn = iExpLevel-1;

	PERFINFO_AUTO_STOP();

	return iReturn;
}

// Returns true if the Entity can buy the PowerTreeDef.  Must set bTemporary to true when testing for
//  Temporary trees, or false when testing for permanent purchases.
AUTO_TRANS_HELPER;
S32 entity_CanBuyPowerTreeHelper(int iPartitionIdx, ATH_ARG NOCONST(Entity)* pEnt, PowerTreeDef *pTree, S32 bTemporary)
{
	PTTypeDef *pTreeType = GET_REF(pTree->hTreeType);
	MultiVal mVal;
	int rtn=0;

	if(pTree->bTemporary!=bTemporary)
		return false;

	//NOTE: If this is getting called from "entity_PowerTreeValidateHelper", then pEnt is a fake entity.
	//Make sure that you are copying over all fields onto the fake entity that are pertinent to buying trees.

	if(pTreeType && pTreeType->pExprPurchase)
	{
		ExprContext *pContext = powertreetypes_GetContext();
		exprContextSetPointerVar(pContext,"Source",(Entity*)pEnt,parse_Entity, true, true);
		exprContextSetPointerVar(pContext, "GameAccount", entity_trh_GetGameAccount(pEnt), parse_GameAccountData, false, true);
		exprContextSetPointerVar(pContext, "PowerTree", pTree, parse_PowerTreeDef, false, true);
		exprContextSetPartition(pContext, iPartitionIdx);

		exprEvaluateTolerateInvalidUsage(pTreeType->pExprPurchase,pContext,&mVal);
	}
	else
		rtn=1;

	if(rtn==1 || MultiValGetInt(&mVal,NULL) == 1)
	{
		if(pTree->pExprRequires)
		{
			ExprContext *pContext = powertree_GetContext();
			exprContextSetPointerVar(pContext,"Source",(Entity*)pEnt,parse_Entity, true, true);
			exprContextSetPointerVar(pContext, "GameAccount", entity_trh_GetGameAccount(pEnt), parse_GameAccountData, false, true);
			exprContextSetUserPtr(pContext, pTree, parse_PowerTreeDef);
			exprContextSetPartition(pContext, iPartitionIdx);

			exprEvaluateTolerateInvalidUsage(pTree->pExprRequires,pContext,&mVal);
			exprContextSetUserPtr(pContext, NULL, NULL);
			rtn=0;
		}
		else
			rtn=1;
	}

	if(rtn==0)
		rtn = MultiValGetInt(&mVal,NULL);
	if(rtn == 1)
	{
		// If a tree has a class, it must be a player class to be bought.
		CharacterClass *pClass = GET_REF(pTree->hClass);
		if (pClass)
			return pClass->bPlayerClass;
		else if(IS_HANDLE_ACTIVE(pTree->hClass))
			return false;
		else
			return true;
	}

	return false;
}

// Adds the PowerTree from the given PowerTreeDef
//  Only validates that the Entity doesn't already have the tree
AUTO_TRANS_HELPER;
S32 entity_PowerTreeAddHelper(ATH_ARG NOCONST(Entity)* pEnt, PowerTreeDef *pTreeDef)
{
	NOCONST(PowerTree)* pTree;
	
	if(ISNULL(pEnt)
		|| ISNULL(pEnt->pChar)
		|| !pTreeDef
		|| eaIndexedFindUsingString(&pEnt->pChar->ppPowerTrees, pTreeDef->pchName) >= 0)
	{
		return false;
	}

	pTree = powertree_Create(pTreeDef);
	eaIndexedEnableNoConst(&pEnt->pChar->ppPowerTrees, parse_PowerTree);
	eaPush(&pEnt->pChar->ppPowerTrees, pTree);

	return true;
}

// Removes the PowerTree with the given PowerTreeDef from the Entity
// If the PowerTree is flagged as NonRefundable, this doesn't actually
//  remove the PowerTree, it just destroys all its Nodes
AUTO_TRANS_HELPER;
S32 entity_PowerTreeRemoveHelper(ATH_ARG NOCONST(Entity)* pEnt, PowerTreeDef *pTreeDef)
{
	int i;
	PTTypeDef *pTypeDef;

	if(ISNULL(pEnt)
		|| ISNULL(pEnt->pChar)
		|| !pTreeDef
		|| (i = eaIndexedFindUsingString(&pEnt->pChar->ppPowerTrees, pTreeDef->pchName)) < 0)
	{
		return false;
	}

	pTypeDef = GET_REF(pTreeDef->hTreeType);

	if(!pTypeDef || !pTypeDef->bNonRefundable)
	{
		// Remove and destroy the Tree
		StructDestroyNoConstSafe(parse_PowerTree,&pEnt->pChar->ppPowerTrees[i]);
		eaRemove(&pEnt->pChar->ppPowerTrees,i);
	}
	else
	{
		// Just destroy all the Nodes
		eaDestroyStructNoConst(&pEnt->pChar->ppPowerTrees[i]->ppNodes,parse_PTNode);
	}

	return true;
}

// Attempts to add or remove a PowerTree from the Entity.
// This DOES modify the Entity, but it is NOT an ATH.
int entity_PowerTreeModifyHelperEx(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char *pchTree, int bAdd, PowerTreeSteps *pSteps, NOCONST(Entity) **ppFakeEnt)
{
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
	S32 bReturn = false;

	PERFINFO_AUTO_START_FUNC();

	if(bAdd)
	{
		if(entity_CanBuyPowerTreeHelper(iPartitionIdx,entity_GetEntityToModify(pEnt,ppFakeEnt,false),pTreeDef,false))
		{
			if(entity_PowerTreeAddHelper(entity_GetEntityToModify(pEnt,ppFakeEnt,true),pTreeDef))
			{
				if(pSteps)
				{
					PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pTreeDef->pchName;
					eaPush(&pSteps->ppSteps,pStep);
				}

				// Run AutoBuy immediately after each Tree purchase to keep things consistent
				entity_PowerTreeAutoBuyHelperEx(iPartitionIdx,pEnt,pPayer,pSteps,NULL,NULL,ppFakeEnt);
				bReturn = true;
			}
		}
	}
	else
	{
		if(entity_PowerTreeRemoveHelper(entity_GetEntityToModify(pEnt,ppFakeEnt,true),pTreeDef))
		{
			if(pSteps)
			{
				PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pTreeDef->pchName;
				eaPush(&pSteps->ppSteps,pStep);
			}
			bReturn = true;
		}
	}

	PERFINFO_AUTO_STOP();

	return bReturn;
}

// Fixes the uiTimeCreated field on the PowerTree with a best guess, if it's not set
AUTO_TRANS_HELPER;
void powertree_FixTimeCreatedHelper(ATH_ARG NOCONST(PowerTree) *pTree)
{
	int i,j;
	if(!pTree->uiTimeCreated)
	{
		U32 uiTimeCreated = 0;
		for(i=eaSize(&pTree->ppNodes)-1; i>=0; i--)
		{
			for(j=eaSize(&pTree->ppNodes[i]->ppPowers)-1; j>=0; j--)
			{
				if(pTree->ppNodes[i]->ppPowers[j]->uiTimeCreated
					&& (!uiTimeCreated || pTree->ppNodes[i]->ppPowers[j]->uiTimeCreated < uiTimeCreated))
				{
					uiTimeCreated = pTree->ppNodes[i]->ppPowers[j]->uiTimeCreated;
				}
			}
		}

		if(uiTimeCreated)
		{
			pTree->uiTimeCreated = uiTimeCreated;
		}
	}
}


AUTO_TRANS_HELPER;
int entity_PowerTreeNodeEnhPointsSpentHelper(ATH_ARG NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef)
{
	int iSpent = 0;

	// Find the node, copied from code below - seems like there should be a helper for this
	int i;
	PTNode *pNode = NULL;
	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes);n++)
		{
			if(GET_REF(pEnt->pChar->ppPowerTrees[i]->ppNodes[n]->hDef) == pNodeDef)
			{
				pNode = (PTNode*)pEnt->pChar->ppPowerTrees[i]->ppNodes[n];
				break;
			}
		}

		if(pNode)
			break;
	}

	if(pNode && !pNode->bEscrow)
	{
		for(i=0;i<eaSize(&pNode->ppPowers);i++)
		{
			int j;
			for(j=0;j<eaSize(&pNodeDef->ppRanks);j++)
			{
				if(GET_REF(pNodeDef->ppRanks[j]->hPowerDef) == GET_REF(pNode->ppPowers[i]->hDef)
					&& RankIsEnhancementHelper(pNodeDef->ppRanks[j]))
				{
					iSpent +=  pNodeDef->ppRanks[j]->iCost;
				}
			}
		}
		for(i=0;i<eaSize(&pNode->ppEnhancementTrackers);i++)
		{
			int j;
			for(j=0;j<eaSize(&pNodeDef->ppEnhancements);j++)
			{
				if(GET_REF(pNode->ppEnhancementTrackers[i]->hDef)==GET_REF(pNodeDef->ppEnhancements[j]->hPowerDef))
				{
					int iLevel = eaSize(&pNode->ppEnhancementTrackers[i]->ppPurchases);
					iSpent += (iLevel * pNodeDef->ppEnhancements[j]->iCost);
					break;
				}
			}
		}
	}

	return iSpent;
}

bool entity_CanBuyPowerTreeEnhHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, PTNodeDef *pNodeDef, PTNodeEnhancementDef *pEnh)
{
	int i;
	PTNode *pNode = NULL;
	PowerTreeDef *pTreeDef = NULL;

	//Does the character own this node already?
	for(i=0;i<eaSize(&pEnt->pChar->ppPowerTrees);i++)
	{
		int n;

		for(n=0;n<eaSize(&pEnt->pChar->ppPowerTrees[i]->ppNodes);n++)
		{
			if(GET_REF(pEnt->pChar->ppPowerTrees[i]->ppNodes[n]->hDef) == pNodeDef)
			{
				pNode = (PTNode*)pEnt->pChar->ppPowerTrees[i]->ppNodes[n];
				pTreeDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);
				break;
			}
		}

		if(pNode)
			break;
	}

	if(!pNode || pNode->bEscrow)
	{
		DBGPOWERTREE_printf("Main node must be purchased before enhancements\n");
		return false;
	}

	//Are there any levels to upgrade to?
	for(i=0;i<eaSize(&pNode->ppEnhancementTrackers);i++)
	{
		if(GET_REF(pNode->ppEnhancementTrackers[i]->hDef) == GET_REF(pEnh->hPowerDef))
		{
			if(eaSize(&pNode->ppEnhancementTrackers[i]->ppPurchases) >= pEnh->iLevelMax)
			{
				DBGPOWERTREE_printf("Enhancement at max level\n");
				return false;
			}
		}
	}

	// Everything is unrestricted and costs 0 if purchase rules are disabled
	if(s_bPowerTreesDisablePurchaseRules)
		return true;

	if(IS_HANDLE_ACTIVE(pEnh->hEnhType))
	{
		PTEnhTypeDef *pTypeDef = GET_REF(pEnh->hEnhType);
		if(pTypeDef && pTypeDef->pExpr)
		{
			ExprContext *pContext = powertreeenhtypes_GetContext();
			MultiVal mVal;

			exprContextSetPointerVar(pContext,"CurNode",pNode,parse_PTNode, false, false);
			exprContextSetPartition(pContext, iPartitionIdx);

			exprEvaluateTolerateInvalidUsage(pTypeDef->pExpr,pContext,&mVal);
			if(MultiValGetInt(&mVal,NULL) == 0)
			{
				DBGPOWERTREE_printf("Enhancement purchased failed expression statement\n");
				return false;
			}
		}
	}

	//Do you have enough points to spend?
	if(entity_PointsRemaining(NULL,pEnt,pTreeDef,pEnh->pchCostTable) < pEnh->iCost)
	{
		DBGPOWERTREE_printf("Character does not have enough enhancement points to spend\n");
		return false;
	}

	// If this costs points, and there is a limit on the points you can spend
	if(pEnh->iCost > 0 && pNodeDef->iCostMaxEnhancement > 0)
	{
		S32 iSpent = entity_PowerTreeNodeEnhPointsSpentHelper(pEnt,pNodeDef);
		if(iSpent + pEnh->iCost > pNodeDef->iCostMaxEnhancement)
		{
			DBGPOWERTREE_printf("Character is not allowed to spend that many enhancement points on that node\n");
			return false;
		}
	}

	return true;
}

static S32 CompareU32(const U32 *i, const U32 *j)
{
	return (*i < *j) ? -1 : (*i > *j);
}


// Returns the type order for the PowerTreeDef for this particular Character.  If the Character
//  doesn't own the Tree yet, it assumes it would be the next one purchased.  0-based.  Returns -1 on an error.
// The value of this could probably be cached, but if we want to access it in a transaction either the cached value
//  would have to persist/transact, or we have to figure it on the fly.  And then we have to make sure the cache is
//  fixed when trees are refunded, etc.  So for now, no caching.
AUTO_TRANS_HELPER;
int character_PowerTreeTypeOrderHelper(ATH_ARG NOCONST(Character)* pchar,
									   SA_PARAM_OP_VALID PowerTreeDef *pTreeDef)
{
	PTTypeDef *pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;
	static U32 *s_puiTimestamps = NULL;
	U32 uiTimestamp = 0;
	int i;

	// Bad request, no PowerTreeDef or a PowerTreeDef with no PTType
	if(!(pTreeDef && pTreeTypeDef))
		return -1;

	// No actual Character, so this will obviously be the first
	if(ISNULL(pchar))
		return 0;

	ea32ClearFast(&s_puiTimestamps);

	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		PowerTreeDef *pTreeDefTemp = GET_REF(pchar->ppPowerTrees[i]->hDef);
		
		if(!pTreeDefTemp)
			continue;
		
		if(pTreeDefTemp==pTreeDef)
		{
			// Exact match, the PowerTree we're looking for
			uiTimestamp = pchar->ppPowerTrees[i]->uiTimeCreated;
			ea32Push(&s_puiTimestamps,uiTimestamp);
		}
		else
		{
			if(GET_REF(pTreeDefTemp->hTreeType)==pTreeTypeDef)
			{
				ea32Push(&s_puiTimestamps,pchar->ppPowerTrees[i]->uiTimeCreated);
			}
		}
	}

	if(!uiTimestamp)
	{
		// No exact match, so we don't own it, return the number of this type we already own
		i = ea32Size(&s_puiTimestamps);
	}
	else
	{
		// Exact match, qsort and find the first index of it
		ea32QSort(s_puiTimestamps,CompareU32);
		i = ea32Find(&s_puiTimestamps,uiTimestamp);
	}

	return i;
}



// Returns the cost of a particular rank of a node for the Entity.  Returns -1 on an error.
AUTO_TRANS_HELPER;
int entity_PowerTreeNodeRankCostHelper(ATH_ARG NOCONST(Character)* pChar,
									   PTNodeDef *pNodeDef,
									   int iRank)
{
	int iCost = -1;

	// Everything costs 0 if purchase rules are disabled
	if(s_bPowerTreesDisablePurchaseRules)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();
	if(pNodeDef)
	{
		if(pNodeDef->ppRanks && iRank<eaSize(&pNodeDef->ppRanks) && pNodeDef->ppRanks[iRank])
		{
			// Default cost
			iCost = pNodeDef->ppRanks[iRank]->iCostScaled;
			
			// In the case of variable cost, get the tree and tree type and try to scale
			if(pNodeDef->ppRanks[iRank]->bVariableCost)
			{
				PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
				PTTypeDef *pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;

				if(pTreeTypeDef)
				{
					// Find the type order
					int iTypeCosts = eafSize(&pTreeTypeDef->pfCostScale);
					int iTypeOrder = (ISNULL(pChar) || iTypeCosts<=1) ? 0 : character_PowerTreeTypeOrderHelper(pChar,pTreeDef);
					if(iTypeOrder >= 0 && iTypeCosts >= 0)
					{
						// Clamp to highest valid scale and scale it
						F32 fRealCost = 0;
						MIN1(iTypeOrder,iTypeCosts-1);
						fRealCost = (F32)iCost * pTreeTypeDef->pfCostScale[iTypeOrder];
						iCost = round(fRealCost);
					}
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return iCost;
}

// Returns the sum cost of ranks [0..iRank] of a node for the Entity.
AUTO_TRANS_HELPER;
int entity_PowerTreeNodeRanksCostHelper(ATH_ARG NOCONST(Character)* pChar,
										PTNodeDef *pNodeDef,
										int iRank)
{
	int iCost = 0;

	// Everything costs 0 if purchase rules are disabled
	if(s_bPowerTreesDisablePurchaseRules)
	{
		return 0;
	}

	PERFINFO_AUTO_START_FUNC();
	if(pNodeDef)
	{
		S32 i, bVariableCost = false;

		if(iRank > eaSize(&pNodeDef->ppRanks)-1)
			iRank = eaSize(&pNodeDef->ppRanks)-1;

		for(i=iRank; i>=0; i--)
		{
			// Default cost
			iCost += pNodeDef->ppRanks[i]->iCostScaled;
			bVariableCost |= pNodeDef->ppRanks[i]->bVariableCost;
		}

		// In the case of variable cost, get the tree and tree type and try to scale
		if(iCost && bVariableCost)
		{
			PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
			PTTypeDef *pTreeTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;

			if(pTreeTypeDef)
			{
				// Find the type order
				int iTypeCosts = eafSize(&pTreeTypeDef->pfCostScale);
				int iTypeOrder = (ISNULL(pChar) || iTypeCosts<=1) ? 0 : character_PowerTreeTypeOrderHelper(pChar,pTreeDef);
				if(iTypeOrder >= 0 && iTypeCosts >= 0)
				{
					// Clamp to highest valid scale and scale it
					F32 fRealCost = 0;
					MIN1(iTypeOrder,iTypeCosts-1);
					fRealCost = (F32)iCost * pTreeTypeDef->pfCostScale[iTypeOrder];
					iCost = round(fRealCost);
				}
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return iCost;
}

// Adds or increases the rank of an Enhancement on an existing PowerTree Node.
//  The ONLY validity check is on the Enhancement's iLevelMax.
// Because STO does not currently Enhancements, this has not been instrumented with separate
//  Payer/Main Entities.
AUTO_TRANS_HELPER;
S32 entity_PowerTreeNodeEnhanceAddHelper(ATH_ARG NOCONST(Entity) *pEnt,
										 ATH_ARG NOCONST(PTNode) *pNode,
										 PTNodeEnhancementDef *pEnhDef,
										 PowerDef *pPowerDef)
{
	NOCONST(PTNodeEnhancementTracker) *pTracker = NULL;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pNode) || !pEnhDef || !pPowerDef)
		return false;

	if(powertreenode_FindEnhancementHelper(pNode,pPowerDef))
	{
		// If we already own it, adding is as simple as pushing another purchase tracker
		pTracker = powertreenode_FindEnhancementTrackerHelper(pNode,pPowerDef);
		// Quick check to make sure we haven't already hit the max
		if(pTracker && eaSize(&pTracker->ppPurchases) >= pEnhDef->iLevelMax)
			pTracker = NULL;
	}
	else if(pEnhDef->iLevelMax > 0)
	{
		// We don't own it yet, so add a Power and purchase tracker
		NOCONST(Power)* ppow = CONTAINER_NOCONST(Power, power_Create(pPowerDef->pchName));
		power_InitHelper(ppow,0);
		eaPush(&pNode->ppEnhancements,ppow);

		pTracker = StructCreateNoConst(parse_PTNodeEnhancementTracker);
		SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pPowerDef,pTracker->hDef);
		eaPush(&pNode->ppEnhancementTrackers,pTracker);
	}

	if(pTracker)
	{
		// Track the purchase
		NOCONST(PowerPurchaseTracker) *pPurchase = StructCreateNoConst(parse_PowerPurchaseTracker);
		pPurchase->uiTimeCreated = timeSecondsSince2000();
		pPurchase->uiOrderCreated = character_PowerTreeGetCreationIndexHelper(pEnt->pChar,NULL);
		eaPush(&pTracker->ppPurchases,pPurchase);

		return true;
	}

	return false;
}

// Removes or decreases the rank of an Enhancement on an existing PowerTree Node.
//  NO validity checks
// Because STO does not currently Enhancements, this has not been instrumented with separate
//  Payer/Main Entities.
AUTO_TRANS_HELPER;
S32 entity_PowerTreeNodeEnhanceSubHelper(ATH_ARG NOCONST(Entity) *pEnt,
										 ATH_ARG NOCONST(PTNode) *pNode,
										 PTNodeEnhancementDef *pEnhDef,
										 PowerDef *pPowerDef)
{
	NOCONST(Power) *ppow = NULL;
	NOCONST(PTNodeEnhancementTracker) *pTracker = NULL;
	int iLevel;

	if(ISNULL(pEnt) || ISNULL(pEnt->pChar) || ISNULL(pNode) || !pEnhDef || !pPowerDef)
		return false;

	ppow = powertreenode_FindEnhancementHelper(pNode,pPowerDef);
	pTracker = powertreenode_FindEnhancementTrackerHelper(pNode,pPowerDef);

	if(!ppow || !pTracker)
		return false;

	iLevel = eaSize(&pTracker->ppPurchases);
	if(iLevel == 1)
	{
		// Get rid of Power and the tracker entirely
		eaFindAndRemove(&pNode->ppEnhancements,ppow);
		eaFindAndRemove(&pNode->ppEnhancementTrackers,pTracker);
		StructDestroyNoConst(parse_Power,ppow);
		StructDestroyNoConst(parse_PTNodeEnhancementTracker,pTracker);
	}
	else
	{
		// Pop off a single purchase and destroy it
		NOCONST(PowerPurchaseTracker) *pPurchase = eaPop(&pTracker->ppPurchases);
		StructDestroyNoConst(parse_PowerPurchaseTracker,pPurchase);
	}

	return true;
}

SA_RET_OP_VALID static PTNodeEnhancementDef* GetPTNodeEnhancementDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef, SA_PARAM_OP_VALID PowerDef *pEnhDef)
{
	if(pNodeDef && pEnhDef)
	{
		int i;
		for(i=eaSize(&pNodeDef->ppEnhancements)-1; i>=0; i--)
		{
			if(pEnhDef == GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef))
				return pNodeDef->ppEnhancements[i];
		}
	}
	return NULL;
}

// Attempts to add or remove a level from the Entity's Character's Tree's Node's Enhancement, 
//  returns if the change was successful. If pSteps is not NULL it is filled in.
// This DOES modify the Entity, but it is NOT an ATH.
S32 entity_PowerTreeNodeEnhanceHelper(int iPartitionIdx,
									  NOCONST(Entity)* pEnt,
									  const char *pchTree,
									  const char *pchNode,
									  const char *pchEnhancement,
									  int bAdd,
									  PowerTreeSteps *pSteps)
{
	int r = false;
	PTNodeEnhancementDef *pEnhDef = NULL;
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
	PowerDef *pdef = powerdef_Find(pchEnhancement);

	PERFINFO_AUTO_START_FUNC();

	pEnhDef = GetPTNodeEnhancementDef(pNodeDef,pdef);

 	
	if(pEnhDef)
	{
		PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
		NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, NULL, pNodeDef->pchNameFull);

		if(bAdd && !entity_CanBuyPowerTreeEnhHelper(iPartitionIdx, pEnt, pNodeDef, pEnhDef))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}

		if(pNode)
		{
			if(bAdd)
				r = entity_PowerTreeNodeEnhanceAddHelper(pEnt,pNode,pEnhDef,pdef);
			else
				r = entity_PowerTreeNodeEnhanceSubHelper(pEnt,pNode,pEnhDef,pdef);

			if(r && pSteps)
			{
				PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
				pStep->pchTree = pTreeDef->pchName;
				pStep->pchNode = pNodeDef->pchNameFull;
				pStep->pchEnhancement = pdef->pchName;
				eaPush(&pSteps->ppSteps, pStep);
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return r;
}

// Fixes up the Enhancements and Enhancement tracker arrays of every PTNode owned by the Character
AUTO_TRANS_HELPER;
void character_FixPTNodeEnhancementsHelper(ATH_ARG NOCONST(Character) *pchar)
{
	int i;
	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		for(j=eaSize(&pchar->ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
		{
			NOCONST(PTNode) *pnode = pchar->ppPowerTrees[i]->ppNodes[j];
			PTNodeDef *pnodedef = GET_REF(pnode->hDef);
			int k,s;

			// Skip if we don't have a PTNodeDef, the entire PTNode will get destroyed elsewhere
			if(!pnodedef)
				continue;

			// First pass, clean up bad Enhancements
			for(k=eaSize(&pnode->ppEnhancements)-1; k>=0; k--)
			{
				int l;
				NOCONST(Power) *ppow = pnode->ppEnhancements[k];
				PowerDef *pdef = GET_REF(ppow->hDef);
				
				// See if the PowerDef is still in the NodeDef's list of Enhancements
				for(l=eaSize(&pnodedef->ppEnhancements)-1; l>=0; l--)
				{
					if(GET_REF(pnodedef->ppEnhancements[l]->hPowerDef)==pdef)
						break;
				}

				if(!pdef || l<0)
				{
					// Not a valid def, or a def not in the NodeDef's list, so destroy it.
					//  Any leftover tracker will get cleaned up next pass.
					StructDestroyNoConst(parse_Power,ppow);
					eaRemove(&pnode->ppEnhancements,k);
				}
			}

			s = eaSize(&pnode->ppEnhancements);
			if(!s)
			{
				// No Enhancements.  Destroy any possible trackers and continue.
				eaDestroyStructNoConst(&pnode->ppEnhancementTrackers,parse_PTNodeEnhancementTracker);
				continue;
			}

			// Second pass, clean up bad Enhancement trackers
			for(k=eaSize(&pnode->ppEnhancementTrackers)-1; k>=0; k--)
			{
				int l;
				PowerDef *pdef = GET_REF(pnode->ppEnhancementTrackers[k]->hDef);

				// See if the PowerDef is still in the NodeDef's list of Enhancements
				for(l=eaSize(&pnodedef->ppEnhancements)-1; l>=0; l--)
				{
					if(GET_REF(pnodedef->ppEnhancements[l]->hPowerDef)==pdef)
						break;
				}

				if(!pdef || l<0)
				{
					// Not a valid def, or a def not in the NodeDef's list, so destroy it.
					StructDestroyNoConst(parse_PTNodeEnhancementTracker, pnode->ppEnhancementTrackers[k]);
					eaRemove(&pnode->ppEnhancementTrackers,k);
				}
				else
				{
					// Make sure we've only purchased as many as allowed
					int iLevelMax = pnodedef->ppEnhancements[l]->iLevelMax;
					for(l=eaSize(&pnode->ppEnhancementTrackers[k]->ppPurchases); l>iLevelMax; l--)
					{
						PowerPurchaseTracker *pPurchase = eaPop(&pnode->ppEnhancementTrackers[k]->ppPurchases);
						StructDestroy(parse_PowerPurchaseTracker,pPurchase);
					}
				}
			}

			// Now we have valid Enhancements and Enhancement trackers, make sure they're 1-to-1
			for(k=0; k<s; k++)
			{
				NOCONST(Power) *ppow = pnode->ppEnhancements[k];
				PowerDef *pdef = GET_REF(ppow->hDef);
				NOCONST(PTNodeEnhancementTracker) *ptracker = NULL;
				int l;

				// Search for a matching tracker
				for(l=k; l<eaSize(&pnode->ppEnhancementTrackers); l++)
				{
					if(pdef==GET_REF(pnode->ppEnhancementTrackers[l]->hDef))
					{
						ptracker = pnode->ppEnhancementTrackers[l];
						break;
					}
				}

				// If we didn't find a match
				if(!ptracker)
				{
					// Create a single purchase
					NOCONST(PowerPurchaseTracker) *pPurchase = StructCreateNoConst(parse_PowerPurchaseTracker);
					pPurchase->uiTimeCreated = ppow->uiTimeCreated;

					// Make a new matching tracker, add a single purchase, and insert it
					ptracker = StructCreateNoConst(parse_PTNodeEnhancementTracker);
					SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pdef,ptracker->hDef);
					eaPush(&ptracker->ppPurchases,pPurchase);

					// Insert it and continue
					eaInsert(&pnode->ppEnhancementTrackers,ptracker,k);
					continue;
				}

				// We did find a match, but later in the array than expected
				if(l!=k)
				{
					// Swap places with later tracker
					NOCONST(PTNodeEnhancementTracker) *pTrackerTemp = pnode->ppEnhancementTrackers[k];
					pnode->ppEnhancementTrackers[k] = pnode->ppEnhancementTrackers[l];
					pnode->ppEnhancementTrackers[l] = pTrackerTemp;
				}
			}

			// Destroy any trackers that don't have matching Enhancements (at the end, now that they're 1-to-1
			for(k=eaSize(&pnode->ppEnhancementTrackers)-1; k>=s; k--)
			{
				StructDestroyNoConst(parse_PTNodeEnhancementTracker, pnode->ppEnhancementTrackers[k]);
				eaRemove(&pnode->ppEnhancementTrackers,k);
			}

			// Final pass, now that everything matches exactly
			for(k=0; k<s; k++)
			{
				NOCONST(Power) *ppow = pnode->ppEnhancements[k];

				// Make sure the Enhancements scale with the Character's level
				ppow->iLevel = 0;
			}
		}
	}
}


// Refreshes Powers from Temporary PowerTrees for the Character.  Will destroy all old Powers and
//  make appropriate new ones from scratch.  Returns true if anything was added or removed.  Does
//  not automatically call character_ResetPowersArray().
S32 character_UpdateTemporaryPowerTrees(int iPartitionIdx, Character *pchar)
{
	int i,iTree,iGroup,iNode,iRank,bChanged = false;
	DictionaryEArrayStruct *pDefs = resDictGetEArrayStruct(g_hPowerTreeDefDict);
	PowerTreeDef **ppDefs = (PowerTreeDef**)pDefs->ppReferents;
	NOCONST(Entity)* pEnt = CONTAINER_NOCONST(Entity, pchar->pEntParent);

	PERFINFO_AUTO_START_FUNC();

	// Remove all Temporary Powers from PowerTrees
	for(i=eaSize(&pchar->ppPowersTemporary)-1; i>=0; i--)
	{
		if(pchar->ppPowersTemporary[i]->bTempSourceIsPowerTree)
		{
			Power *ppow = eaRemove(&pchar->ppPowersTemporary,i);
			power_Destroy(ppow,pchar);
			bChanged = true;
		}
	}

	// Adds all the legal Powers from Temporary PowerTrees... wish I could use the
	//  general helpers for this, but it's probably safer for now to avoid that and
	//  just write custom stuff.
	for(iTree=eaSize(&pDefs->ppReferents)-1; iTree>=0; iTree--)
	{
		PowerTreeDef *pdefTree = ppDefs[iTree];

		if(pdefTree->bTemporary && entity_CanBuyPowerTreeHelper(iPartitionIdx,pEnt,pdefTree,true))
		{
			for(iGroup=eaSize(&pdefTree->ppGroups)-1; iGroup>=0; iGroup--)
			{
				PTGroupDef *pdefGroup = pdefTree->ppGroups[iGroup];
				for(iNode=eaSize(&pdefGroup->ppNodes)-1; iNode>=0; iNode--)
				{
					PowerDef *pdefPower = NULL;
					PTNodeDef *pdefNode = pdefGroup->ppNodes[iNode];
					// Don't buy restricted nodes
					if (pdefNode->eFlag & kNodeFlag_RequireTraining)
					{
						continue;
					}
					// Find the PowerDef of the highest rank we can "buy"
					for(iRank=0; iRank<eaSize(&pdefNode->ppRanks); iRank++)
					{
						if(entity_CanBuyPowerTreeNodeIgnorePointsRankHelper(ATR_EMPTY_ARGS,pEnt,pEnt,NULL,pdefGroup,pdefNode,iRank))
						{
							if(GET_REF(pdefNode->ppRanks[iRank]->hPowerDef))
							{
								pdefPower = GET_REF(pdefNode->ppRanks[iRank]->hPowerDef);
							}
						}
					}

					// Make a single Power, add it to Temporary Powers
					if(pdefPower)
					{
						Power *ppow = power_Create(pdefPower->pchName);
						U32 uiPowerID = character_GetNewTempPowerID(pchar);
						power_SetIDHelper(CONTAINER_NOCONST(Power, ppow),uiPowerID);
						ppow->bTempSourceIsPowerTree = true;
						eaIndexedEnable(&pchar->ppPowersTemporary,parse_Power);
						eaPush(&pchar->ppPowersTemporary,ppow);
						bChanged = true;
					}
				}
			}
		}
	}

	if(bChanged)
	{
		entity_SetDirtyBit(pchar->pEntParent,parse_Character,pchar,false);
	}

	PERFINFO_AUTO_STOP();

	return bChanged;
}





// STO node training stuff


// Like entity_PowerTreeNodeIncreaseRankHelper(), except only works on Nodes you don't already own,
//  and if you CAN'T actually buy the Node, it puts it into escrow instead.
// This DOES modify the Entity, but it is NOT an ATH.
NOCONST(PTNode) *entity_PowerTreeNodeEscrowHelper(int iPartitionIdx, NOCONST(Entity) *pEnt, NOCONST(Entity) *pPayer, const char* pchTree, const char* pchNode, PowerTreeSteps *pSteps)
{
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
	NOCONST(PowerTree) *pTree = pTreeDef ? entity_FindPowerTreeHelper(pEnt, pTreeDef) : NULL;
	NOCONST(PTNode) *pNode = pTree ? character_FindPowerTreeNodeHelper(pEnt->pChar, &pTree, pchNode) : NULL;

	if (!pTreeDef || !pNodeDef)
		return NULL;

	// Not allowed to use this function on Nodes you already own
	if(pNode==NULL)
	{
		pNode = entity_PowerTreeNodeIncreaseRankHelper(iPartitionIdx,pEnt,pPayer,pchTree,pchNode,false,false,false,pSteps);

		// If it failed, THAT means we put it in escrow instead
		if(pNode==NULL)
		{
			if(!pTree)
				pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);

			if(pTree)
			{
				// An escrow Node is is basically a malformed Node - no purchase tracking, no Powers, etc
				pNode = CONTAINER_NOCONST(PTNode, powertreenode_create(pNodeDef));
				eaIndexedEnableNoConst(&pTree->ppNodes, parse_PTNode);
				eaPush(&pTree->ppNodes, pNode);
				pNode->iRank = 0;
				pNode->bEscrow = true;

				if(pSteps)
				{
					PowerTreeStep *pStep = StructCreate(parse_PowerTreeStep);
					pStep->pchTree = pchTree;
					pStep->pchNode = pchNode;
					pStep->bEscrow = true;
					eaPush(&pSteps->ppSteps,pStep);
				}
			}
		}
	}

	return pNode;
}

bool entity_CanReplacePowerTreeNodeInEscrow(int iPartitionIdx,
											NOCONST(Entity) *pBuyer, 
											NOCONST(Entity) *pEnt, 
											const char* pchOldTree, const char* pchOldNode, 
											const char* pchNewTree, const char* pchNewNode,
											bool bCheckPropagation )
{
	int i, c;
	PowerTreeDef *pOldTreeDef = powertreedef_Find(pchOldTree);
	PTNodeDef *pOldNodeDef = powertreenodedef_Find(pchOldNode);

	PowerTreeDef *pNewTreeDef = powertreedef_Find(pchNewTree);
	PTNodeDef *pNewNodeDef = powertreenodedef_Find(pchNewNode);

	PTGroupDef *pNewGroupDef = NULL;

	NOCONST(PowerTree) *pOldTree = pOldTreeDef ? entity_FindPowerTreeHelper(pEnt, pOldTreeDef) : NULL;
	NOCONST(PTNode) *pOldNode = pOldTree ? character_FindPowerTreeNodeHelper(pEnt->pChar, &pOldTree, pchOldNode) : NULL;

	NOCONST(PowerTree) *pNewTree = pNewTreeDef ? entity_FindPowerTreeHelper(pEnt, pNewTreeDef) : NULL;

	
	if (!pOldTree || !pOldNodeDef || !pNewTreeDef || !pNewNodeDef || !pOldNode)
		return false;

	if(pNewNodeDef->bSlave)
	{
		return false;
	}

	if (	bCheckPropagation 
		&&	powertree_NodeHasPropagationPowers(pOldNodeDef) != powertree_NodeHasPropagationPowers(pNewNodeDef) )
	{
		return false;
	}

	if (!pNewTree)
	{
		bool bResult;

		bResult = entity_CanBuyPowerTreeHelper(iPartitionIdx,pEnt,pNewTreeDef,false);

		return bResult;
	}

	for (i=eaSize(&pNewTreeDef->ppGroups)-1;i>=0;i--)
	{
		pNewGroupDef = pNewTreeDef->ppGroups[i];
		for(c=eaSize(&pNewGroupDef->ppNodes)-1;c>=0;c--)
		{
			if(pNewNodeDef == pNewGroupDef->ppNodes[c])
				break;
		}
		if(c>=0)
			break;
	}

	if (pNewGroupDef->pRequires && !EntityPTPurchaseReqsHelper(ATR_EMPTY_ARGS,pBuyer,pEnt,pNewTree,pNewGroupDef->pRequires))
	{
		return false;
	}

	return !entity_IsTrainingNode(pEnt,pNewNodeDef);
}











// Adds the Steps specified, in order, to pEnt.
// Does not perform validation, beyond some bare-minimum sanity checks
// Does not do "convenience" purchasing - so it will fail if you ask it to buy a Node
//  and the Entity doesn't have that Node's Tree yet.
AUTO_TRANS_HELPER;
S32 entity_PowerTreeStepsAddHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pPayer, ATH_ARG NOCONST(Entity)* pEnt, PowerTreeSteps *pSteps, const ItemChangeReason *pReason)
{
	int i,s;

	// Verify that if we're in an ATR that the bTransaction flag is set true, or not if not.  Note that this
	//  flag is only set to true in SPECIFIC functions, so if you hit this, you're doing something wrong.
	if(!verify(!exprCurAutoTrans==!pSteps->bTransaction))
		return false;
	
	s = eaSize(&pSteps->ppSteps);
	for(i=0; i<s; i++)
	{
		PowerTreeStep *pStep = pSteps->ppSteps[i];

		if(pStep->pchEnhancement)
		{
			// Could validate rank here?
			NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, NULL, pStep->pchNode);
			PowerDef *pPowerDef = powerdef_Find(pStep->pchEnhancement);
			PTNodeEnhancementDef *pEnhDef = GetPTNodeEnhancementDef(powertreenodedef_Find(pStep->pchNode),pPowerDef);
			if(!entity_PowerTreeNodeEnhanceAddHelper(pEnt,pNode,pEnhDef,pPowerDef))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED add enhancement: %s on %s/%s",pStep->pchEnhancement,pStep->pchNode,pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Added enhancement: %s on %s/%s",pStep->pchEnhancement,pStep->pchNode,pStep->pchTree);
		}
		else if(pStep->pchNode && pStep->bEscrow)
		{
			// Escrow is super-special (and more than a little dumb).  It doesn't do a normal add,
			//  it just creates a basically empty Node and marks it as escrow.  Yeah really.
			NOCONST(PTNode)* pNode;
			NOCONST(PowerTree)* pTree = eaIndexedGetUsingString(&pEnt->pChar->ppPowerTrees,pStep->pchTree);

			// Fail if we don't have the PowerTree
			if(!pTree)
				break;

			// Fail if we already have the Node
			pNode = eaIndexedGetUsingString(&pTree->ppNodes,pStep->pchNode);
			if(pNode)
				break;

			// Make the new escrow Node (which is basically empty, except for the escrow flag), and add it
			//  to the PowerTree
			pNode = CONTAINER_NOCONST(PTNode, powertreenode_create(powertreenodedef_Find(pStep->pchNode)));
			pNode->bEscrow = true;
			eaIndexedEnableNoConst(&pTree->ppNodes, parse_PTNode);
			eaPush(&pTree->ppNodes, pNode);
		}
		else if(pStep->pchNode)
		{
			// Could validate rank here?
			NOCONST(PowerTree)* pTree = eaIndexedGetUsingString(&pEnt->pChar->ppPowerTrees,pStep->pchTree);
			PTNodeDef *pNodeDef = powertreenodedef_Find(pStep->pchNode);
			NOCONST(PTNode)* pNode = entity_PowerTreeNodeAddHelper(ATR_PASS_ARGS,pPayer,pEnt,pTree,pNodeDef,pReason);
			if(!pNode)
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED add node rank: %d of %s/%s",pStep->iRank,pStep->pchNode,pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Added node rank: %d of %s/%s",pStep->iRank,pStep->pchNode,pStep->pchTree);
			
			if(pStep->bEscrow)
			{
				if(pNode->iRank != 0 && !pNode->bEscrow)
				{
					TRANSACTION_APPEND_LOG_FAILURE("FAILED enable node escrow: %s/%s",pStep->pchNode,pStep->pchTree);
					return false;
				}
				pNode->bEscrow = true;
				TRANSACTION_APPEND_LOG_SUCCESS("Enabled node escrow: %s/%s",pStep->pchNode,pStep->pchTree);
			}
		}
		else if(pStep->pchTree)
		{
			PowerTreeDef *pTreeDef = powertreedef_Find(pStep->pchTree);
			if(!entity_PowerTreeAddHelper(pEnt,pTreeDef))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED add tree: %s",pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Added tree: %s",pStep->pchTree);
		}
	}

	return true;
}


// Removes the Steps specified, in order, from pEnt.
// Does not perform validation, beyond some bare-minimum sanity checks
// Charges the Entity as specified on each Step
AUTO_TRANS_HELPER;
S32 entity_PowerTreeStepsRespecHelper(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt, ATH_ARG NOCONST(GameAccountData)* pData, PowerTreeSteps *pSteps, const ItemChangeReason *pReason, int eRespecType)
{
	int i,s;
	S32 bUpdatePointSpent = !!pSteps->bUpdatePointsSpent;

	// Variables for doing the numeric item/bag lookup.
	ItemDef *pCostNumeric = GET_REF(g_PowerTreeRespecConfig.hNumeric);

	// Verify that if we're in an ATR that the bTransaction flag is set true, or not if not.  Note that this
	//  flag is only set to true in SPECIFIC functions, so if you hit this, you're doing something wrong.
	if(!verify(!exprCurAutoTrans==!pSteps->bTransaction))
		return false;

	s = eaSize(&pSteps->ppSteps);
	for(i=0; i<s; i++)
	{
		PowerTreeStep *pStep = pSteps->ppSteps[i];

		PowerTreeDef *pTreeDef = powertreedef_Find(pStep->pchTree);

		// Break on obvious failure - shouldn't get this far, but check it anyway
		if(pTreeDef && pTreeDef->eRespec == kPowerTreeRespec_None)
			break;

		// If this has a cost, try to pay it
		if(pStep->iCostRespec > 0)
		{
#ifdef GAMESERVER
			//Try to use the free respec token, before using the numeric
			if (trhCharacter_UseForceRespecFromSchedule(pEnt))
			{
				//Yay, its free for you!
			}
			else if(g_PowerTreeRespecConfig.bForceUseFreeRespec
				&& (trhCharacter_UseFreeRespecFromSchedule(pEnt) 
					|| slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pData, MicroTrans_GetRespecTokensGADKey(), -1, 0, 100000)
					|| inv_ent_trh_AddNumeric(ATR_PASS_ARGS,pEnt,false,PowerTree_GetRespecTokensPurchaseItem(eRespecType),-1,pReason)))
			{
				//Yay, its free for you!
			}
			else if(!pCostNumeric || !inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pCostNumeric->pchName, -pStep->iCostRespec, pReason))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED to pay respec cost: %d", pStep->iCostRespec);
				return false;
			}
			bUpdatePointSpent = true;
#endif
		}
		else if(pStep->iCostRespec < 0)
		{
			// Negative cost means disallowed - shouldn't get this far, but check it anyway
			TRANSACTION_APPEND_LOG_FAILURE("FAILED disallowed with a negative respec cost: %d", pStep->iCostRespec);
			return false;
		}

		if(pStep->pchEnhancement)
		{
			// Could validate rank here?
			NOCONST(PTNode)* pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, NULL, pStep->pchNode);
			PowerDef *pPowerDef = powerdef_Find(pStep->pchEnhancement);
			PTNodeEnhancementDef *pEnhDef = GetPTNodeEnhancementDef(powertreenodedef_Find(pStep->pchNode),pPowerDef);
			if(!entity_PowerTreeNodeEnhanceSubHelper(pEnt,pNode,pEnhDef,pPowerDef))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED subtract enhancement: %s on %s/%s",pStep->pchEnhancement,pStep->pchNode,pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Subtract enhancement: %s on %s/%s",pStep->pchEnhancement,pStep->pchNode,pStep->pchTree);
		}
		else if(pStep->pchNode)
		{
			// Could validate rank here?
			NOCONST(PowerTree) *pTree = NULL;
			NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, &pTree, pStep->pchNode);
			// This passes NULL for the pPayer, because no one has ever provided one, so starting to provide
			//  one could introduce unforeseen side-effects.
			if(!entity_PowerTreeNodeSubHelper(ATR_PASS_ARGS,pEnt,NULL,pTree,pNode,false,pStep->bEscrow, pReason))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED subtract node rank: %d of %s/%s",pStep->iRank,pStep->pchNode,pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Subtract node rank: %d of %s/%s",pStep->iRank,pStep->pchNode,pStep->pchTree);
		}
		else if(pStep->pchTree)
		{
			if(!pTreeDef || pTreeDef->eRespec!=kPowerTreeRespec_Remove || !entity_PowerTreeRemoveHelper(pEnt,pTreeDef))
			{
				TRANSACTION_APPEND_LOG_FAILURE("FAILED remove tree: %s",pStep->pchTree);
				return false;
			}
			TRANSACTION_APPEND_LOG_SUCCESS("Removed tree: %s",pStep->pchTree);
		}
	}

	if(bUpdatePointSpent)
		character_UpdatePointsSpentPowerTrees(pEnt->pChar, pSteps->bOnlyLowerPoints);

	return true;
}

AUTO_TRANS_HELPER;
void character_UpdatePowerTreeVersion(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		U32 uVersion;
		U32 uFullRespecVersion;
		U32 uMask = ~0;
		uMask <<= POWER_TREE_VERSION_NUMBITS;
		uVersion = pEnt->pChar->uiPowerTreeModCount & ~uMask;
		uFullRespecVersion = pEnt->pChar->uiPowerTreeModCount & uMask;

		if (++uVersion > (1 << POWER_TREE_VERSION_NUMBITS)-1)
		{
			uVersion = 1;
		}
		// Add the full respec version back to the incremented version
		pEnt->pChar->uiPowerTreeModCount = uVersion | uFullRespecVersion;
	}
}

AUTO_TRANS_HELPER;
void character_UpdateFullRespecVersion(ATH_ARG NOCONST(Entity) *pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pChar))
	{
		U32 uFullRespec = pEnt->pChar->uiPowerTreeModCount >> POWER_TREE_VERSION_NUMBITS;
		
		if (++uFullRespec > (1 << POWER_TREE_FULL_RESPEC_VERSION_NUMBITS)-1)
		{
			uFullRespec = 1;
		}
		// This is intentionally clearing the lower bits
		pEnt->pChar->uiPowerTreeModCount = uFullRespec << POWER_TREE_VERSION_NUMBITS;
	}
}

const char *PowerTree_GetRespecTokensPurchaseItem(PTRespecGroupType eRespecType)
{
	S32 i;
	if(eRespecType == kPTRespecGroup_ALL)
	{
		return MicroTrans_GetRespecTokensKeyID();
	}

	for(i = 0; i < eaSize(&g_PowerTreeRespecConfig.eaPTItemRespecNames); ++i)
	{
		if(g_PowerTreeRespecConfig.eaPTItemRespecNames[i]->eRespecGroup == eRespecType)
		{
			return g_PowerTreeRespecConfig.eaPTItemRespecNames[i]->pchName;
		}
	}

	return allocAddString("BadRespecItemToken");
}

const PowerTreeRespecName *PowerTree_GetRespecGroupNumeric(PTRespecGroupType eRespecType)
{
	S32 i;

	for(i = 0; i < eaSize(&g_PowerTreeRespecConfig.eaPTNumericRespecNames); ++i)
	{
		if(g_PowerTreeRespecConfig.eaPTNumericRespecNames[i]->eRespecGroup == eRespecType)
		{	
			return g_PowerTreeRespecConfig.eaPTNumericRespecNames[i];
		}
	}

	return NULL;	
}

bool PowerTree_CanRespecGroupWithNumeric(PTRespecGroupType eRespecType)
{
	const PowerTreeRespecName *pGroup = PowerTree_GetRespecGroupNumeric(eRespecType);

	if(pGroup && pGroup->pchTableName)
	{
		if(powertable_Find(pGroup->pchTableName))
		{
			return true;
		}
	}

	return false;
}

AUTO_TRANS_HELPER;
S32 PowerTree_GetNumericRespecCost(ATH_ARG NOCONST(Entity) *pEnt, PTRespecGroupType eRespecType)
{
	if(NONNULL(pEnt))
	{
		const PowerTreeRespecName *pGroup = PowerTree_GetRespecGroupNumeric(eRespecType);
		if(pGroup)
		{
			// get character level
			S32 iLevel = entity_trh_CalculateExpLevelSlow(pEnt, true) -1;
			iLevel = max(0, iLevel);

			return powertable_Lookup(pGroup->pchTableName, iLevel);
		}
	}

	return 0;
}

#include "AutoGen/PowerTreeHelpers_h_ast.c"