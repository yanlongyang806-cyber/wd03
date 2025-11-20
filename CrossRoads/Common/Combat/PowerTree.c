
/***************************************************************************
*     Copyright (c) 2006-2008, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "PowerTree.h"
#include "PowerTreeEval.h"
#include "PowerTreeHelpers.h"


#include "Entity.h"

#include "Expression.h"

#include "Character.h"
#include "CharacterClass.h"
#include "itemCommon.h"
#include "Powers.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/PowersEnums_h_ast.h"
#include "PowerVars.h"

#include "AutoGen/PowerTree_h_ast.h"
#include "Estring.h"
#include "GlobalStateMachine.h"
#include "ResourceManager.h"
#include "StringCache.h"
#include "file.h"
#include "fileutil.h"
#include "foldercache.h"
#include "textparser.h"
#include "chatCommon.h"
#include "AutoGen/GameAccountData_h_ast.h"

#ifdef GAMECLIENT
#include "gclBaseStates.h"
#include "UIGen.h"
#endif


#include "AutoGen/Character_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

DictionaryHandle g_hPowerTreeTypeDict;
DictionaryHandle g_hPowerTreeNodeTypeDict;
DictionaryHandle g_hPowerTreeEnhTypeDict;
DictionaryHandle g_hPowerTreeDefDict;
DictionaryHandle g_hPowerTreeGroupDefDict;
DictionaryHandle g_hPowerTreeNodeDefDict;

DefineContext *g_pUICategories = NULL;
DefineContext *g_pNodeUICategories = NULL;
int g_iNumOfPowerUICategories;
int g_iNumOfPowerNodeUICategories;
Respec **g_eaRespecs = NULL;

DefineContext *g_PTRespecGroupType = NULL;

bool g_bDebugPowerTree = 0;

AUTO_TRANS_HELPER_SIMPLE;
PowerTreeDef *powertreedef_Find(const char *pchName)
{
	if(pchName && *pchName)
	{
		return (PowerTreeDef*)RefSystem_ReferentFromString(g_hPowerTreeDefDict,pchName);
	}
	return NULL;
}

AUTO_TRANS_HELPER_SIMPLE;
PTNodeDef *powertreenodedef_Find(const char *pchName)
{
	if(pchName && *pchName)
	{
		return (PTNodeDef*)RefSystem_ReferentFromString(g_hPowerTreeNodeDefDict,pchName);
	}
	return NULL;
}

PTGroupDef *powertreegroupdef_Find(const char *pchName)
{
	if(pchName && *pchName)
	{
		return (PTGroupDef*)RefSystem_ReferentFromString(g_hPowerTreeGroupDefDict,pchName);
	}
	return NULL;
}

//creates a node from a node def
AUTO_TRANS_HELPER_SIMPLE;
PTNode *powertreenode_create(PTNodeDef *pNode)
{
	NOCONST(PTNode) *pReturn;

	pReturn = StructCreateNoConst(parse_PTNode);
	SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pchNameFull,pReturn->hDef);

	return (PTNode *)pReturn;
}

// Creates a PowerTree (and initialized the timestamp)
NOCONST(PowerTree)* powertree_Create(PowerTreeDef *pTreeDef)
{
	if(pTreeDef)
	{
		NOCONST(PowerTree)* pTree = StructCreateNoConst(parse_PowerTree);
		SET_HANDLE_FROM_STRING(g_hPowerTreeDefDict,pTreeDef->pchName,pTree->hDef);
		pTree->uiTimeCreated = timeSecondsSince2000();
		return pTree;
	}
	return NULL;
}

// Derives the scale and executed power of each node in the tree
AUTO_TRANS_HELPER;
void powertree_FinalizeNodes(ATH_ARG NOCONST(PowerTree) *pTree)
{
	int i,j,k;	
	PowerTreeDef *pDef = GET_REF(pTree->hDef);
	assert(pTree);

	for(i=eaSize(&pTree->ppNodes)-1; i>=0; i--)
	{
		NOCONST(Power) **ppPowersNodeCopy = NULL;
		NOCONST(PTNode) *pNode = pTree->ppNodes[i];
		PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
		int iMaxRanks;

		// TODO(JW): Fix this leak
		if(!pNodeDef)
		{
			eaRemove(&pTree->ppNodes,i);
			if (isDevelopmentMode())
			{
				Alertf("%s node does not exist any more. Purchased node refunded",REF_STRING_FROM_HANDLE(pNode->hDef));
			}
			continue;
		}
		iMaxRanks = eaSize(&pNodeDef->ppRanks);

		if(pNode->iRank >= iMaxRanks)
		{
			if (isDevelopmentMode())
			{
				Alertf("%s node rank %d is to high. Purchased ranks refunded",REF_STRING_FROM_HANDLE(pNode->hDef), pNode->iRank);
			}
			pNode->iRank = iMaxRanks - 1;
		}

		if ( pNode->bEscrow )
			continue;

		//verify adequate power purchase tracking information exists - if not, create information
		if ( pNode->iRank > eaSize( &pNode->ppPurchaseTracker ) - 1 )
		{
			eaClearStructNoConst( &pNode->ppPurchaseTracker, parse_PowerPurchaseTracker );

			for ( k = 0, j = 0; j <= pNode->iRank; j++ )
			{
				NOCONST(PowerPurchaseTracker)* pTracker = StructCreateNoConst( parse_PowerPurchaseTracker );

				pTracker->uiTimeCreated = pNode->ppPowers[k]->uiTimeCreated;
				
				eaPush( &pNode->ppPurchaseTracker, pTracker );
				
				if ( !pNodeDef->ppRanks[j]->bEmpty ) 
					k++;
			}
		}
		else if(pNode->iRank < eaSize(&pNode->ppPurchaseTracker)-1)
		{
			// We have more purchase trackers than ranks
			Alertf("%s PowerTreeNode owns more PurchaseTrackers than it should.  Attempting to fix.",REF_STRING_FROM_HANDLE(pNode->hDef));
			while(pNode->iRank < eaSize(&pNode->ppPurchaseTracker)-1)
			{
				PowerPurchaseTracker *pTracker = eaPop(&pNode->ppPurchaseTracker);
				StructDestroy(parse_PowerPurchaseTracker,pTracker);
			}
		}

		// Validate the owned Powers
		eaCopy(&ppPowersNodeCopy,&pNode->ppPowers);
		for(j=0; j<=pNode->iRank; j++)
		{
			PTNodeRankDef *pRankDef = pNodeDef->ppRanks[j];
			PowerDef *pRankPowerDef;
			PowerDef *pNodePowerDef;
			
			assert(pRankDef);
			
			if(pRankDef->bEmpty)
				continue;

			pRankPowerDef = GET_REF(pRankDef->hPowerDef);

			assert(pRankPowerDef);

			assert(ppPowersNodeCopy[0]);

			pNodePowerDef = GET_REF(ppPowersNodeCopy[0]->hDef);

			if(pNodePowerDef!=pRankPowerDef)
			{
				// Either the old Power doesn't exist anymore, or it refers to
				//  something else.  Try to fix it.
				Alertf("%s PowerTreeNode rank %d PowerDef %s doesn't match existing Power's PowerDef %s.  Attempting to fix.",REF_STRING_FROM_HANDLE(pNode->hDef), pNode->iRank, pRankPowerDef->pchName, REF_STRING_FROM_HANDLE(ppPowersNodeCopy[0]->hDef));

				// I'm pretty sure this isn't a complete "re-def" of a Power, but hopefully it's enough
				SET_HANDLE_FROM_REFERENT(g_hPowerDefDict,pRankPowerDef,ppPowersNodeCopy[0]->hDef);
				ppPowersNodeCopy[0]->bActive = false;
			}

			eaRemove(&ppPowersNodeCopy,0);		
		}

		// TODO(JW): Fix this leak
		if(eaSize(&ppPowersNodeCopy)>0)
		{
			// We had leftover Powers.  
			Alertf("%s PowerTreeNode owns more Powers than it should.  Attempting to fix.",REF_STRING_FROM_HANDLE(pNode->hDef));
			for(j=0; j<eaSize(&ppPowersNodeCopy); j++)
			{
				eaFindAndRemove(&pNode->ppPowers,ppPowersNodeCopy[j]);
			}
		}

		eaDestroy(&ppPowersNodeCopy);
	}
}

// Returns the node if the character owns it, or null
PTNode *character_GetNode(Character *p, const char *pchNameFull)
{
	int i;
	PTNode *pNode = NULL;
	for(i=eaSize(&p->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		for(j=eaSize(&p->ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
		{
			PTNodeDef *pDef = GET_REF(p->ppPowerTrees[i]->ppNodes[j]->hDef);
			if(pDef && !stricmp(pDef->pchNameFull,pchNameFull))
			{
				pNode = p->ppPowerTrees[i]->ppNodes[j];
				i=0; // break out of outer loop
				break;
			}
			pDef = pDef ? GET_REF(pDef->hNodeClone) : NULL;
			if(pDef && !stricmp(pDef->pchNameFull,pchNameFull))
			{
				pNode = p->ppPowerTrees[i]->ppNodes[j];
				i=0;
				break;
			}
		}

	}
	return pNode;
}


static bool PowerTreeSystemValidateName(const char *pchInternalName, char *chErrorCharOut)
{
	int i;

	for(i=(int)strlen(pchInternalName)-1;i>=0;i--)
	{
		int ialpha = isalnum((unsigned int)pchInternalName[i]);
		if(ialpha == 0)
		{
			if(pchInternalName[i] != '_')
			{
				if(chErrorCharOut)
					*chErrorCharOut = pchInternalName[i];

				return false;
			}
		}
	}
	return true;
}


bool powertrees_Load_RankValidate(PTNodeRankDef *pRank, int iRank, PTNodeDef *pNode, PTGroupDef *pGroup, PowerTreeDef *pTree)
{
	bool bRet = 1;

	if(IS_HANDLE_ACTIVE(pRank->hPowerDef)==(bool)pRank->bEmpty)
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: Rank must either specify a power or be marked as empty\n",pNode->pchNameFull, iRank+1);
		bRet = 0;
	}

	if(!pRank->bEmpty && !GET_REF(pRank->hPowerDef) && IsServer())
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: Nonexistent power specified\n",pNode->pchNameFull, iRank+1);
		bRet = 0;
	}

	if(pRank->bForcedAutoBuy && !(pNode->eFlag & kNodeFlag_AutoBuy))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: There is a Forced AutoBuy Rank of a non-AutoBuy Node. Forced AutoBuy Ranks must be part of an AutoBuy Node.",pNode->pchNameFull, iRank+1);
		bRet = 0;
	}

	if(pRank->bForcedAutoBuy && !pRank->iCost)
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: There is a Forced AutoBuy Rank without a cost. Forced AutoBuy is meant to be used only with nodes that are not free.",pNode->pchNameFull, iRank+1);
		bRet = 0;
	}

	if(pNode->bSlave
		&& (pRank->iCost
			|| (pRank->pchCostVar && pRank->pchCostVar[0])
			|| IS_HANDLE_ACTIVE(pRank->pRequires->hGroup)
			|| pRank->pRequires->pExprPurchase))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: Node is slaved to a MasterNode, ranks can not have a cost or requirements\n",pNode->pchNameFull, iRank+1);
		bRet = 0;
	}

	if(pRank->iCost && pRank->pchCostVar && pRank->pchCostVar[0])
	{
		ErrorFilenamef(pTree->pchFile,"%s: Rank %d: specifies both a cost %d and a cost var %s. It should only specify one.\n",pNode->pchNameFull, iRank+1, pRank->iCost, pRank->pchCostVar);
		bRet = 0;
	}

	pRank->bVariableCost = false;
	pRank->iCostScaled = pRank->iCost;
	if(pRank->iCost || (pRank->pchCostVar && pRank->pchCostVar[0]))
	{
		PTTypeDef *pTreeType = GET_REF(pTree->hTreeType);
		int iCost = 0;
		if(pRank->pchCostVar && pRank->pchCostVar[0])
		{
			MultiVal* pMultiVal = powervar_Find(pRank->pchCostVar);

			if(!pMultiVal)
			{
				ErrorFilenamef(pTree->pchFile,"%s: Rank %d: Rank cost var specifies %s, which is not a valid PowerVar\n",pNode->pchNameFull, iRank+1, pRank->pchCostVar);
				bRet = 0;
			}
			else
			{
				pRank->iCostScaled = iCost = MultiValGetInt(pMultiVal, NULL);
			}
		}
		else
		{
			iCost = pRank->iCost;
		}

		if(pTreeType && eafSize(&pTreeType->pfCostScale))
		{
			if(eafSize(&pTreeType->pfCostScale)==1)
			{
				// Static scale, cache its result in iCostScaled
				pRank->iCostScaled = round((F32)iCost * pTreeType->pfCostScale[0]);
			}
			else
			{
				// Non-static scale, note that we need to do this work
				pRank->bVariableCost = true;
			}
		}

		pNode->bHasCosts = true;

		if(!pRank->pchCostTable || (!powertable_Find(pRank->pchCostTable) && !item_DefFromName(pRank->pchCostTable)))
		{
			ErrorFilenamef(pTree->pchFile,"%s: Rank %d: Rank cost specifies %s, which is not a valid PowerTable or numeric ItemDef\n",pNode->pchNameFull, iRank+1, pRank->pchCostTable);
			bRet = 0;
		}
		else
		{
			if(iRank > 0 && 0!=stricmp(pRank->pchCostTable,pNode->ppRanks[0]->pchCostTable))
			{
				pNode->bRankCostTablesVary = true;
			}
			if(!pRank->bVariableCost
				&& pTree->pchMaxSpendablePointsCostTable && pTree->pchMaxSpendablePointsCostTable[0]
				&& stricmp(pTree->pchMaxSpendablePointsCostTable, pRank->pchCostTable)==0
				&& (!pTree->iMinCost || pRank->iCostScaled < pTree->iMinCost))
			{
				pTree->iMinCost = pRank->iCostScaled;
			}
		}
	}

	if(iRank > 0)
	{
		PTNodeRankDef *pRankPrior = pNode->ppRanks[iRank-1];
		if (0==StructCompare(parse_PTPurchaseRequirements,pRank->pRequires,pRankPrior->pRequires,0,0,0)
			&& (!pRank->pRequires->iMinPointsSpentInThisTree && !pRank->pRequires->iMaxPointsSpentInThisTree))
		{
			pRank->bIgnoreRequires = true;
		}
		if (0==StructCompare(parse_PTPurchaseRequirements,pRank->pRequires,pRankPrior->pRequires,0,0,0)
			&& (!pRank->pRequires->iMinPointsSpentInAnyTree && !pRank->pRequires->iMaxPointsSpentInAnyTree))
		{
			pRank->bIgnoreRequires = true;
		}
	}
	else
	{
		if (0==StructCompare(parse_PTPurchaseRequirements,pRank->pRequires,pGroup->pRequires,0,0,0)
			&& (!pRank->pRequires->iMinPointsSpentInThisTree && !pRank->pRequires->iMaxPointsSpentInThisTree))
		{
			pRank->bIgnoreRequires = true;
		}
		if (0==StructCompare(parse_PTPurchaseRequirements,pRank->pRequires,pGroup->pRequires,0,0,0)
			&& (!pRank->pRequires->iMinPointsSpentInAnyTree && !pRank->pRequires->iMaxPointsSpentInAnyTree))
		{
			pRank->bIgnoreRequires = true;
		}
	}


	//if the group table level is greater than this rank's, then set the rank's derived table level to the group's
	if ( pGroup->pRequires->iTableLevel > pRank->pRequires->iTableLevel )
	{
		pRank->pRequires->iDerivedTableLevel = pGroup->pRequires->iTableLevel;
	}
	else //otherwise the derived table level equals this rank's table level
	{
		pRank->pRequires->iDerivedTableLevel = pRank->pRequires->iTableLevel;
	}

	return bRet;
}

bool powertrees_Load_NodeValidate(PTNodeDef *pNode, int iNode, PTGroupDef *pGroup, PowerTreeDef *pTree)
{
	int i;
	bool bRet = 1;

	if(!pNode->pchName || !stricmp(pNode->pchName,""))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Node %d: Node has invalid Name",pGroup->pchNameFull,iNode);
		bRet = false;
	}

	else
	{
		char achErrorChar;
		if(PowerTreeSystemValidateName(pNode->pchName,&achErrorChar) == false)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node name has invalid characters '%c'",pNode->pchNameFull,achErrorChar);
			bRet = false;
		}
	}

	if(pNode->pchAttribPowerTable)
	{
		PowerTable *ptable = powertable_Find(pNode->pchAttribPowerTable);
		if(!ptable)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node has invalid Attrib PowerTable '%s'",pNode->pchNameFull,pNode->pchAttribPowerTable);
			bRet = false;
		}
		if(pNode->eAttrib==-1)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node has Attrib PowerTable, but no Attrib",pNode->pchNameFull);
			bRet = false;
		}
	}

	if(pNode->bSlave)
	{
		if(pNode->eFlag&kNodeFlag_AutoBuy)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node is slaved to a MasterNode, can not be flagged as AutoBuy",pNode->pchNameFull);
			bRet = false;
		}

		if(IS_HANDLE_ACTIVE(pNode->hNodeClone))
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node is slaved to a MasterNode, can not be a reference",pNode->pchNameFull);
			bRet = false;
		}

		if(IS_HANDLE_ACTIVE(pNode->hNodeRequire))
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node is slaved to a MasterNode, can not require another node",pNode->pchNameFull);
			bRet = false;
		}
	}

	if(IS_HANDLE_ACTIVE(pNode->hNodeRequire))
	{
		const char* pchNodeRequire = REF_STRING_FROM_HANDLE(pNode->hNodeRequire);

		if(stricmp(pchNodeRequire,pNode->pchNameFull)== 0)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node has a requirement on itself!",pNode->pchNameFull);
			bRet = false;
		}
	}

	if(IS_HANDLE_ACTIVE(pNode->hNodePowerSlot) && !GET_REF(pNode->hNodePowerSlot))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Node PowerSlot refers to unknown Node %s",pNode->pchNameFull,REF_STRING_FROM_HANDLE(pNode->hNodePowerSlot));
		bRet = false;
	}

	if(IsServer() && !GET_REF(pNode->pDisplayMessage.hMessage))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Node has invalid Display Name",pNode->pchNameFull);
		bRet = false;
	}
	if(IsServer() && REF_STRING_FROM_HANDLE(pNode->msgRequirements.hMessage) && !GET_REF(pNode->msgRequirements.hMessage))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Node requirements message does not exist",pNode->pchNameFull);
		bRet = false;
	}

	if(eaSize(&pNode->ppRanks) < 1)
	{
		ErrorFilenamef(pTree->pchFile,"%s: Node with no ranks\n",pNode->pchNameFull);
		bRet = 0;
	}
	else
	{
		PowerDef *pDef = GET_REF(pNode->ppRanks[0]->hPowerDef);
		if(pDef && pDef->eType == kPowerType_Enhancement)
		{
			//Make sure all the ranks are enhancements
			for(i=0;i<eaSize(&pNode->ppRanks);i++)
			{
				PowerDef *pDefCheck = GET_REF(pNode->ppRanks[i]->hPowerDef);

				if(pDefCheck && pDefCheck->eType != kPowerType_Enhancement)
				{
					ErrorFilenamef(pTree->pchFile,"%s: Rank %d is required to be an enhancement while first rank is enhancement", pNode->pchName, i);
				}
			}
		}
	}

	// Validate ranks
	pNode->bRankCostTablesVary = false;
	pNode->bHasCosts = false;
	for(i=eaSize(&pNode->ppRanks)-1; i>=0; i--)
	{
		int bGoodRank = powertrees_Load_RankValidate(pNode->ppRanks[i], i, pNode, pGroup, pTree);
		bRet &= bGoodRank;
	}

	for(i=0;i<eaSize(&pGroup->ppNodes);i++)
	{
		if(pGroup->ppNodes[i] != pNode && stricmp(pGroup->ppNodes[i]->pchNameFull,pNode->pchNameFull) == 0)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node shares internal name with another node!",pNode->pchName);
			bRet = false;
		}
	}

	// Validate enhancements
	for(i=eaSize(&pNode->ppEnhancements)-1; i>=0; i--)
	{
		PowerDef *pdefEnh = GET_REF(pNode->ppEnhancements[i]->hPowerDef);
		if(IsServer())
		{
			if(!pdefEnh)
			{
				ErrorFilenamef(pTree->pchFile,"%s: Enhancement %s: Nonexistent power specified\n",pNode->pchNameFull, REF_STRING_FROM_HANDLE(pNode->ppEnhancements[i]->hPowerDef));
				bRet = false;
			}
			else if(pdefEnh->eType!=kPowerType_Enhancement)
			{
				ErrorFilenamef(pTree->pchFile,"%s: Enhancement %s: Power must be an Enhancement\n",pNode->pchNameFull, REF_STRING_FROM_HANDLE(pNode->ppEnhancements[i]->hPowerDef));
				bRet = false;
			}
		}

		if(pNode->ppEnhancements[i]->iCost)
		{
			if(!pNode->ppEnhancements[i]->pchCostTable || (!powertable_Find(pNode->ppEnhancements[i]->pchCostTable) && !item_DefFromName(pNode->ppEnhancements[i]->pchCostTable)))
			{
				ErrorFilenamef(pTree->pchFile,"%s: Enhancement %s: Enhancement cost specifies %s, which is not a valid PowerTable or numeric ItemDef\n",pNode->pchNameFull, REF_STRING_FROM_HANDLE(pNode->ppEnhancements[i]->hPowerDef), pNode->ppEnhancements[i]->pchCostTable);
				bRet = 0;
			}
			else
			{
				if(pTree->pchMaxSpendablePointsCostTable && pTree->pchMaxSpendablePointsCostTable[0]
					&& stricmp(pTree->pchMaxSpendablePointsCostTable, pNode->ppEnhancements[i]->pchCostTable)==0
					&& (!pTree->iMinCost || pNode->ppEnhancements[i]->iCost < pTree->iMinCost))
				{
					pTree->iMinCost = pNode->ppEnhancements[i]->iCost;
				}
			}
		}
	}

	return bRet;
}

bool powertrees_Load_GroupValidate(PTGroupDef *pGroup, int iGroup, PowerTreeDef *pTree)
{
	int i, j;
	bool bRet = 1;

	if(!pGroup->pchGroup || !stricmp(pGroup->pchGroup,""))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Group %d: Group has invalid Name",pTree->pchName,iGroup);
		bRet = false;
	}
	else
	{
		char achErrorChar;
		if(PowerTreeSystemValidateName(pGroup->pchGroup,&achErrorChar) == false)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node name has invalid characters '%c'",pGroup->pchNameFull,achErrorChar);
			bRet = false;
		}
	}

	if(IsServer() && !GET_REF(pGroup->pDisplayMessage.hMessage))
	{
		ErrorFilenamef(pTree->pchFile,"%s: Group %d: Group has invalid Display Name",pTree->pchName,iGroup);
		bRet = false;
	}

	if(pGroup->pRequires)
	{
		if(IS_HANDLE_ACTIVE(pGroup->pRequires->hGroup) && strcmp(REF_STRING_FROM_HANDLE(pGroup->pRequires->hGroup),pGroup->pchNameFull) == 0)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Group has a requirement on itself!",pGroup->pchGroup);
			bRet = false;
		}
	}

	if(eaSize(&pGroup->ppNodes) == 0)
	{
		ErrorFilenamef(pTree->pchFile,"%s: Group %d: Group has no Nodes",pTree->pchName,iGroup);
		bRet = false;
	}

	for(i=0;i<eaSize(&pTree->ppGroups);i++)
	{
		if(pTree->ppGroups[i] != pGroup && strcmp(pTree->ppGroups[i]->pchNameFull,pGroup->pchNameFull) == 0)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Group shares internal name with another group!",pGroup->pchGroup);
			bRet = false;
		}
	}

	// Set and validate the HasMasterNode flag, done before Nodes are validated so they know the state of the Group
	pGroup->bHasMasterNode = false;
	for(i=eaSize(&pGroup->ppNodes)-1; i>=0; i--)
	{
		if(pGroup->ppNodes[i]->eFlag&kNodeFlag_MasterNode)
		{
			if(pGroup->bHasMasterNode)
			{
				ErrorFilenamef(pTree->pchFile,"%s: Group has more than one MasterNode",pGroup->pchGroup);
				bRet = false;
			}
			else
			{
				pGroup->bHasMasterNode = true;
			}
		}
	}

	// If this is a group with a master node, mark all the non-masters as slaves
	if(pGroup->bHasMasterNode)
	{
		for(i=eaSize(&pGroup->ppNodes)-1; i>=0; i--)
		{
			if(!(pGroup->ppNodes[i]->eFlag&kNodeFlag_MasterNode))
			{
				pGroup->ppNodes[i]->bSlave = true;
			}
		}
	}

	// Flag nodes for ForceAutoBuy if any of their ranks have that flag set
	for(i=eaSize(&pGroup->ppNodes)-1; i>=0; i--)
	{
		PTNodeDef *pNode = pGroup->ppNodes[i];
		for(j=eaSize(&pNode->ppRanks)-1; j>=0; j--)
		{
			PTNodeRankDef *pRank = pNode->ppRanks[j];
			if(pRank->bForcedAutoBuy)
			{
				pNode->bForcedAutoBuy = true;
				break;
			}
		}
	}

	//TODO: make sure all requirements can be met
	for(i=eaSize(&pGroup->ppNodes)-1; i>=0; i--)
	{
		bRet &=powertrees_Load_NodeValidate(pGroup->ppNodes[i],i,pGroup,pTree);
	}
	return bRet;
}

bool powertrees_Load_TreeValidate(PowerTreeDef *pTree)
{
	int i;
	bool bRet = 1;
	
	if(!resIsValidName(pTree->pchName))
	{
		ErrorFilenamef(pTree->pchFile,"Power Tree has invalid name!");
		bRet = false;
	}
	else
	{
		char achErrorChar;
		if(PowerTreeSystemValidateName(pTree->pchName,&achErrorChar) == false)
		{
			ErrorFilenamef(pTree->pchFile,"%s: Node name has invalid characters '%c'",pTree->pchName,achErrorChar);
			bRet = false;
		}
	}

	if(pTree->bTemporary && pTree->bAutoBuy)
	{
		ErrorFilenamef(pTree->pchFile,"Power Tree can not be both Temporary and AutoBuy");
	}

	if(IsServer() && !GET_REF(pTree->pDisplayMessage.hMessage))
	{
		ErrorFilenamef(pTree->pchFile,"Power Tree has invalid display name!");
		bRet = false;
	}
	if(pTree->pchMaxSpendablePointsCostTable && pTree->pchMaxSpendablePointsCostTable[0] && !pTree->fMaxSpendablePoints)
	{
		ErrorFilenamef(pTree->pchFile,"Power Tree has a max spendable points cost table, but does not specify a max spendable points value!");
		bRet = false;
	}
	if(eaSize(&pTree->ppGroups) == 0)
	{
		ErrorFilenamef(pTree->pchFile,"Power Tree does not contain any groups!");
		bRet = false;
	}

	// Reset the min cost
	pTree->iMinCost = 0;

	// Validate nodes
	for(i=eaSize(&pTree->ppGroups)-1; i>=0; i--)
	{
		bRet &= powertrees_Load_GroupValidate(pTree->ppGroups[i],i,pTree);
	}

	return bRet;
}

static PTNodeDef *PTFindNode(PowerTreeDef *pTree,const char *pchNode)
{
	int i,c;

	for(i=eaSize(&pTree->ppGroups)-1;i>=0;i--)
	{
		for(c=eaSize(&pTree->ppGroups[i]->ppNodes)-1;c>=0;c--)
		{
			if(stricmp(pTree->ppGroups[i]->ppNodes[c]->pchNameFull,pchNode)== 0)
			{
				return pTree->ppGroups[i]->ppNodes[c];
			}
		}
	}
	return NULL;
}

ExprContext* powertree_GetContext(void)
{
	static ExprContext* s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"PTECharacter");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetPointerVar(s_pContext,"Source",NULL,parse_Entity, true, true);
		exprContextSetPointerVar(s_pContext, "GameAccount", NULL, parse_GameAccountData, false, true);
		exprContextSetUserPtr(s_pContext, NULL, NULL);

		exprContextSetAllowRuntimePartition(s_pContext);
	}

	return s_pContext;
}

static void powertree_Load_Generate(PowerTreeDef *pDef)
{
	if(pDef->pExprRequires)
	{
		ExprContext* pContext = powertree_GetContext();
		exprContextSetUserPtr(pContext, pDef, parse_PowerTreeDef);
		exprGenerate(pDef->pExprRequires, pContext);
		exprContextSetUserPtr(pContext, NULL, NULL);
	}
}

static void powertrees_Load_Post(PowerTreeDef *pTree)
{
	
	int i,c,j;

	for(i=eaSize(&pTree->ppGroups)-1; i>=0; i--)
	{
		for(c=eaSize(&pTree->ppGroups[i]->ppNodes)-1; c>=0; c--)
		{
			PTNodeDef *pNode = pTree->ppGroups[i]->ppNodes[c];
			
			if(IS_HANDLE_ACTIVE(pNode->hNodeClone))
			{
				char *period;
				char treeName[MAX_OBJECT_PATH];				
				// Have to do something slightly tricky because the node dict isn't ready yet
				const char *pNodePath = REF_STRING_FROM_HANDLE(pNode->hNodeClone);
				PowerTreeDef *pClonedDef;
				PTNodeDef *pSourceNode = NULL;

				strcpy(treeName, pNodePath);
				if (period = strchr(treeName, '.'))
				{
					*period = 0;
				}

				pClonedDef = powertreedef_Find(treeName);
				if (pClonedDef)
				{
					pSourceNode = PTFindNode(pClonedDef, pNodePath);
				}

				if(pSourceNode)
				{
					StructCopyFields(parse_PTNodeDef,pSourceNode,pNode,0,TOK_USEROPTIONBIT_1);
					//SET_HANDLE_FROM_REFERENT(g_hPowerTreeNodeDefDict,pSourceNode,pNode->hNodeClone);
					SET_HANDLE_FROM_REFERENT(g_hPowerTreeDefDict,pClonedDef,pNode->hTreeClone);

					for(j=eaSize(&pSourceNode->ppRanks)-1;j>=0;j--)
					{
						StructCopyAll(parse_PTNodeRankDef,pSourceNode->ppRanks[j],pNode->ppRanks[j]);
					}

					// Note that the two nodes use the clone system
					pNode->bCloneSystem = true;
					pSourceNode->bCloneSystem = true;
				}
				else
				{
					ErrorFilenamef(pTree->pchFile,"Missing Clone Node reference %s in tree %s", pNodePath, pTree->pchName);
				}
			}

#ifdef PURGE_POWER_TREE_POWER_DEFS
			// Set up the string sent to the client instead of the reference
			for(j=eaSize(&pNode->ppRanks)-1; j>=0; j--)
			{
				if(IS_HANDLE_ACTIVE(pNode->ppRanks[j]->hPowerDef))
				{
					pNode->ppRanks[j]->pchPowerDefName = allocAddString(REF_STRING_FROM_HANDLE(pNode->ppRanks[j]->hPowerDef));
				}
			}

			for(j=eaSize(&pNode->ppEnhancements)-1; j>=0; j--)
			{
				if(IS_HANDLE_ACTIVE(pNode->ppEnhancements[j]->hPowerDef))
				{
					pNode->ppEnhancements[j]->pchPowerDefName = allocAddString(REF_STRING_FROM_HANDLE(pNode->ppEnhancements[j]->hPowerDef));
				}
			}
#endif
		}
	}

	powertree_Load_Generate(pTree);

	if (!IsClient())
	{
		powertrees_Load_TreeValidate(pTree);
	}
}

static int powertree_ResourceValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, void *pResource, U32 userID)
{
	PowerTreeDef *pTree = pResource;
	switch (eType)
	{
	case RESVALIDATE_POST_BINNING:
		powertrees_Load_Post(pTree);
		return VALIDATE_HANDLED;

	case RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename(&pTree->pchFile, "defs/powertrees", NULL, pTree->pchName, "powertree");
		return VALIDATE_HANDLED;
	}

	return VALIDATE_NOT_HANDLED;
}

/***** Fill Ref Dicts and StashTables *****/

void powertreedefs_RemoveRefDict(PowerTreeDef *pTree)
{
	int c,j;
	for(j=eaSize(&pTree->ppGroups)-1;j>=0;j--)
	{
		for(c=eaSize(&pTree->ppGroups[j]->ppNodes)-1;c>=0;c--)
		{
			RefSystem_RemoveReferent(pTree->ppGroups[j]->ppNodes[c],0);
		}
		RefSystem_RemoveReferent(pTree->ppGroups[j],0);
	}
}

// Walk the list of tree defs and add them to the reference dictionary and stash table
static void powertreedefs_FillRefDictAndStash(PowerTreeDef *pTree)
{
	int c,j;

	for(j=eaSize(&pTree->ppGroups)-1; j>=0; j--)
	{
		void *pReferent = RefSystem_ReferentFromString("PowerTreeGroupDef",pTree->ppGroups[j]->pchNameFull);
		if (pReferent && pReferent != pTree->ppGroups[j])
		{
			RefSystem_RemoveReferent(pReferent,false);
			pReferent = NULL;
		}
		if (!pReferent)
		{
			RefSystem_AddReferent("PowerTreeGroupDef",pTree->ppGroups[j]->pchNameFull,pTree->ppGroups[j]);
		}		
		for(c=eaSize(&pTree->ppGroups[j]->ppNodes)-1;c>=0; c--)
		{
			pReferent = RefSystem_ReferentFromString("PowerTreeNodeDef",pTree->ppGroups[j]->ppNodes[c]->pchNameFull);
			if (pReferent && pReferent != pTree->ppGroups[j]->ppNodes[c])
			{
				RefSystem_RemoveReferent(pReferent,false);
				pReferent = NULL;
			}
			if (!pReferent)
			{
				RefSystem_AddReferent("PowerTreeNodeDef",pTree->ppGroups[j]->ppNodes[c]->pchNameFull,pTree->ppGroups[j]->ppNodes[c]);
			}		
		}
	}
}

static void PowerTreeUICategories_Load(void)
{
	PowerTreeUICategories categories = {0};
	S32 i;
	const char* pchMessageFail;

	g_pUICategories = DefineCreate();

	loadstart_printf("Loading Power Tree UI Categories... ");

	ParserLoadFiles(NULL, "defs/config/powertreeuicategories.def", "powertreeuicategories.bin", PARSER_OPTIONALFLAG, parse_PowerTreeUICategories, &categories);

	for (i = 0; i < eaSize(&categories.pchNames); i++)
		DefineAddInt(g_pUICategories, categories.pchNames[i], i);
	g_iNumOfPowerUICategories = i;
	

	StructDeInit(parse_PowerTreeUICategories, &categories);

	if (pchMessageFail = StaticDefineVerifyMessages(PowerTreeUICategoryEnum))
		Errorf("Not all Power tree ui category messages were found: %s", pchMessageFail);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(PowerTreeUICategoryEnum, "PowerTreeUICategory_");
#endif

	loadend_printf(" done (%d UI Categories).", i);
}

static void PTNodeUICategories_Load(void)
{
	PTNodeUICategories categories = {0};
	S32 i;
	const char* pchMessageFail;

	g_pNodeUICategories = DefineCreate();

	loadstart_printf("Loading Power Tree Node UI Categories... ");

	ParserLoadFiles(NULL, "defs/config/ptnodeuicategories.def", "ptnodeuicategories.bin", PARSER_OPTIONALFLAG, parse_PowerTreeUICategories, &categories);

	for (i = 0; i < eaSize(&categories.pchNames); i++)
		DefineAddInt(g_pNodeUICategories, categories.pchNames[i], i+1);
	g_iNumOfPowerNodeUICategories = i+1;

	StructDeInit(parse_PTNodeUICategories, &categories);

	if (pchMessageFail = StaticDefineVerifyMessages(PTNodeUICategoryEnum))
		Errorf("Not all Power Tree Node UI category messages were found: %s", pchMessageFail);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(PTNodeUICategoryEnum, "PTNodeUICategory_");
#endif
	loadend_printf(" done (%d UI Categories).", i);
}

// Globally accessible PowerTreeRespecConfig
PowerTreeRespecConfig g_PowerTreeRespecConfig = {0};

static void PowerTreeRespecConfigGenerate(void)
{
	ExprContext *pContext = powerTreeEval_GetContextRespec();
	// Simple generate
	if(g_PowerTreeRespecConfig.pExprCostStep)
	{
		exprGenerate(g_PowerTreeRespecConfig.pExprCostStep, pContext);
	}

	if(g_PowerTreeRespecConfig.pExprCostBase)
	{
		exprGenerate(g_PowerTreeRespecConfig.pExprCostBase, pContext);
	}

	if(g_PowerTreeRespecConfig.pExprRequiredPointsSpent)
	{
		exprGenerate(g_PowerTreeRespecConfig.pExprRequiredPointsSpent, pContext);
	}
}

static int CmpTimes(const Respec **a, const Respec **b)
{
	if((*a)->uiDerivedRespecTime == (*b)->uiDerivedRespecTime)
		return(0);
	else if((*a)->uiDerivedRespecTime > (*b)->uiDerivedRespecTime)
		return -1;
	else
		return 1;
}

void CharacterRespec_ParseRespecs(Respecs *pRespecs)
{
	S32 i, j;
	U32 uiNow = timeSecondsSince2000();
	for (i = 0; i < eaSize(&pRespecs->eaRespecs); i++)
	{
		Respec *pRespec = StructClone(parse_Respec, pRespecs->eaRespecs[i]);
		bool bValid = true;
		pRespec->uiDerivedRespecTime = timeGetSecondsSince2000FromDateString(pRespec->pchRespecTimeString);
		if(pRespec->uiDerivedRespecTime == 0)
		{
			loadupdate_printf("[%s] is an invalid date.  Didn't get loaded.", pRespecs->eaRespecs[i]->pchRespecTimeString);
			StructDestroy(parse_Respec, pRespec);
			continue;
		}
				
		// check for invalid power tree defs
		for(j = 0; j < eaSize(&pRespec->eaTrees); ++j)
		{
			if(GET_REF(pRespec->eaTrees[j]->respecTreeDef) == NULL)
			{
				bValid = false;
				break;
			}
		}

		if(!bValid)
		{
			loadupdate_printf("Respec date [%s] has a bad power tree ref. Didn't get loaded.", pRespecs->eaRespecs[i]->pchRespecTimeString);
			StructDestroy(parse_Respec, pRespec);
			continue;
		}

		// check for invalid power defs
		for(j = 0; j < eaSize(&pRespec->eaPowers); ++j)
		{
			if(GET_REF(pRespec->eaPowers[j]->respecPowerDef) == NULL)
			{
				bValid = false;
				break;
			}
		}

		if(!bValid)
		{
			loadupdate_printf("Respec date [%s] has a bad power def ref. Didn't get loaded.", pRespecs->eaRespecs[i]->pchRespecTimeString);
			StructDestroy(parse_Respec, pRespec);
			continue;
		}

		eaPush(&g_eaRespecs, pRespec);
	}

	eaQSort(g_eaRespecs, CmpTimes);
}

void CharacterRespec_ReloadRespecs(const char *pchRelPath, int UNUSED_when)
{
	Respecs pRespecs = {0};
	loadstart_printf("Reloading Respec Dates and Times...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserLoadFiles(NULL, "defs/config/PowerTreeRespecDates.def", "PowerTreeRespecDates.bin", PARSER_OPTIONALFLAG, parse_Respecs, &pRespecs);

	//Clear it, then load it
	eaClearStruct(&g_eaRespecs, parse_Respec);

	CharacterRespec_ParseRespecs(&pRespecs);

	StructDeInit(parse_Respecs, &pRespecs);

	loadend_printf(" done (%d Respec Dates and Times).", eaSize(&g_eaRespecs));
}

void CharacterRespec_LoadRespecs(void)
{
	Respecs pRespecs = {0};

	loadstart_printf("Loading Respec Dates and Times...");

	ParserLoadFiles(NULL, "defs/config/PowerTreeRespecDates.def", "PowerTreeRespecDates.bin", PARSER_OPTIONALFLAG, parse_Respecs, &pRespecs);

	CharacterRespec_ParseRespecs(&pRespecs);

	StructDeInit(parse_Respecs, &pRespecs);

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerTreeRespecDates.def", CharacterRespec_ReloadRespecs);
	}

	loadend_printf(" done (%d Respec Dates and Times).", eaSize(&g_eaRespecs));
}

static void PowerTreeRespecConfigReload(const char *pchRelPath, int UNUSED_when)
{
	loadstart_printf("Reloading PowerTreeRespecConfig...");

	fileWaitForExclusiveAccess(pchRelPath);
	errorLogFileIsBeingReloaded(pchRelPath);

	ParserReloadFile(pchRelPath,parse_PowerTreeRespecConfig,&g_PowerTreeRespecConfig,NULL,0);

	PowerTreeRespecConfigGenerate();

	loadend_printf(" done");
}

static void PowerTreeRespecConfigLoad(void)
{
	loadstart_printf("Loading PowerTreeRespecConfig...");

	//do non shared memory load

	ParserLoadFiles(NULL, "defs/config/PowerTreeRespecConfig.def", "PowerTreeRespecConfig.bin", PARSER_OPTIONALFLAG, parse_PowerTreeRespecConfig, &g_PowerTreeRespecConfig);

	PowerTreeRespecConfigGenerate();

	// Reload callbacks
	if(isDevelopmentMode())
	{
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/PowerTreeRespecConfig.def", PowerTreeRespecConfigReload);
	}

	loadend_printf(" done.");
}

AUTO_RUN;
int RegisterPowerTreeDict(void)
{
	// Set up reference dictionaries
	g_hPowerTreeTypeDict = RefSystem_RegisterSelfDefiningDictionary("PowerTreeTypeDef",false, parse_PTTypeDef, true, true, "PowerTreeType");
	g_hPowerTreeNodeTypeDict = RefSystem_RegisterSelfDefiningDictionary("PTNodeTypeDef",false, parse_PTNodeTypeDef, true, true, "PowerTreeNodeType");
	g_hPowerTreeEnhTypeDict = RefSystem_RegisterSelfDefiningDictionary("PTEnhTypeDef",false,parse_PTEnhTypeDef, true, true, "EnhancementType");
	g_hPowerTreeDefDict = RefSystem_RegisterSelfDefiningDictionary("PowerTreeDef",false, parse_PowerTreeDef, true, true, "PowerTree");
	g_hPowerTreeGroupDefDict = RefSystem_RegisterSelfDefiningDictionary("PowerTreeGroupDef",false, parse_PTGroupDef, true, true, "PowerTreeGroup");
	g_hPowerTreeNodeDefDict = RefSystem_RegisterSelfDefiningDictionary("PowerTreeNodeDef",false, parse_PTNodeDef, true, true, "PowerTreeNode");

	resDictManageValidation(g_hPowerTreeDefDict, powertree_ResourceValidateCB);
	resDictSetDisplayName(g_hPowerTreeDefDict, "Power Tree", "Power Trees", RESCATEGORY_DESIGN);

	if (IsServer())
	{
		resDictProvideMissingResources(g_hPowerTreeDefDict);
		//resDictProvideMissingResources(g_hPowerTreeNodeDefDict);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_hPowerTreeDefDict, ".displayMessage.Message", NULL, NULL, ".Notes", NULL);
			resDictMaintainInfoIndex(g_hPowerTreeTypeDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hPowerTreeEnhTypeDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hPowerTreeNodeTypeDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hPowerTreeGroupDefDict, NULL, NULL, NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_hPowerTreeNodeDefDict, NULL, NULL, NULL, NULL, NULL);
		}
	} 
	else
	{
		resDictRequestMissingResources(g_hPowerTreeDefDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		//resDictRequestMissingResources(g_hPowerTreeNodeDefDict, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult powertreedef_Fixup(PowerTreeDef *pDef, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		{
			int i,n,r;

			for(i=0;i<eaSize(&pDef->ppGroups);i++)
			{
				
				for(n=0;n<eaSize(&pDef->ppGroups[i]->ppNodes);n++)
				{
					for(r=0;r<eaSize(&pDef->ppGroups[i]->ppNodes[n]->ppRanks);r++)
					{
						REMOVE_HANDLE(pDef->ppGroups[i]->ppNodes[n]->ppRanks[r]->hPowerDef);
					}
					RefSystem_RemoveReferent(pDef->ppGroups[i]->ppNodes[n],true);
				}
				RefSystem_RemoveReferent(pDef->ppGroups[i],true);
			}
		}
	}

	return bRet;
}

ExprContext* powertreetypes_GetContext(void)
{
	static ExprContext* s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"PTECharacter");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetPointerVar(s_pContext,"Source",NULL,parse_Entity, true, true);
		exprContextSetPointerVar(s_pContext, "GameAccount", NULL, parse_GameAccountData, false, true);
		exprContextSetPointerVar(s_pContext, "PowerTree", NULL, parse_PowerTreeDef, false, true);

		exprContextSetAllowRuntimePartition(s_pContext);
	}

	return s_pContext;
}

ExprContext* powertreeenhtypes_GetContext(void)
{
	static ExprContext* s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"PTECharacter");
		exprContextAddFuncsToTableByTag(stTable,"PTENode");
		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetPointerVar(s_pContext,"CurNode",NULL,parse_PTNode, false, false);

		exprContextSetAllowRuntimePartition(s_pContext);
	}

	return s_pContext;
}

static void powertreenodetypes_generate(PTNodeTypeDef *pDef)
{
	char *pchFind;
	char *estrName = NULL;

	estrCreate(&estrName);
	estrPrintf(&estrName,"%s",pDef->pchNodeType);

	pchFind = strrchr(estrName,'.');

	while(pchFind)
	{
		PTNodeTypeDef *pDefFind = NULL;

		pchFind[0] = 0;
		pDefFind = RefSystem_ReferentFromString(g_hPowerTreeNodeTypeDict,estrName);

		if(pDefFind)
		{
			eaPush(&pDef->ppchSubTypes, StructAllocString(estrName));
			//eaPush(&pDef->ppSubTypes, pDefFind);
		}

		pchFind = strrchr(estrName,'.');
	}

	estrDestroy(&estrName);
}

ExprContext *ptpurchaserequirements_GetContext(void)
{
	static ExprContext* s_pContext = NULL;

	if(!s_pContext)
	{
		ExprFuncTable* stTable;

		s_pContext = exprContextCreate();

		stTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(stTable,"PTECharacter");
		exprContextAddFuncsToTableByTag(stTable,"Entity");
		exprContextAddFuncsToTableByTag(stTable,"entityutil");
		exprContextAddFuncsToTableByTag(stTable,"util");
		exprContextSetFuncTable(s_pContext, stTable);

		exprContextSetPointerVar(s_pContext,"Source",NULL,parse_Entity,true,true);
		exprContextSetPointerVar(s_pContext,"GameAccount",NULL,parse_GameAccountData,false,true);
		exprContextSetSelfPtr(s_pContext,NULL);
		exprContextSetAllowRuntimeSelfPtr(s_pContext);
		exprContextSetAllowRuntimePartition(s_pContext);
	}

	return s_pContext;
}

AUTO_FIXUPFUNC;
TextParserResult PTNodeTypeDef_FixUp(PTNodeTypeDef *pDef, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch(eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
		{
			powertreenodetypes_generate(pDef);
		}
	}

	return bRet;
}

AUTO_FIXUPFUNC;
TextParserResult PTTypeDef_Fixup(PTTypeDef *pDef, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch(eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
	case FIXUPTYPE_POST_RELOAD:
		{
			exprGenerate(pDef->pExprPurchase,powertreetypes_GetContext());
		}
	}

	return bRet;
}

AUTO_FIXUPFUNC;
TextParserResult PTEnhTypeDef_Fixup(PTEnhTypeDef *pDef, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch(eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
	case FIXUPTYPE_POST_RELOAD:
		{
			exprGenerate(pDef->pExpr,powertreeenhtypes_GetContext());
		}
	}

	return bRet;
}

AUTO_FIXUPFUNC;
TextParserResult PTPurchaseRequirements_Fixup(PTPurchaseRequirements *pRequires, enumTextParserFixupType eFixupType, void *pExtraData)
{
	bool bRet = true;

	switch(eFixupType)
	{
	case FIXUPTYPE_POST_BINNING_DURING_LOADFILES:
	case FIXUPTYPE_POST_RELOAD:
		{
			exprGenerate(pRequires->pExprPurchase,ptpurchaserequirements_GetContext());
		}
	}

	return bRet;
}

#ifdef PURGE_POWER_TREE_POWER_DEFS
static void PowerTreeDefFillPowerDefRefs(PowerTreeDef *pdef)
{
	int i,j;
	for(i=eaSize(&pdef->ppGroups)-1; i>=0; i--)
	{
		for(j=eaSize(&pdef->ppGroups[i]->ppNodes)-1; j>=0; j--)
		{
			if(eaSize(&pdef->ppGroups[i]->ppNodes[j]->ppRanks))
			{
				PTNodeRankDef *pdefRank = pdef->ppGroups[i]->ppNodes[j]->ppRanks[0];
				if(pdefRank->pchPowerDefName)
				{
					SET_HANDLE_FROM_STRING(g_hPowerDefDict,pdefRank->pchPowerDefName,pdefRank->hPowerDef);
				}
			}
		}
	}
}
#endif

static void PowerTreeReferenceCallback(enumResourceEventType eType, const char *pDictName, const char *pTreeName, PowerTreeDef *pTree, void *pUserData)
{
	switch (eType)
	{
	case RESEVENT_RESOURCE_ADDED:
	case RESEVENT_RESOURCE_MODIFIED:
		powertreedefs_FillRefDictAndStash(pTree);
#ifdef PURGE_POWER_TREE_POWER_DEFS
#ifdef GAMECLIENT
		if(GSM_IsStateActiveOrPending(GCL_LOGIN_NEW_CHARACTER_WAITING_FOR_DATA) || GSM_IsStateActiveOrPending(GCL_LOGIN_NEW_CHARACTER_CREATION))
		{
			PowerTreeDefFillPowerDefRefs(pTree);
		}
#endif
#endif
		break;
	case RESEVENT_RESOURCE_REMOVED:
	case RESEVENT_RESOURCE_PRE_MODIFIED:
		powertreedefs_RemoveRefDict(pTree);	
		break;
	}
}

// Set the enum values for PTRespecGroupType, must be before loading powertreetypes
static void powertrees_LoadPTRespecGroupTypes(void)
{
	S32 i;
	PTRespecGroupTypeNames Names = {0};

	g_PTRespecGroupType = DefineCreate();

	loadstart_printf("Loading PTRespecGroupType...");

	ParserLoadFiles(NULL, "defs/config/PTRespecGroupTypeNames.def", "PTRespecGroupTypeNames.bin", PARSER_OPTIONALFLAG, parse_PTRespecGroupTypeNames, &Names);

	for(i=0; i < eaSize(&Names.eaNames); ++i)
	{
		if(Names.eaNames[i]->pcName && Names.eaNames[i]->pcName[0])
		{
			DefineAddInt(g_PTRespecGroupType, Names.eaNames[i]->pcName, kPTRespecGroup_FirstGameSpecific + i);				
		}
	}

	StructDeInit(parse_PTRespecGroupTypeNames, &Names);

#ifdef GAMECLIENT
	ui_GenInitStaticDefineVars(PTRespecGroupTypeEnum, "RespecGroupType");
#endif

	loadend_printf("Done.");

}

AUTO_STARTUP(PowerTrees) ASTRT_DEPS(Powers,PowerReplaces,Items);
void powertrees_Load(void)
{
	resDictRegisterEventCallback(g_hPowerTreeDefDict,PowerTreeReferenceCallback,NULL);

	PowerTreeUICategories_Load();
	PTNodeUICategories_Load();
	powertrees_LoadPTRespecGroupTypes();	// needs to be before PowerTreeRespecConfigLoad
	PowerTreeRespecConfigLoad();
	
	resLoadResourcesFromDisk(g_hPowerTreeTypeDict,"defs/powertrees/","powertreetypes.def","PowerTreeTypes.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	resLoadResourcesFromDisk(g_hPowerTreeNodeTypeDict,"defs/powertrees/","PTNodeTypes.def","PTNodeTypes.bin",PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
	resLoadResourcesFromDisk(g_hPowerTreeEnhTypeDict,"defs/powertrees/","EnhancementTypes.def","PowerTreeEnhTypes.bin",PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);

	if (IsServer())
	{
		resLoadResourcesFromDisk(g_hPowerTreeDefDict, "defs/powertrees/", ".powertree", "PowerTrees.bin", PARSER_OPTIONALFLAG | RESOURCELOAD_SHAREDMEMORY);
		stringCacheSharedMemoryRegisterExtra("PowerTreeGroupDef");
		stringCacheSharedMemoryRegisterExtra("PowerTreeNodeDef");
		// Set these so shared memory knows to expect these dictionary names
	}

	// must be here as some respecs count on the g_hPowerTreeDefDict to be valid ...
	CharacterRespec_LoadRespecs();

}

// Returns the highest activatable Power from the node
Power *powertreenode_GetActivatablePower(PTNode *pNode)
{
	S32 i;
	for (i = eaSize(&pNode->ppPowers) - 1; i >= 0; i--)
	{
		if (pNode->ppPowers[i] && power_DefDoesActivate(GET_REF(pNode->ppPowers[i]->hDef)))
			return pNode->ppPowers[i];
	}
	return NULL;
}

PowerDef *powertree_PowerDefFromNode(PTNodeDef *pDef, S32 iRank)
{
	if (pDef)
	{
		while (iRank >= 0)
		{
			PowerDef *pPowerDef = GET_REF(pDef->ppRanks[iRank]->hPowerDef);
			if (pPowerDef)
				return pPowerDef;
		}
	}
	return NULL;
}

const char *powertree_PowerDefNameFromNode(PTNodeDef *pDef, S32 iRank)
{
	if (pDef)
	{
		while (iRank >= 0)
		{
			const char *pchHandleName = REF_STRING_FROM_HANDLE(pDef->ppRanks[iRank]->hPowerDef);
			if (pchHandleName)
				return pchHandleName;
			iRank--;
		}
	}
	return NULL;
}

const char *powertreenodedef_GetDisplayName(PTNodeDef *pDef)
{
	if (!pDef)
		return "Unknown Node";
	else if (GET_REF(pDef->pDisplayMessage.hMessage))
		return TranslateMessageRef(pDef->pDisplayMessage.hMessage);
	else if (pDef->pchName)
		return pDef->pchName;
	else if (pDef->pchNameFull)
		return pDef->pchNameFull;
	else
		return "Unnamed Node";
}

#ifdef PURGE_POWER_TREE_POWER_DEFS

// The number of seconds a PTNodeDef keeps its PowerDef references for, upon request
#define NODE_PURGE_TIME 300

// Requests that the node fill in all its PowerDef references
void powertreenodedef_RequestPowerDefs(PTNodeDef *pdef)
{
	U32 uiTimestampPurge = timeSecondsSince2000() + NODE_PURGE_TIME;
	if(!pdef->uiTimestampPurge)
	{
		int i;
		
		// Fill in unfilled handles for each rank
		for(i=eaSize(&pdef->ppRanks)-1; i>=0; i--)
		{
			if(!IS_HANDLE_ACTIVE(pdef->ppRanks[i]->hPowerDef))
			{
				SET_HANDLE_FROM_STRING(g_hPowerDefDict,pdef->ppRanks[i]->pchPowerDefName,pdef->ppRanks[i]->hPowerDef);
			}
		}

		// Fill in unfilled handles for each enhancement
		for(i=eaSize(&pdef->ppEnhancements)-1; i>=0; i--)
		{
			if(!IS_HANDLE_ACTIVE(pdef->ppEnhancements[i]->hPowerDef))
			{
				SET_HANDLE_FROM_STRING(g_hPowerDefDict,pdef->ppEnhancements[i]->pchPowerDefName,pdef->ppEnhancements[i]->hPowerDef);
			}
		}
	}

	// Set purge timestamp
	pdef->uiTimestampPurge = uiTimestampPurge;
}

// Purges old requested PowerDef references from all PowerTreeDefs in the dictionary.
//  If a Character is provided, it will make sure PowerDef references relevant to that Character are around.
void powertrees_CleanPowerDefs(Character *pchar)
{
	int i,j,k,l;
	U32 uiTimestamp = timeSecondsSince2000();
	DictionaryEArrayStruct *pEArrayStruct = resDictGetEArrayStruct(g_hPowerTreeDefDict);
	PowerTreeDef **ppDefs = (PowerTreeDef**)pEArrayStruct->ppReferents;

	// If there is a Character, make sure all its nodes are currently requested
	if(pchar)
	{
		for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
		{
			for(j=eaSize(&pchar->ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
			{
				PTNodeDef *pdefNode = GET_REF(pchar->ppPowerTrees[i]->ppNodes[j]->hDef);
				if(pdefNode)
				{
					powertreenodedef_RequestPowerDefs(pdefNode);
				}
			}
		}
	}

	for(i=eaSize(&ppDefs)-1; i>=0; i--)
	{
		for(j=eaSize(&ppDefs[i]->ppGroups)-1; j>=0; j--)
		{
			for(k=eaSize(&ppDefs[i]->ppGroups[j]->ppNodes)-1; k>=0; k--)
			{
				PTNodeDef *pdefNode = ppDefs[i]->ppGroups[j]->ppNodes[k];
				// If the NodeDef has a timestamp and it's less than now, purge its handles
				if(pdefNode->uiTimestampPurge && pdefNode->uiTimestampPurge<uiTimestamp)
				{
					for(l=eaSize(&pdefNode->ppRanks)-1; l>=0; l--)
					{
						REMOVE_HANDLE(pdefNode->ppRanks[l]->hPowerDef);
					}

					for(l=eaSize(&pdefNode->ppEnhancements)-1; l>=0; l--)
					{
						REMOVE_HANDLE(pdefNode->ppEnhancements[l]->hPowerDef);
					}
					pdefNode->uiTimestampPurge = 0;
				}
			}
		}
	}
}

#endif

//Eval Functions --------------------------------------------------------------------------

static Character *GetSource(ExprContext *pContext)
{
	return exprContextGetVarPointerUnsafe(pContext, "Source");
}

// RvP: Commented these out because I couldn't find a single exprcontext exposing them
//AUTO_EXPR_FUNC(exprFuncListTreeEval) ACMD_NAME(PTOrigin);
//ExprFuncReturnVal ExprPTOrigin(ExprContext *pContext, ACMD_EXPR_INT_OUT piRet, const char *pchOrigin)
//{
	//return ExprFuncReturnError;
//}

//AUTO_EXPR_FUNC(exprFuncListTreeEval) ACMD_NAME(PTLevel);
//ExprFuncReturnVal ExprPTLevel(ExprContext* pContext, ACMD_EXPR_INT_OUT piRet, int iCompareLevel)
//{
	//Character *pSrc = GetSource(pContext);
//
//
	//if(pSrc)
	//{
		//int iLevel = entity_CalculateFullExpLevelSlow(pSrc->pEntParent);
//	
		//if(iLevel >= iCompareLevel)
			//*piRet = 1;
		//else
			//*piRet = 0;
		//return ExprFuncReturnFinished;
	//}
	//return ExprFuncReturnError;
//}
//AUTO_EXPR_FUNC(exprFuncListTreeEval) ACMD_NAME(PTClass);
//ExprFuncReturnVal ExprPTClass(ExprContext* pContext, ACMD_EXPR_INT_OUT piRet, const char* pchClass)
//{
	//Character *pSrc = GetSource(pContext);
	//CharacterClass *pClass = GET_REF(pSrc->hClass);
	//*piRet = 0;
//
	//if(pSrc)
	//{
		//if(!stricmp(pchClass,pClass->pchName))
			//*piRet = 1;
//
		//return ExprFuncReturnFinished;
	//}
	//return ExprFuncReturnError;
//}
//
//AUTO_EXPR_FUNC(exprFuncListTreeEval) ACMD_NAME(PTPower);
//ExprFuncReturnVal ExprPTPower(ExprContext* pContext, ACMD_EXPR_INT_OUT piRet, const char* pchNode, int require)
//{
	//Character *pSrc = GetSource(pContext);
	//*piRet = 0;
//
	//if(pSrc)
	//{
		//int i,c,count=0;
//
		//if(!pSrc->ppPowerTrees)
			//return ExprFuncReturnError;
//
		//for(i=eaSize(&pSrc->ppPowerTrees)-1;i>=0;i--)
		//{
			//PowerTreeDef *pTreeDef = GET_REF(pSrc->ppPowerTrees[i]->hDef);
			//for(c=eaSize(&pSrc->ppPowerTrees[i]->ppNodes)-1;c>=0;c--)
			//{
				//PTNode *pNode = pSrc->ppPowerTrees[i]->ppNodes[c];
				//PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
//
				//if ( pNode->bEscrow )
					//continue;
//
				//if(pNodeDef && !stricmp(pNodeDef->pchName,pchNode))
				//{
					//count = pNode->iRank + 1;
					//break;
				//}
			//}
		//}
//
		//if(count >= require)
			//*piRet = 1;
//
		//return ExprFuncReturnFinished;
//		
	//}
//
	//return ExprFuncReturnError;
//}
//
//AUTO_EXPR_FUNC(exprFuncListTreeEval) ACMD_NAME(PTGroup);
//ExprFuncReturnVal ExprPTGroup(ExprContext* pContext, ACMD_EXPR_INT_OUT piRet, const char* pchGroupName, int require)
//{
	//Character *pSrc = GetSource(pContext);
	//*piRet = 0;
//
	//if(pSrc)
	//{
		//int i,c;
		//int count = 0;
//
		//if(!pSrc->ppPowerTrees)
			//return ExprFuncReturnError;
//
		//for(i=eaSize(&pSrc->ppPowerTrees)-1;i>=0;i--)
		//{
			//const char *pchTreeName = REF_STRING_FROM_HANDLE(pSrc->ppPowerTrees[i]->hDef);
			//for(c=eaSize(&pSrc->ppPowerTrees[i]->ppNodes)-1;c>=0;c--)
			//{
				//PTNodeDef *pNodeDef = GET_REF(pSrc->ppPowerTrees[i]->ppNodes[c]->hDef);
//
				//if ( pSrc->ppPowerTrees[i]->ppNodes[c]->bEscrow )
					//continue;
//
				//if (pNodeDef)
				//{
					//const char *pchNodeName = pNodeDef->pchName;
					//char *pchFullName = NULL;
					//estrStackCreate(&pchFullName);
					//estrPrintf(&pchFullName,"%s.%s.%s",pchTreeName,pchGroupName,pchNodeName);
//
					//if(!stricmp(pchFullName,pNodeDef->pchNameFull))
					//{
						//count += pSrc->ppPowerTrees[i]->ppNodes[c]->iRank + 1;
					//}
//
					//estrDestroy(&pchFullName);
				//}
			//}
		//}
//
		//DBGPOWERTREE_printf("Total count for group %s: %d",pchGroupName,count);
		//require = count >= require ? 1 : 0;
//		
		//*piRet = require;
		//return ExprFuncReturnFinished;
	//}
	//*piRet = 0;
	//return ExprFuncReturnError;
//}

/*
ExprFuncDesc exprFuncTableTreeEval[] = {
	{ "PTgroup", ExprPTGroup, {MULTI_STRING,MULTI_FLOAT, 0}, MULTI_INT},
	{ "PTpower", ExprPTPower, {MULTI_STRING,MULTI_INT, 0}, MULTI_INT},
	{ "PTclass", ExprPTClass, {MULTI_STRING,0}, MULTI_INT},
	{ "PTlevel", ExprPTLevel, {MULTI_INT,0}, MULTI_INT},
	{ "PTorigin", ExprPTOrigin, {MULTI_STRING,0}, MULTI_INT}
};

AUTO_RUN;
int _PowerTreeRegisterFuncTable(void)
{
	exprRegisterFunctionTable(exprFuncTableTreeEval, ARRAY_SIZE(exprFuncTableTreeEval));
	return 1;
}
*/

typedef bool (*PowerTreeNodeFilterCallback)(const PTNodeDef *);

// Returns a PowerTree in the Character's PowerTrees by the specified tree name
SA_RET_OP_VALID PowerTree *character_FindTreeByDefName(SA_PARAM_OP_VALID const Character *pchar,
													SA_PARAM_OP_STR const char* pchTreeName)
{
	if (pchar)
	{
		const char* pchTreeNamePooled = allocFindString(pchTreeName);
		int i;
		for (i = eaSize(&pchar->ppPowerTrees)-1; i >= 0; i--)
		{
			if (pchTreeNamePooled == REF_STRING_FROM_HANDLE(pchar->ppPowerTrees[i]->hDef))
			{
				return pchar->ppPowerTrees[i];
			}
		}
	}
	return NULL;
}

// Returns a Power owned by the Character's PowerTrees, otherwise returns NULL.
Power *character_FindPowerByIDTree(const Character *pchar,
								   U32 uiID,
								   PowerTree **ppTreeOut,
								   PTNode **ppNodeOut)
{
	int i;
	for(i=eaSize(&(pchar)->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		PowerTree *ptree = (pchar)->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			int k;
			PTNode *pnode = ptree->ppNodes[j];
			for(k=eaSize(&pnode->ppPowers)-1; k>=0; k--)
			{
				if(uiID == pnode->ppPowers[k]->uiID)
				{
					if(ppTreeOut) *ppTreeOut = ptree;
					if(ppNodeOut) *ppNodeOut = pnode;
					return pnode->ppPowers[k];
				}
			}
		}
	}
	return NULL;
}

// Returns a Power owned by the Character's PowerTrees, otherwise returns NULL.
Power *character_FindPowerByDefTree(const Character *pchar,
									PowerDef *pdef,
									PowerTree **ppTreeOut,
									PTNode **ppNodeOut)
{
	int i;
	for(i=eaSize(&(pchar)->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		PowerTree *ptree = (pchar)->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			int k;
			PTNode *pnode = ptree->ppNodes[j];
			for(k=eaSize(&pnode->ppPowers)-1; k>=0; k--)
			{
				if(pdef == GET_REF(pnode->ppPowers[k]->hDef))
				{
					if(ppTreeOut) *ppTreeOut = ptree;
					if(ppNodeOut) *ppNodeOut = pnode;
					return pnode->ppPowers[k];
				}
			}
		}
	}
	return NULL;
}

// Returns true if the Character is allowed to add or remove an Enhancement level
int character_CanEnhanceNode(int iPartitionIdx,
							 Character *pchar,
							 const char *pchTree,
							 const char *pchNode,
							 const char *pchEnhancement,
							 int bAdd)
{
	int r = false;
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
	PTNodeEnhancementDef *pEnhDef = NULL;
	PowerDef *pdef = NULL;
	int i;

	// Yeah, removal not allowed, since none of the rest of this code checks for
	//  it and it's handled by respec anyway.
	if(!pNodeDef || !bAdd)
		return false;

	// Find the PTNodeEnhancementDef and its PowerDef
	for(i=eaSize(&pNodeDef->ppEnhancements)-1; i>=0; i--)
	{
		pdef = GET_REF(pNodeDef->ppEnhancements[i]->hPowerDef);
		if(pdef && !stricmp(pdef->pchName,pchEnhancement))
		{
			pEnhDef = pNodeDef->ppEnhancements[i];
			break;
		}
	}

	if(pEnhDef && entity_CanBuyPowerTreeEnhHelper(iPartitionIdx, CONTAINER_NOCONST(Entity, pchar->pEntParent),pNodeDef,pEnhDef))
		return true;
	else
		return false;
}

// Appends Enhancements from the Character's PowerTrees to the earray of Enhancements attached to the Power
void power_GetEnhancementsTree(const Character *pchar,
							   Power *ppow,
							   Power ***pppEnhancements)
{
	PowerDef *pdef = GET_REF(ppow->hDef);
	if(!pdef || pdef->eType!=kPowerType_Enhancement)
	{
		PTNode *pnode = NULL;
		Power *ppowParent = ppow->pParentPower ? ppow->pParentPower : ppow;
		if(character_FindPowerByIDTree(pchar,ppowParent->uiID,NULL,&pnode))
		{
			int i;
			for(i=eaSize(&pnode->ppEnhancements)-1; i>=0; i--)
			{
				eaPush(pppEnhancements,pnode->ppEnhancements[i]);
			}
		} 
	}
}



// Walks the Character's PowerTrees and adds all the available Powers to the general Powers list
bool character_AddPowersFromPowerTrees(int iPartitionIdx, Character *pchar, GameAccountDataExtract *pExtract)
{
	bool bSuccess = true;
	int i;
	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		int j;
		PowerTree *ptree = pchar->ppPowerTrees[i];
		for(j=eaSize(&ptree->ppNodes)-1; j>=0; j--)
		{
			int k,s;
			PTNode *pnode = ptree->ppNodes[j];
			PTNodeDef *pnodedef;
			U32 uiPowerSlotReplacementID = 0;

			s = eaSize(&pnode->ppPowers);

			if(s == 0)
				continue;

			if ( pnode->bEscrow ) 
				continue;

			pnodedef = GET_REF(pnode->hDef);
			if(!pnodedef)
			{
				bSuccess = false;
				continue;
			}

			if(IS_HANDLE_ACTIVE(pnodedef->hNodePowerSlot))
			{
				// The Powers here are expected to be slotted, but only if the Power from the specified
				//  node are slotted.  So we set that up here, but if we don't have those Powers, don't
				//  bother to add these Powers.
				PTNode *pnodeSlot = character_FindPowerTreeNode(pchar,GET_REF(pnodedef->hNodePowerSlot),NULL);
				if(!pnodeSlot || pnodeSlot->bEscrow)
					continue;
				for(k=eaSize(&pnodeSlot->ppPowers)-1; k>=0; k--)
				{
					PowerDef *pdef = pnodeSlot->ppPowers[k] ? GET_REF(pnodeSlot->ppPowers[k]->hDef) : NULL;
					if(pdef && pdef->eType!=kPowerType_Enhancement)
					{
						uiPowerSlotReplacementID = pnodeSlot->ppPowers[k]->uiID;
						break;
					}
				}
			}
			
			if(pnode->ppPowers[0])
			{
				PowerDef *pdef = GET_REF(pnode->ppPowers[0]->hDef);

				//If the first node is an enhancement, then all the nodes are enhancements, and you should
				//only add the last power
				if(pdef && pdef->eType == kPowerType_Enhancement)
				{
					pnode->ppPowers[s-1]->uiPowerSlotReplacementID = uiPowerSlotReplacementID;
					character_AddPower(iPartitionIdx,pchar,pnode->ppPowers[s-1],kPowerSource_PowerTree,pExtract);
					continue;
				}
			}

			for(k=s-1; k>=0; k--)
			{
				PowerDef *pdef = pnode->ppPowers[k] ? GET_REF(pnode->ppPowers[k]->hDef) : NULL;

				if(!pnode->ppPowers[k])
					continue;

				if (!pdef)
				{
					bSuccess = false;
					continue;
				}

				pnode->ppPowers[k]->uiPowerSlotReplacementID = uiPowerSlotReplacementID;
				character_AddPower(iPartitionIdx,pchar,pnode->ppPowers[k],kPowerSource_PowerTree,pExtract);
				// If the Power we just added isn't an Enhancement, we're done with this node
				if(!pdef || pdef->eType!=kPowerType_Enhancement)
				{
					break;
				}
			}
		}
	}
	return bSuccess;
}

//-----------------Top Down Functions-------------------//
static PTNodeTopDown *PTTopDown_FillNode(PTNodeDef *pNode,PowerTreeDef *pTree, S32 iDepth, S32 iCount)
{
	PTNodeTopDown *pReturn = StructCreate(parse_PTNodeTopDown);
	int i,c;
	pReturn->iDepth = iDepth;

	SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict,pNode->pchNameFull,pReturn->hNode);

	for(i=eaSize(&pTree->ppGroups)-1;i>=0;i--)
	{
		for(c=eaSize(&pTree->ppGroups[i]->ppNodes)-1;c>=0;c--)
		{
			if(GET_REF(pTree->ppGroups[i]->ppNodes[c]->hNodeRequire) == pNode)
			{
				eaPush(&pReturn->ppNodes,PTTopDown_FillNode(pTree->ppGroups[i]->ppNodes[c],pTree,iDepth + 1, iCount));
				pReturn->ppNodes[eaSize(&pReturn->ppNodes)-1]->iCount = iCount;
				iCount++;
			}
		}
	}

	return pReturn;
}

int SortGroupList(const PTGroupTopDown **pGroupA, const PTGroupTopDown **pGroupB, const void *pContext)
{
	PTGroupDef *pGroupDefA = GET_REF((*pGroupA)->hGroup);
	PTGroupDef *pGroupDefB = GET_REF((*pGroupB)->hGroup);

	if(!pGroupDefA || !pGroupDefB)
		return 0;

	if(!pGroupDefA->pRequires || !pGroupDefB->pRequires)
		return 0;
	//Make sure the ones with a lower level restriction are on top
	if(pGroupDefA->pRequires->iTableLevel > pGroupDefB->pRequires->iTableLevel)
		return 1;
	else if(pGroupDefA->pRequires->iTableLevel < pGroupDefB->pRequires->iTableLevel)
		return -1;

	if(pGroupDefA->pRequires->iGroupRequired > pGroupDefB->pRequires->iGroupRequired)
		return 1;
	else if(pGroupDefA->pRequires->iGroupRequired < pGroupDefB->pRequires->iGroupRequired)
		return -1;

	if (pGroupDefA->iOrder > pGroupDefB->iOrder)
		return 1;
	else if (pGroupDefA->iOrder < pGroupDefB->iOrder)
		return -1;

	return 0;
}

static PTGroupTopDown *PTTopDown_FillGroup(PTGroupDef *pGroup, PowerTreeDef *pTree, S32 iDepth)
{
	PTGroupTopDown *pReturn = StructCreate(parse_PTGroupTopDown);
	int i,c,x;
	int iCountG=0;
	int iCountN=0;
	pReturn->iDepth = iDepth;

	SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pGroup->pchNameFull,pReturn->hGroup);

	for(i=eaSize(&pTree->ppGroups)-1;i>=0;i--)
	{
		if(GET_REF(pTree->ppGroups[i]->pRequires->hGroup) == pGroup)
		{
			eaPush(&pReturn->ppGroups,PTTopDown_FillGroup(pTree->ppGroups[i],pTree,iDepth + 1));
			pReturn->ppGroups[iCountG]->iCount = iCountG;
			iCountG++;
		}
		for(c=eaSize(&pTree->ppGroups[i]->ppNodes)-1;c>=0;c--)
		{
			for(x=eaSize(&pTree->ppGroups[i]->ppNodes[c]->ppRanks)-1;x>=0;x--)
			{
				if(GET_REF(pTree->ppGroups[i]->ppNodes[c]->ppRanks[x]->pRequires->hGroup) == pGroup)
				{
					ErrorFilenamef(pTree->pchFile, "Group %s has a locked but unowned node %s; such trees cannot be rebuilt top-down",
						pGroup->pchNameFull, pTree->ppGroups[i]->ppNodes[c]->pchName);
				}
			}
		}
	}

	eaStableSort(pReturn->ppGroups, NULL,SortGroupList);

	for(i=eaSize(&pGroup->ppNodes)-1;i>=0;i--)
	{
		if(!IS_HANDLE_ACTIVE(pGroup->ppNodes[i]->hNodeRequire))
		{
			eaPush(&pReturn->ppOwnedNodes,PTTopDown_FillNode(pGroup->ppNodes[i],pTree,0,iCountN));
			pReturn->ppOwnedNodes[iCountN]->iCount = iCountN;
			iCountN++;
		}
	}

	return pReturn;
}

static void PTTopDown_FindDimentions(PTGroupTopDown *pGroupTopDown, int *iWidth, int *iHeight)
{
	PTGroupDef *pGroup = GET_REF(pGroupTopDown->hGroup);

	if(pGroup)
	{
		int iWidthTemp,iHeightTemp =0;
		int i;
		
		iWidthTemp = pGroup->x + (eaSize(&pGroupTopDown->ppOwnedNodes) + 1) * 65;
		iHeightTemp = pGroup->y + 80; 

		for(i=0;i<eaSize(&pGroupTopDown->ppGroups);i++)
			PTTopDown_FindDimentions(pGroupTopDown->ppGroups[i],iWidth,iHeight);

		*iWidth = MAX(iWidthTemp,*iWidth);
		*iHeight = MAX(iHeightTemp,*iHeight);
	}
}

static void PTTopDown_FillTree(PowerTreeTopDown *pTree)
{
	PowerTreeDef *pDef = GET_REF(pTree->hTree);
	int i;

	if (!pDef)
		return;

	//Look for all the top groups
	for(i=eaSize(&pDef->ppGroups)-1;i>=0;i--)
	{
		if(IS_HANDLE_ACTIVE(pDef->ppGroups[i]->pRequires->hGroup))
			continue;

		eaPush(&pTree->ppGroups,PTTopDown_FillGroup(pDef->ppGroups[i],pDef, 0));

		PTTopDown_FindDimentions(pTree->ppGroups[eaSize(&pTree->ppGroups)-1],&pTree->iWidth,&pTree->iHeight);
	}

	eaStableSort(pTree->ppGroups, NULL,SortGroupList);

	
}

void powertree_DestroyTopDown(PowerTreeTopDown *pTree)
{
	StructDestroy(parse_PowerTreeTopDown,pTree);
}

static const char *GetEmptyString(S32 iLength)
{
	static char ach[1000];
	if (!devassertmsg(iLength < ARRAY_SIZE_CHECKED(ach), "Length is too big, power tree is too complicated"))
		iLength = 0;
	memset(ach, ' ', iLength);
	ach[iLength] = '\0';
	return ach;
}

static void powertree_PrintTopDownNode(PTNodeTopDown *pNode, S32 iDepth);

static void powertree_PrintTopDownGroup(PTGroupTopDown *pGroup, S32 iDepth)
{
	PTGroupDef *pGroupDef = GET_REF(pGroup->hGroup);
	const char *pchName = pGroupDef ? pGroupDef->pchNameFull : NULL;
	S32 iMax = pGroupDef ? pGroupDef->iMax : -1;
	S32 i;
	printf("%sGroup: %s (Max %d)\n", GetEmptyString(iDepth), pchName, iMax);
	printf("%sCHILD GROUPS\n", GetEmptyString(iDepth));
	for (i = 0; i < eaSize(&pGroup->ppGroups); i++)
		powertree_PrintTopDownGroup(pGroup->ppGroups[i], iDepth + 1);
	printf("%sOWNED NODES\n", GetEmptyString(iDepth));
	for (i = 0; i < eaSize(&pGroup->ppOwnedNodes); i++)
		powertree_PrintTopDownNode(pGroup->ppOwnedNodes[i], iDepth + 1);
}

static void powertree_PrintTopDownNode(PTNodeTopDown *pNode, S32 iDepth)
{
	PTNodeDef *pNodeDef = GET_REF(pNode->hNode);
	const char *pchName = pNodeDef ? pNodeDef->pchNameFull : NULL;
	S32 i;
	printf("%sNode: %s\n", GetEmptyString(iDepth), pchName);
	printf("%sCHILD NODES\n", GetEmptyString(iDepth));
	for (i = 0; i < eaSize(&pNode->ppNodes); i++)
		powertree_PrintTopDownNode(pNode->ppNodes[i], iDepth + 1);
}

void powertree_PrintTopDown(PowerTreeTopDown *pTree)
{
	PowerTreeDef *pTreeDef = GET_REF(pTree->hTree);
	S32 i;
	if (!pTreeDef)
		return;
	printf("POWER TREE: %s\n", pTreeDef ? pTreeDef->pchName : NULL);
	for (i = 0; i < eaSize(&pTree->ppGroups); i++)
		powertree_PrintTopDownGroup(pTree->ppGroups[i], 1);
}

PowerTreeTopDown *powertree_GetTopDown(PowerTreeDef *pTree)
{
	if (pTree)
	{
		PowerTreeTopDown *pReturn = StructCreate(parse_PowerTreeTopDown);

		SET_HANDLE_FROM_STRING(g_hPowerTreeDefDict,pTree->pchName,pReturn->hTree);

		PTTopDown_FillTree(pReturn);

		return pReturn;
	}
	else
		return NULL;
}

AUTO_TRANS_HELPER;
NOCONST(PTNode)* character_FindPowerTreeNodeHelper(ATH_ARG NOCONST(Character)* pChar, NOCONST(PowerTree)** ppTreeOut, const char *pchNode)
{
	S32 i;
	S32 n;
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
	PTNodeDef *pCloneNode;

	if (!pNodeDef)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	pCloneNode = GET_REF(pNodeDef->hNodeClone);

	for(n=eaSize(&pChar->ppPowerTrees)-1; n>=0; n--)
	{
		for(i=eaSize(&pChar->ppPowerTrees[n]->ppNodes)-1; i>=0; i--)
		{
			PTNodeDef *pRefDef = GET_REF(pChar->ppPowerTrees[n]->ppNodes[i]->hDef);
			if(pRefDef)
			{
				if(pCloneNode)
				{
					if(pRefDef == pCloneNode || GET_REF(pRefDef->hNodeClone) == pCloneNode)
					{
						if(ppTreeOut)
							*ppTreeOut = pChar->ppPowerTrees[n];
						PERFINFO_AUTO_STOP();
						return pChar->ppPowerTrees[n]->ppNodes[i];
					}
				}
				else
				{
					if(pRefDef == pNodeDef || GET_REF(pNodeDef->hNodeClone) == pRefDef)
					{
						if(ppTreeOut)
							*ppTreeOut = pChar->ppPowerTrees[n];
						PERFINFO_AUTO_STOP();
						return pChar->ppPowerTrees[n]->ppNodes[i];
					}
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return NULL;
}

PTNode *powertree_FindNode(Character *pChar, PowerTree **ppTreeOut, const char *pchNode)
{
	return (PTNode*)character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pChar), (NOCONST(PowerTree)**) ppTreeOut, pchNode);
}

S32 character_CountOwnedInGroup(Character *pChar, PTGroupDef *pGroup)
{
	S32 iCount = 0;
	S32 i;
	for (i = 0; i < eaSize(&pGroup->ppNodes); i++)
	{
		PTNodeDef *pNode = pGroup->ppNodes[i];
		if (pNode && character_GetNode(pChar, pNode->pchNameFull))
			iCount++;
	}
	return iCount;
}

AUTO_TRANS_HELPER;
bool powertree_CountOwnedInGroupHelper(ATH_ARG NOCONST(PowerTree)* pTree, PTGroupDef* pGroup)
{
	int i, iCount=0;

	for(i=0;i<eaSize(&pTree->ppNodes);i++)
	{
		PTNodeDef *pDef = GET_REF(pTree->ppNodes[i]->hDef);
		if (pDef && strStartsWith(pDef->pchNameFull, pGroup->pchNameFull))
		{	
			iCount++;
		}
	}
	return iCount;
}

// Makes sure all Powers in the Character's PowerTrees are properly ranked and scaled
void character_PowerTreesFixup(Character *pchar)
{
	int i,j,k;

	if (pchar->pClientPowerTreeInfo)
	{
		eaClearStruct(&pchar->pClientPowerTreeInfo->ppTrees, parse_PowerTreeClientInfo);
		entity_SetDirtyBit(pchar->pEntParent, parse_PowerTreeClientInfoList, pchar->pClientPowerTreeInfo, false);
	}

	entity_SetDirtyBit(pchar->pEntParent, parse_Character, pchar, false);
	for(i=eaSize(&pchar->ppPowerTrees)-1; i>=0; i--)
	{
		PowerTreeDef *pTreeDef = GET_REF(pchar->ppPowerTrees[i]->hDef);
		if(pTreeDef)
		{
			PTTypeDef *pTypeDef = GET_REF(pTreeDef->hTreeType);
			F32 fTableScale = pTypeDef ? pTypeDef->fTableScale : 1;
			for(j=eaSize(&pchar->ppPowerTrees[i]->ppNodes)-1; j>=0; j--)
			{
				PTNode *pNode = pchar->ppPowerTrees[i]->ppNodes[j];
				for(k=eaSize(&pNode->ppPowers)-1; k>=0; k--)
				{
					pNode->ppPowers[k]->fTableScale = fTableScale;
					pNode->ppPowers[k]->iIdxMultiTable = pNode->iRank;
				}
			}

			if (pTreeDef->bSendToClient)
			{
				PowerTreeClientInfo *pPowerTreeClientInfo = StructCreate(parse_PowerTreeClientInfo);
				if (pchar->pClientPowerTreeInfo == NULL)
				{
					pchar->pClientPowerTreeInfo = StructCreate(parse_PowerTreeClientInfoList);
					entity_SetDirtyBit(pchar->pEntParent, parse_PowerTreeClientInfoList, pchar->pClientPowerTreeInfo, false);
				}
				pPowerTreeClientInfo->pchName = allocAddString(pTreeDef->pchName);
				COPY_HANDLE(pPowerTreeClientInfo->hDisplayMessage, pTreeDef->pDisplayMessage.hMessage);
				eaPush(&pchar->pClientPowerTreeInfo->ppTrees, pPowerTreeClientInfo);
			}
		}		
	}
}

bool PowerTree_GetAllAvailableNodes(int iPartitionIdx, Character *pChar, PTNodeDef ***pppNodesOut)
{
	int t,g,n;

	eaClear(pppNodesOut);

	for(t=0;t<eaSize(&pChar->ppPowerTrees);t++)
	{
		PowerTreeDef *pTreeDef = GET_REF(pChar->ppPowerTrees[t]->hDef);

		for(g=0;g<eaSize(&pTreeDef->ppGroups);g++)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[g];

			for(n=0;n<eaSize(&pGroupDef->ppNodes);n++)
			{
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[n];

				if(character_CanBuyPowerTreeNodeNextRank(iPartitionIdx,pChar,pGroupDef,pNodeDef))
				{
					eaPush(pppNodesOut,pNodeDef);
				}
			}
		}
	}

	if(eaSize(pppNodesOut) > 0)
		return true;
	else
		return false;
}

//if NULL, returns true if the character has any trainer nodes
bool powertree_CharacterHasTrainerUnlockNode(Character* pChar, PTNodeDef* pFindNodeDef)
{
	S32 i, j, k;

	if (pChar==NULL)
		return false;

	for (i = 0; i < eaSize( &pChar->ppPowerTrees ); i++)
	{
		for (j = 0; j < eaSize( &pChar->ppPowerTrees[i]->ppNodes ); j++)
		{
			PTNode* pPTNode = pChar->ppPowerTrees[i]->ppNodes[j];
			PTNodeDef* pNodeDef = GET_REF(pPTNode->hDef);
			S32 iRankSearchSize = pNodeDef ? MIN(pPTNode->iRank+1, eaSize(&pNodeDef->ppRanks)) : 0;
			for (k = 0; k < iRankSearchSize; k++)
			{
				PTNodeDef* pTrainerUnlockNodeDef = GET_REF(pNodeDef->ppRanks[k]->hTrainerUnlockNode);
				if (pTrainerUnlockNodeDef==NULL)
					continue;
				if (pFindNodeDef==NULL || pFindNodeDef == pTrainerUnlockNodeDef)
					return true;
			}
		}
	}
	return false;
}

void powertree_CharacterGetTrainerUnlockNodes(Character* pChar, PTNodeDef ***pppNodesOut)
{
	S32 i, j, k;

	eaClear(pppNodesOut);

	if (pChar==NULL)
		return;

	for (i = 0; i < eaSize( &pChar->ppPowerTrees ); i++)
	{
		for (j = 0; j < eaSize( &pChar->ppPowerTrees[i]->ppNodes ); j++)
		{
			PTNode* pPTNode = pChar->ppPowerTrees[i]->ppNodes[j];
			PTNodeDef* pNodeDef = GET_REF(pPTNode->hDef);
			S32 iRankSearchSize = pNodeDef ? MIN(pPTNode->iRank+1, eaSize(&pNodeDef->ppRanks)) : 0;
			for (k = 0; k < iRankSearchSize; k++)
			{
				PTNodeDef* pTrainerUnlockNodeDef = GET_REF(pNodeDef->ppRanks[k]->hTrainerUnlockNode);

				if ( pTrainerUnlockNodeDef )
				{
					eaPush(pppNodesOut, pTrainerUnlockNodeDef);
				}
			}
		}
	}
}

bool powertree_NodeHasPropagationPowers(PTNodeDef* pNodeDef)
{
	int i;
	if ( pNodeDef==NULL )
		return false;
	for (i=eaSize(&pNodeDef->ppRanks)-1;i>=0;i--)
	{
		PowerDef *pDef = GET_REF(pNodeDef->ppRanks[i]->hPowerDef);

		if ( pDef && pDef->powerProp.bPropPower )
			return true;
	}
	return false;
}

void powertree_FindLevelRequirements(PowerTreeDef *pDef, S32 **peaiLevels)
{
	S32 i, n;
	for(i=0;i<eaSize(&pDef->ppGroups);i++)
	{
		for(n=0;n<eaSize(&pDef->ppGroups[i]->ppNodes);n++)
		{
			PTNodeDef *pNode = pDef->ppGroups[i]->ppNodes[n];

			if(eaSize(&pNode->ppRanks))
			{
				int r;
				ea32PushUnique(peaiLevels,pNode->ppRanks[0]->pRequires->iTableLevel);
				for(r=1;r<eaSize(&pNode->ppRanks);r++)
				{
					if(pNode->ppRanks[r]->pRequires->iTableLevel > pNode->ppRanks[0]->pRequires->iTableLevel)
						ea32PushUnique(peaiLevels,pNode->ppRanks[r]->pRequires->iTableLevel);
				}
			}
			else
				ea32PushUnique(peaiLevels,0);
		}
	}
}

bool powertree_GroupNameFromNode(PTNode *pNode, char **ppchGroup)
{
	const char *pchNodeName = pNode ? REF_STRING_FROM_HANDLE(pNode->hDef) : NULL;
	char *pchSep;

	if (pchNodeName)
	{
		ANALYSIS_ASSUME(pchNodeName != NULL); 
		if (pchSep = strrchr(pchNodeName, '.'))
		{
			estrConcat(ppchGroup, pchNodeName, pchSep - pchNodeName);
			return true;
		}
	}

	return false;
}

bool powertree_GroupNameFromNodeDef(PTNodeDef *pNodeDef, char **ppchGroup)
{
	const char *pchNodeName = pNodeDef ? pNodeDef->pchNameFull : NULL;
	char *pchSep;

	if (pchNodeName && (pchSep = strrchr(pchNodeName, '.')))
	{
		estrConcat(ppchGroup, pchNodeName, pchSep - pchNodeName);
		return true;
	}
	else
		return false;
}

bool powertree_TreeNameFromNodeDefName(const char *pchNodeName, char **ppchTree)
{
	char *pchSep;

	if (pchNodeName && (pchSep = strchr(pchNodeName, '.')))
	{
		estrConcat(ppchTree, pchNodeName, pchSep - pchNodeName);
		return true;
	}
	else
		return false;
}

bool powertree_TreeNameFromNodeDef(PTNodeDef *pNodeDef, char **ppchTree)
{
	const char *pchNodeName = pNodeDef ? pNodeDef->pchNameFull : NULL;

	return powertree_TreeNameFromNodeDefName(pchNodeName, ppchTree);
}

bool powertree_TreeNameFromGroupDef(PTGroupDef *pGroupDef, char **ppchTree)
{
	const char *pchGroupName = pGroupDef ? pGroupDef->pchNameFull : NULL;
	char *pchSep;

	if (pchGroupName && (pchSep = strchr(pchGroupName, '.')))
	{
		estrConcat(ppchTree, pchGroupName, pchSep - pchGroupName);
		return true;
	}
	else
		return false;
}

PTGroupDef *powertree_GroupDefFromNode(PTNode *pNode)
{
	PTGroupDef *pDef = NULL;
	char *pch = NULL;
	estrStackCreate(&pch);
	if (powertree_GroupNameFromNode(pNode, &pch))
		pDef = RefSystem_ReferentFromString(g_hPowerTreeGroupDefDict, pch);
	estrDestroy(&pch);
	return pDef;
}

PTGroupDef *powertree_GroupDefFromNodeDef(PTNodeDef *pNodeDef)
{
	PTGroupDef *pDef = NULL;
	char *pch = NULL;
	estrStackCreate(&pch);
	if (powertree_GroupNameFromNodeDef(pNodeDef, &pch))
		pDef = RefSystem_ReferentFromString(g_hPowerTreeGroupDefDict, pch);
	estrDestroy(&pch);
	return pDef;
}

PowerTreeDef *powertree_TreeDefFromNodeDef(PTNodeDef *pNodeDef)
{
	PowerTreeDef *pDef = NULL;
	char *pch = NULL;
	estrStackCreate(&pch);
	if (powertree_TreeNameFromNodeDef(pNodeDef, &pch))
		pDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pch);
	estrDestroy(&pch);
	return pDef;
}

PowerTreeDef *powertree_TreeDefFromGroupDef(PTGroupDef *pGroupDef)
{
	PowerTreeDef *pDef = NULL;
	char *pch = NULL;
	estrStackCreate(&pch);
	if (powertree_TreeNameFromGroupDef(pGroupDef, &pch))
		pDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pch);
	estrDestroy(&pch);
	return pDef;
}

// Compares two PTNodes by their current rank's power purpose
S32 ComparePTNodesByPurpose(const PTNode** a, const PTNode** b)
{
	if (a && *a && b && *b)
	{
		PTNodeDef *pNodeDefA = GET_REF((*a)->hDef);
		PowerDef *pPowerDefA = pNodeDefA ? GET_REF(pNodeDefA->ppRanks[(*a)->iRank]->hPowerDef) : NULL;
		PTNodeDef *pNodeDefB = GET_REF((*b)->hDef);
		PowerDef *pPowerDefB = pNodeDefB ? GET_REF(pNodeDefB->ppRanks[(*b)->iRank]->hPowerDef) : NULL;

		if(pPowerDefA && pPowerDefB) {
			if (pPowerDefA->ePurpose < pPowerDefB->ePurpose)
			{
				return -1;
			}
			else if (pPowerDefA->ePurpose > pPowerDefB->ePurpose)
			{
				return 1;
			}
		}
	}
	return 0;
}

// Returns the cost table of the first rank in the node definition
SA_RET_OP_VALID const char * powertree_CostTableOfFirstRankFromNodeDef(SA_PARAM_NN_VALID const PTNodeDef *pNodeDef)
{
	devassert(pNodeDef);

	if (pNodeDef == NULL)
		return NULL;

	if (eaSize(&pNodeDef->ppRanks) <= 0)
		return NULL;

	return pNodeDef->ppRanks[0]->pchCostTable;
}

#include "AutoGen/PowerTree_h_ast.c"
