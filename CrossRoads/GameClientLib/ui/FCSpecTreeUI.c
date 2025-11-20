#include "earray.h"
#include "UIGen.h"
#include "PowersUI.h"
#include "PowerGrid.h"
#include "Powers.h"
#include "PowerTreeHelpers.h"
#include "Entity.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CombatEval.h"

#include "AutoGen/PowerGrid_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/FCSpecTreeUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_STRUCT;
typedef struct SpecTreeData
{
	PowerTree *pPowerTree;			AST(UNOWNED)
	PowerTreeDef *pPowerTreeDef;	AST(UNOWNED)
} SpecTreeData;

static PTNode* SpecTree_GetOwnedNode(SA_PARAM_OP_VALID PowerTree *pTree, PTNodeDef *pNodeDef)
{
	int i;
	if (pTree && pNodeDef)
	{
		for (i = 0; i < eaSize(&pTree->ppNodes); i++)
		{
			PTNode* pNode = pTree->ppNodes[i];
			if (GET_REF(pNode->hDef) == pNodeDef)
			{
				return pNode;
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_Init");
void SpecTree_Init(SA_PARAM_OP_VALID UIGen* pGen, const char* pchTrees)
{
	if (pGen)
	{
		PowerTreeDefRef ***peaTrees = ui_GenGetManagedListSafe(pGen, PowerTreeDefRef);
		char *pchNameCopy;
		char *pchStart;
		char *pchContext;
		S32 iIndex = 0;
		
		strdup_alloca(pchNameCopy, pchTrees);
		pchStart = strtok_r(pchNameCopy, " ,\t\r\n", &pchContext);
		if (pchStart) 
		{
			do
			{
				PowerTreeDefRef *pDefRef = eaGetStruct(peaTrees, parse_PowerTreeDefRef, iIndex++);
				SET_HANDLE_FROM_STRING("PowerTreeDef", pchStart, pDefRef->hRef);

			} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
		}
		ui_GenSetManagedListSafe(pGen, peaTrees, PowerTreeDefRef, 1);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetDataNewSpecTreeData");
void GenSetDataNewSpecTreeData(SA_PARAM_OP_VALID UIGen* pGen)
{
	if (pGen)
	{
		ui_GenSetManagedPointer(pGen, StructCreate(parse_SpecTreeData), parse_SpecTreeData, true);
	}
}

static S32 SpecTree_PowerTreeTimeComparitor(const PowerTree **ppA, const PowerTree **ppB)
{
	int iOrderA = INT_MAX;
	int iOrderB = INT_MAX;

	if ((*ppA)->uiTimeCreated != (*ppB)->uiTimeCreated)
	{
		return (*ppA)->uiTimeCreated - (*ppB)->uiTimeCreated;
	}

	if (eaSize(&(*ppA)->ppNodes) > 0 && eaSize(&(*ppA)->ppNodes[0]->ppPurchaseTracker) > 0)
	{
		iOrderA = (*ppA)->ppNodes[0]->ppPurchaseTracker[0]->uiOrderCreated;
	}

	if (eaSize(&(*ppB)->ppNodes) > 0 && eaSize(&(*ppB)->ppNodes[0]->ppPurchaseTracker) > 0)
	{
		iOrderB = (*ppB)->ppNodes[0]->ppPurchaseTracker[0]->uiOrderCreated;
	}

	return iOrderA - iOrderB;
}

static PowerTree *SpecTree_GetOwnedPowerTreeByIndex(SA_PARAM_OP_VALID Entity* pEnt, const char* pchUICategory, int iNthTree)
{
	if (pEnt && pEnt->pChar && pchUICategory && pchUICategory[0])
	{
		Character *pChar = pEnt->pChar;
		int eUICategory = StaticDefineIntGetInt(PowerTreeUICategoryEnum, pchUICategory);
		int i;
		static PowerTree **eaPowerTrees = NULL;
		eaCopy(&eaPowerTrees, &pChar->ppPowerTrees);
		eaQSort(eaPowerTrees, SpecTree_PowerTreeTimeComparitor);
		for (i = 0; i < eaSize(&eaPowerTrees); i++)
		{
			PowerTree *pTree = eaPowerTrees[i];
			PowerTreeDef *pDef = GET_REF(pTree->hDef);
			if (pDef->eUICategory == eUICategory)
			{
				if (iNthTree-- == 0)
				{
					return pTree;
				}
			}
		}
	}
	return NULL;
}

int SpecTree_PowerTreeDefDisplayNameComparitor(const PowerTreeDef **ppDefA, const PowerTreeDef **ppDefB)
{
	return stricmp(TranslateDisplayMessage((*ppDefA)->pDisplayMessage), TranslateDisplayMessage((*ppDefB)->pDisplayMessage));
}

static PowerTreeDef *SpecTree_GetUnownedPowerTreeByIndex(SA_PARAM_OP_VALID Entity* pEnt, const char* pchUICategory, int iIndex)
{
	if (pEnt && pEnt->pChar && pchUICategory && pchUICategory[0])
	{
		Character *pChar = pEnt->pChar;
		int eUICategory = StaticDefineIntGetInt(PowerTreeUICategoryEnum, pchUICategory);
		int i;
		RefDictIterator iter;
		PowerTreeDef* pDef = NULL;
		static PowerTreeDef** eaPowerTreeDefs = NULL;
		eaClearFast(&eaPowerTreeDefs);
		RefSystem_InitRefDictIterator(g_hPowerTreeDefDict, &iter);
		while (pDef = RefSystem_GetNextReferentFromIterator(&iter))
		{
			if (pDef->eUICategory == eUICategory)
			{
				bool bFound = false;
				for (i = 0; i < eaSize(&pChar->ppPowerTrees) && !bFound; i++)
				{
					PowerTree *pTree = pChar->ppPowerTrees[i];
					PowerTreeDef *pOtherDef = GET_REF(pTree->hDef);
					bFound = (pDef == pOtherDef);
				}
				if (!bFound)
					eaPush(&eaPowerTreeDefs, pDef);
			}
		}
		eaQSort(eaPowerTreeDefs, SpecTree_PowerTreeDefDisplayNameComparitor);
		if (eaSize(&eaPowerTreeDefs))
		{
			iIndex %= eaSize(&eaPowerTreeDefs);
			if (iIndex < 0)
				iIndex += eaSize(&eaPowerTreeDefs);		
			return eaPowerTreeDefs[iIndex];
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTreeData_SetPowerTreeByIndex");
void SpecTreeData_SetPowerTreeByIndex(SA_PARAM_OP_VALID SpecTreeData *pData, SA_PARAM_OP_VALID Entity* pEnt, const char* pchUICategory, int iNthTree, int iDisplayIndex)
{
	if (pData)
	{
		pData->pPowerTree = SpecTree_GetOwnedPowerTreeByIndex(pEnt, pchUICategory, iNthTree);
		if (pData->pPowerTree)
		{
			pData->pPowerTreeDef = GET_REF(pData->pPowerTree->hDef);
		}
		else
		{
			pData->pPowerTreeDef = SpecTree_GetUnownedPowerTreeByIndex(pEnt, pchUICategory, iDisplayIndex);
		}
	}
}

PowerTree* Character_GetPowerTreeFromDef(Character *pChar, PowerTreeDef *pTreeDef)
{
	if (pChar && pTreeDef)
	{
		int i;
		for (i = 0; i < eaSize(&pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = pChar->ppPowerTrees[i];
			if (GET_REF(pTree->hDef) == pTreeDef)
				return pTree;
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTreeData_GetNumTreesInPath");
int SpecTreeData_GetNumTreesInPath(SA_PARAM_OP_VALID Entity* pEnt, const char* pchPoints, int iNthTree)
{
	CharacterPath *pPath = SAFE_GET_REF2(pEnt, pChar, hPath);
	if (pPath && pchPoints)
	{
		int i;
		for (i = 0; i < eaSize(&pPath->eaSuggestedPurchases); i++)
		{
			CharacterPathSuggestedPurchase *pSuggestion = pPath->eaSuggestedPurchases[i];
			if (stricmp(pSuggestion->pchPowerTable, pchPoints) == 0)
			{
				CharacterPathChoice *pChoice = eaGet(&pSuggestion->eaChoices, iNthTree);
				return pChoice ? eaSize(&pChoice->eaPowerTreeDefs) : 0;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTreeData_SetPowerTreeFromPath");
void SpecTreeData_SetPowerTreeFromPath(SA_PARAM_OP_VALID SpecTreeData *pData, SA_PARAM_OP_VALID Entity* pEnt, const char* pchPoints, int iNthTree, int iDisplayIndex)
{
	CharacterPath *pPath = SAFE_GET_REF2(pEnt, pChar, hPath);
	if (pData && pPath && pchPoints)
	{
		int i;
		for (i = 0; i < eaSize(&pPath->eaSuggestedPurchases); i++)
		{
			CharacterPathSuggestedPurchase *pSuggestion = pPath->eaSuggestedPurchases[i];
			if (stricmp(pSuggestion->pchPowerTable, pchPoints) == 0)
			{
				CharacterPathChoice *pChoice = eaGet(&pSuggestion->eaChoices, iNthTree);
				int iSize = pChoice ? eaSize(&pChoice->eaPowerTreeDefs) : 0;
				if (iSize > 0)
				{
					int iIndex = iDisplayIndex % iSize;
					PowerTreeDefRef *pDefRef = NULL;
					if (iIndex < 0)
						iIndex += iSize;
					pDefRef = eaGet(&pChoice->eaPowerTreeDefs, iIndex);
					pData->pPowerTreeDef = SAFE_GET_REF(pDefRef, hRef);
					pData->pPowerTree = Character_GetPowerTreeFromDef(pEnt->pChar, pData->pPowerTreeDef);
					return;
				}
			}
		}
	}
}

static S32 SpecTree_PTGroupDefComparitor(const PTGroupDef **ppGroupA, const PTGroupDef **ppGroupB)
{
	if (*ppGroupA && *ppGroupB)
		return stricmp((*ppGroupA)->pchGroup, (*ppGroupB)->pchGroup);
	else
	{
		return ppGroupA - ppGroupB;
	}
}

static S32 SpecTree_PowerListNodeGroupComparitor(const PowerListNode **ppA, const PowerListNode **ppB)
{
	PTGroupDef *pGroupA = GET_REF((*ppA)->hGroupDef);
	PTGroupDef *pGroupB = GET_REF((*ppB)->hGroupDef);
	return SpecTree_PTGroupDefComparitor(&pGroupA, &pGroupB);
}

static void SpecTree_GetPowerListNodesEx(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, SA_PARAM_OP_VALID PowerTree *pTree, SA_PARAM_OP_VALID PowerTreeDef *pTreeDef, int iMax)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iCount = 0;
	if (pGen && pTreeDef)
	{
		int i;
		for (i = 0; i < eaSize(&pTreeDef->ppGroups) && iCount < iMax; i++)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
			int j;
			for (j = 0; j < eaSize(&pGroupDef->ppNodes) && iCount < iMax; j++)
			{
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
				FillPowerListNodeForEnt(
					pEnt,
					pFakeEnt,
					eaGetStruct(peaNodes, parse_PowerListNode, iCount++),
					pTree, 
					pTreeDef,
					NULL, 
					pGroupDef,
					SpecTree_GetOwnedNode(pTree, pNodeDef), 
					pNodeDef);
			}
		}
	}
	eaSetSizeStruct(peaNodes, parse_PowerListNode, iCount);
	eaQSort(*peaNodes, SpecTree_PowerListNodeGroupComparitor);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_GetPowerListNodesByPowerTreeDef");
void SpecTree_GetPowerListNodesByTreeDef(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, SA_PARAM_OP_VALID PowerTreeDef *pTreeDef, int iMax)
{
	SpecTree_GetPowerListNodesEx(pGen, pEnt, pFakeEnt, NULL, pTreeDef, iMax);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_GetPowerListNodesByPowerTree");
void SpecTree_GetPowerListNodesByTree(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt,  SA_PARAM_OP_VALID PowerTree *pTree, int iMax)
{
	SpecTree_GetPowerListNodesEx(pGen, pEnt, pFakeEnt, pTree, GET_REF(pTree->hDef), iMax);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_GetFinalTierPowerListNodes");
void SpecTree_GetFinalTierPowerListNodes(SA_PARAM_OP_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pchUICategories, int iExpectedCount)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	if (pEnt && pEnt->pChar && pchUICategories && pchUICategories[0])
	{
		Character *pChar = pEnt->pChar;
		int iCount = 0;
		int i;
		// Add one power from each tree
		// The power to add should be the one in the highest group
		for (i = 0; i < eaSize(&pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = pChar->ppPowerTrees[i];
			PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);
			const char* pchUICategory = StaticDefineIntRevLookup(PowerTreeUICategoryEnum, pTreeDef->eUICategory);
	
			if (strstri(pchUICategories, pchUICategory))
			{
				int j;
				PTGroupDef *pBestGroup = eaGet(&pTreeDef->ppGroups, 0);
				for (j = 1; j < eaSize(&pTreeDef->ppGroups); j++)
				{
					PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];
					if (SpecTree_PTGroupDefComparitor(&pBestGroup, &pGroupDef) < 0)
					{
						pBestGroup = pGroupDef;
					}
				}

				if (pBestGroup)
				{
					// Always going to be one power in final group. 
					PTNodeDef *pNodeDef = eaGet(&pBestGroup->ppNodes, 0);
					if (pNodeDef)
					{
						FillPowerListNode(
							pEnt,
							eaGetStruct(peaNodes, parse_PowerListNode, iCount++),
							pTree, 
							pTreeDef,
							NULL, 
							pBestGroup,
							SpecTree_GetOwnedNode(pTree, pNodeDef), 
							pNodeDef);
					}
				}
			}
		}
	}
	eaSetSizeStruct(peaNodes, parse_PowerListNode, iExpectedCount);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_TotalRankOfGroup");
int SpecTree_TotalRankOfGroup(SA_PARAM_OP_VALID PowerTree *pTree, const char* pchGroup)
{
	int iTotal = 0;
	if (pTree && pchGroup && *pchGroup)
	{
		int i;
		for (i = 0; i < eaSize(&pTree->ppNodes); i++)
		{
			PTNode *pNode = pTree->ppNodes[i];
			PTNodeDef *pNodeDef = pNode ? GET_REF(pNode->hDef) : NULL;
			if (pNodeDef && strstr(pNodeDef->pchNameFull, pchGroup))
			{
				iTotal += pNode->iRank + 1;
			}
		}
	}
	return iTotal;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("SpecTree_Tooltip");
const char* SpecTree_Tooltip(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode *pListNode, const char* pchCurrentRankKey, const char* pchNextRankKey, const char* pchAttribModsMessageKey)
{
	static char *s_estrDesc = NULL;
	estrClear(&s_estrDesc);
	if (pListNode->iRank)
	{
		g_CombatEvalOverrides.bEnabled = true;
		g_CombatEvalOverrides.bNodeRank = true;
		g_CombatEvalOverrides.iNodeRank = pListNode->iRank;
		estrAppend2(&s_estrDesc, gclAutoDescPower(pEnt, pListNode->pPower, GET_REF(pListNode->hPowerDef), pchCurrentRankKey, pchAttribModsMessageKey, false));
		g_CombatEvalOverrides.bEnabled = false;
		g_CombatEvalOverrides.bNodeRank = false;
		g_CombatEvalOverrides.iNodeRank = 0;
	}
	if (pEnt)
	{
		PowerTreeDef *pTreeDef = GET_REF(pListNode->hTreeDef);
		PTNodeDef *pNodeDef = GET_REF(pListNode->hNodeDef);
		if (pTreeDef && pNodeDef && pListNode->iRank < pListNode->iMaxRank)
		{
			g_CombatEvalOverrides.bEnabled = true;
			g_CombatEvalOverrides.bNodeRank = true;
			g_CombatEvalOverrides.iNodeRank = pListNode->iRank+1;
			estrAppend2(&s_estrDesc, gclAutoDescPower(pEnt, pListNode->pPower, GET_REF(pListNode->hPowerDef), pchNextRankKey, pchAttribModsMessageKey, false));
			g_CombatEvalOverrides.bEnabled = false;
			g_CombatEvalOverrides.bNodeRank = false;
			g_CombatEvalOverrides.iNodeRank = 0;
		}
	}
	return s_estrDesc;
}


#include "AutoGen/FCSpecTreeUI_c_ast.c"