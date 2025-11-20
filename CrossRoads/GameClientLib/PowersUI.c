#include "earray.h"
#include "EString.h"
#include "GameAccountDataCommon.h"
#include "UIGen.h"
#include "PowersUI.h"
#include "PowerGrid.h"
#include "Powers.h"
#include "PowerTree.h"
#include "Expression.h"
#include "error.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "structDefines.h"
#include "PowersAutoDesc.h"
#include "Entity.h"
#include "Character.h"
#include "CharacterClass.h"
#include "GameStringFormat.h"
#include "PowerTreeHelpers.h"
#include "StashTable.h"
#include "LoginCommon.h"
#include "PowerVars.h"
#include "species_common.h"
#include "StringUtil.h"
#include "gclEntity.h"
#include "rand.h"
#include "CharacterCreationUI.h"
#include "Character.h"

#include "AutoGen/PowerGrid_h_ast.h"
#include "AutoGen/PowersAutoDesc_h_ast.h"
#include "AutoGen/Powers_h_ast.h"
#include "AutoGen/PowersEnums_h_ast.h"
#include "Autogen/PowersUI_h_ast.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/Character_h_ast.h"
#include "AutoGen/Entity_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// Maybe this should be on the PowersUI state?
static U32 s_uiCategoryUpdateIndex = 1;
static U32 s_uiCallbacksRegistered = 0;
static bool s_bErrorDisplayed = false;

StashTable s_stPowersUIStates;

void PowersUI_TrimLists(PowersUIState *pPowersUI);
void PowersUI_ClearPowerTrees(PowersUIState *pPowersUI);
SA_RET_OP_VALID PowerListNode *PowersUI_FindFirstPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen);
void PowersUI_SetSelectedPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowerListNode *pListNode);
S32 PowersUI_PurposeComparitor(const PowersUIPurposeNode** a, const PowersUIPurposeNode** b);
S32 PowersUI_CategoryComparitor(const PowersUICategoryNode** a, const PowersUICategoryNode** b);

// Callback for when the dictionary get updated.
static void PowersUI_PowerTreesUpdated(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	s_uiCategoryUpdateIndex++;
}

// Get PowersUI state and ensure it has been updated this frame - important for anything dealing with pointers in the lists. 
// Using stale pointers could lead to a crash.
#define PowersUI_GetUpdatedPowersUIState(pContext, pGen) \
	PowersUI_GetPowersUIStateEx((pContext), (pGen), true, __FUNCTION__)

// Get PowersUI state without ensuring it has been updated. 
// This is for when you need to adjust options on the powers UI state that affect how it updates.
#define PowersUI_GetPowersUIState(pContext, pGen) \
	PowersUI_GetPowersUIStateEx((pContext), (pGen), false, __FUNCTION__)

SA_RET_OP_VALID PowersUIState *PowersUI_GetPowersUIStateEx(ExprContext *pContext, UIGen *pGen, bool bCheckTimestamp, const char* pchFunction)
{
	PowersUIState *pPowersUI = NULL;

	while (pGen && !pPowersUI)
	{
		if (!stashFindPointer(s_stPowersUIStates, pGen, &pPowersUI))
			pGen = pGen->pParent;
	}

	if (!pPowersUI)
	{
		if (!s_bErrorDisplayed)
		{
			s_bErrorDisplayed = true;
			ErrorFilenamef(exprContextGetBlameFile(pContext), "No PowersUI Data. Either you forget to call PowersUI_Init, you opened the editor with the powers UI open. (Called from %s)", pchFunction);
		}
	}
	else if (bCheckTimestamp && pPowersUI->uiFrameLastUpdate < pGen->uiFrameLastUpdate)
	{
		pPowersUI = NULL;
		if (!s_bErrorDisplayed)
		{
			s_bErrorDisplayed = true;
			ErrorFilenamef(exprContextGetBlameFile(pContext), "Stale PowersUI Data. You didn't update with PowersUI_Update or PowersUI_UpdateEx in the PointerUpdate phase. (Called from %s)", pchFunction);
		}
	}

	return pPowersUI;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ClearTrees");
void PowersUI_ClearTrees(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if(pPowersUI)
	{
		eaDestroyStruct(&pPowersUI->eaTreeDefRefs, parse_PowerTreeDefRef);
	}
	s_uiCategoryUpdateIndex++;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_AddTrees");
void PowersUI_AddTrees(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char* pchTrees)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		char *pchNameCopy;
		char *pchStart;
		char *pchContext;

		strdup_alloca(pchNameCopy, pchTrees);
		pchStart = strtok_r(pchNameCopy, " ,\t\r\n", &pchContext);
		if (pchStart)
		{
			do
			{
				PowerTreeDefRef *pDefRef = StructCreate(parse_PowerTreeDefRef);
				SET_HANDLE_FROM_STRING("PowerTreeDef", pchStart, pDefRef->hRef);
				eaPush(&pPowersUI->eaTreeDefRefs, pDefRef);
				gclPowersUIRequestRefsTreeName(pchStart);
			} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
		}
		s_uiCategoryUpdateIndex++;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ClearGroups");
void PowersUI_ClearGroups(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if(pPowersUI)
	{
		eaDestroyStruct(&pPowersUI->eaPTGroupDefRefs, parse_PTGroupDefRef);
	}
	s_uiCategoryUpdateIndex++;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_AddGroups");
void PowersUI_AddGroups(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char* pchGroups)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		char *pchNameCopy;
		char *pchStart;
		char *pchContext;

		strdup_alloca(pchNameCopy, pchGroups);
		pchStart = strtok_r(pchNameCopy, " ,\t\r\n", &pchContext);
		if (pchStart)
		{
			do
			{
				char *pchGroupName;
				char *c;
				strdup_alloca(pchGroupName, pchStart);
				c = strchr(pchGroupName, '.');
				if (c)
				{
					PTGroupDefRef *pDefRef = StructCreate(parse_PTGroupDefRef);
					SET_HANDLE_FROM_STRING("PTGroupDef", pchGroupName, pDefRef->hGroupRef);
					*c = '\0';
					SET_HANDLE_FROM_STRING("PowerTreeDef", pchGroupName, pDefRef->hTreeRef);
					eaPush(&pPowersUI->eaPTGroupDefRefs, pDefRef);
				}
			} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
		}
		s_uiCategoryUpdateIndex++;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_Init");
void PowersUI_Init(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char* pchTrees, const char* pchGroups)
{
	PowersUIState *pPowersUI;
	if (!pGen)
		return;

	pPowersUI = StructCreate(parse_PowersUIState);
	s_bErrorDisplayed = false;

	if (!s_stPowersUIStates)
		s_stPowersUIStates = stashTableCreateAddress(12);

	if (stashAddressAddPointer(s_stPowersUIStates, pGen, pPowersUI, false))
	{
		// Only register callbacks if powers UI state was actually created
		if (s_uiCallbacksRegistered++ == 0)
		{
			resDictRegisterEventCallback(g_hPowerTreeDefDict, PowersUI_PowerTreesUpdated, NULL);
		}

		// Set up hidden category
		pPowersUI->pHiddenCategoryNode = StructCreate(parse_PowersUICategoryNode);
		pPowersUI->pHiddenCategoryNode->eCategory = -2;
		pPowersUI->pHiddenCategoryNode->pchName = "";

		PowersUI_AddTrees(pContext, pGen, pchTrees);
		PowersUI_AddGroups(pContext, pGen, pchGroups);
	}
	else
	{
		StructDestroy(parse_PowersUIState, pPowersUI);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_Deinit");
void PowersUI_Deinit(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = NULL;

	if (stashAddressRemovePointer(s_stPowersUIStates, pGen, &pPowersUI))
	{
		if (s_uiCallbacksRegistered-- == 1)
		{
			resDictRemoveEventCallback(g_hPowerTreeDefDict, PowersUI_PowerTreesUpdated);
		}
	}
	StructDestroy(parse_PowersUIState, pPowersUI);
}

void PowersUI_TrimLists(PowersUIState *pPowersUI)
{
	int i;
	for (i = 0; i < pPowersUI->iNumPurposes; i++)
	{
		PowersUIPurposeNode *pPurposeNode = pPowersUI->eaPurposeListNodes[i];
		eaSetSizeStruct(&pPurposeNode->eaPowerListNodes, parse_PowerListNode, pPurposeNode->iSize);
	}
	eaSetSizeStruct(&pPowersUI->eaPurposeListNodes, parse_PowersUIPurposeNode, pPowersUI->iNumPurposes);
}

void PowersUI_ClearPowerTrees(PowersUIState *pPowersUI)
{
	int i;
	for (i = 0; i < eaSize(&pPowersUI->eaUICategories); i++)
	{
		PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[i];
		if (pCategoryNode)
		{
			eaDestroyStruct(&pCategoryNode->eaTreeNodes, parse_PowersUITreeNode);
			StructDestroy(parse_PowersUICategoryNode, pCategoryNode);
		}
	}
	eaClear(&pPowersUI->eaUICategories);
}

PTNode *PowersUI_FindOwnedNode(Entity *pEnt, PTNodeDef *pNodeDef, PowerTree **pTreeOut)
{
	S32 i, j;

	if (!pNodeDef || !SAFE_MEMBER2(pEnt, pChar, ppPowerTrees))
		return NULL;

	for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
		if (!pTree)
			continue;

		for (j = 0; j < eaSize(&pTree->ppNodes); j++)
		{
			PTNode *pNode = pTree->ppNodes[j];
			PTNodeDef *pOtherNodeDef = pNode ? GET_REF(pNode->hDef) : NULL;

			if (pOtherNodeDef == pNodeDef)
			{
				if (pTreeOut)
					*pTreeOut = pTree;

				return pNode;
			}
		}
	}
	return NULL;
}

PowersUIPurposeNode *PowersUI_GetPurposeNodeForTalentTree(PowersUIState *pPowersUI, PowerTreeDef *pPowerTreeDef)
{
	PowersUIPurposeNode *pPurposeNode = NULL;
	const char *pchPurposeName = NULL;
	devassert(pPowersUI);
	devassert(pPowerTreeDef);

	// Use the power tree name as the purpose name
	pchPurposeName = pPowerTreeDef->pchName;

	if (pchPurposeName && pchPurposeName[0])
	{
		S32 i, iPurposeIndex = -1;
		for (i = 0; i < pPowersUI->iNumPurposes; i++)
		{
			pPurposeNode = pPowersUI->eaPurposeListNodes[i];
			if (stricmp(pchPurposeName, pPurposeNode->pchName) == 0)
			{
				iPurposeIndex = i;
				break;
			}
		}
		if (iPurposeIndex == -1)
		{
			pPurposeNode = eaGetStruct(&pPowersUI->eaPurposeListNodes, parse_PowersUIPurposeNode, pPowersUI->iNumPurposes);
			pPurposeNode->pchName = pchPurposeName;
			pPurposeNode->ePurpose = kPowerPurpose_Uncategorized; // All talent trees are assigned to uncategorized power purpose
			pPurposeNode->iSize = 0;
			iPurposeIndex = pPowersUI->iNumPurposes++;
		}
	}

	return pPurposeNode;
}

PowersUIPurposeNode *PowersUI_GetPurposeNode(PowersUIState *pPowersUI, PowerDef *pPowerDef)
{
	PowerPurpose ePurpose = pPowerDef ? pPowerDef->ePurpose : -1;
	const char *pchPurposeName = (ePurpose != -1) ? StaticDefineIntRevLookup(PowerPurposeEnum, pPowerDef->ePurpose) : NULL;
	PowersUIPurposeNode *pPurposeNode = NULL;

	if(pPowerDef && pchPurposeName && pchPurposeName[0])
	{
		S32 i, iPurposeIndex = -1;
		for (i = 0; i < pPowersUI->iNumPurposes; i++)
		{
			pPurposeNode = pPowersUI->eaPurposeListNodes[i];
			if (stricmp(pchPurposeName, pPurposeNode->pchName) == 0)
			{
				iPurposeIndex = i;
				break;
			}
		}
		if (iPurposeIndex == -1)
		{
			pPurposeNode = eaGetStruct(&pPowersUI->eaPurposeListNodes, parse_PowersUIPurposeNode, pPowersUI->iNumPurposes);
			pPurposeNode->pchName = pchPurposeName;
			pPurposeNode->ePurpose = ePurpose;
			pPurposeNode->iSize = 0;
			iPurposeIndex = pPowersUI->iNumPurposes++;
		}
	}

	return pPurposeNode;
}

// NOTE: This only returns info about Rank 0.
bool PowersUI_AddPowersInPowerGroup(PowersUIState *pPowersUI, Entity *pEnt, SA_PARAM_OP_VALID Entity *pEntCompare, PowerTreeDef *pTreeDef, PTGroupDef *pGroupDef)
{
	int i;
	bool bPowerAdded = false;
	for(i = 0; i < eaSize(&pGroupDef->ppNodes); i++)
	{
		// NOTE: This only returns info about Rank 0.
		PTNodeDef *pNodeDef = pGroupDef->ppNodes[i];
		PTNodeRankDef *pRank = eaGet(&pNodeDef->ppRanks, 0);
		PowerDef *pPowerDef = (pRank)? GET_REF(pRank->hPowerDef) : NULL;
		PowerTree *pTree = NULL;
		PTNode *pNode = PowersUI_FindOwnedNode(pEnt, pNodeDef, &pTree);
		bool bIsOwned = !!pNode;
		int iRankNew = (pNode && !pNode->bEscrow) ? pNode->iRank + 1 : 0;
		bool bCanIncrement = entity_CanBuyPowerTreeNodeHelper(ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pEnt), pGroupDef, pNodeDef, iRankNew, true, true, false, false);
		bool bIsAvailable = !bIsOwned && entity_CanBuyPowerTreeNodeHelper( ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pEnt), pGroupDef, pNodeDef, 0, true, true, false, false);
		bool bForceAddDueToCompEntity = false;

		// Is there an entity to compare against
		if (pEntCompare)
		{
			// Filter out any powers owned by the comparison entity
			if (pPowersUI->bFilterOwnedByComparisonEntity)
			{
				PowerTree *pTreeCompEnt = NULL;
				PTNode *pNodeCompEnt = PowersUI_FindOwnedNode(pEntCompare, pNodeDef, &pTreeCompEnt);
				if (pNodeCompEnt)
				{
					// Skip this power because it's owned by the comparison entity
					continue;
				}
			}

			// Do we want to add powers available to the comparison entity
			if (pPowersUI->bAddAvailableForComparisonEntity)
			{
				bForceAddDueToCompEntity = entity_CanBuyPowerTreeNodeHelper( ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pEntCompare), pGroupDef, pNodeDef, 0, true, true, false, false);
			}
		}
		if (pPowerDef
			&& !(pNodeDef->eFlag & kNodeFlag_HideNode)
			&& isPowerUnfiltered(pNodeDef, pPowersUI->pchFilterText)
			&& isPowerAttribModUnfiltered(pNodeDef, pPowersUI->pchFilterModList)
			&& (bForceAddDueToCompEntity ||
			(pPowersUI->bShowOwned && bIsOwned) ||
			(pPowersUI->bShowAvailable && bIsAvailable) ||
			(pPowersUI->bShowUnavailable && !(bIsAvailable || bIsOwned))))
		{
			PowerListNode* pListNode;
			PowersUIPurposeNode *pPurposeNode;
			if (pTreeDef->bIsTalentTree) // Talent trees ignore the power purpose
				pPurposeNode = PowersUI_GetPurposeNodeForTalentTree(pPowersUI, pTreeDef);
			else
				pPurposeNode = PowersUI_GetPurposeNode(pPowersUI, pPowerDef);
			pListNode = eaGetStruct(&pPurposeNode->eaPowerListNodes, parse_PowerListNode, pPurposeNode->iSize++);
			FillPowerListNode(pEnt, pListNode, pTree, pTreeDef, NULL, pGroupDef, pNode, pNodeDef);
			pListNode->bIsOwned = bIsOwned;
			pListNode->bIsAvailable = bIsAvailable;
			pListNode->bCanIncrement = bCanIncrement;
			bPowerAdded = true;
			if (pPowersUI->pSelectedPowerNode == GET_REF(pListNode->hNodeDef))
			{
				pPowersUI->pSelectedPower = pListNode;
			}

			if (pListNode->iLevel > pPowersUI->iMaxPowerLevelInPurposeNodes)
			{
				pPowersUI->iMaxPowerLevelInPurposeNodes = pListNode->iLevel;
			}
		}
	}
	return bPowerAdded;
}

bool PowersUI_AddPowersInPowerTree(PowersUIState *pPowersUI, Entity* pEnt, SA_PARAM_OP_VALID Entity *pEntCompare, PowersUITreeNode *pTreeNode)
{
	int i;
	PowerTreeDef *pTreeDef = GET_REF(pTreeNode->hTreeDef);
	bool bPowerAdded = false;
	if (pTreeDef && SAFE_MEMBER(pTreeNode, bSelected))
	{
		for(i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
		{
			bPowerAdded |= PowersUI_AddPowersInPowerGroup(pPowersUI, pEnt, pEntCompare, pTreeDef, pTreeDef->ppGroups[i]);
		}
	}
	return bPowerAdded;
}

void PowersUI_AddPowersInCategory(PowersUIState *pPowersUI, Entity *pEnt, SA_PARAM_OP_VALID Entity *pEntCompare, PowersUICategoryNode *pCategoryNode)
{
	int i;
	for(i = 0; i < eaSize(&pCategoryNode->eaTreeNodes); i++)
	{
		PowersUITreeNode *pTreeNode = pCategoryNode->eaTreeNodes[i];
		pTreeNode->bShow |=	PowersUI_AddPowersInPowerTree(pPowersUI, pEnt, pEntCompare, pTreeNode);
	}
}

void PowersUI_UpdateDependentTrees(PowersUIState *pPowersUI)
{
	int i, j, k;
	for (i = 0 ; i < eaSize(&pPowersUI->pHiddenCategoryNode->eaTreeNodes); i++)
	{
		PowersUITreeNode *pHiddenTreeNode = pPowersUI->pHiddenCategoryNode->eaTreeNodes[i];
		static PowerTreeDef **eaParents = NULL;
		S32 iParentsSize = 0;
		PowerTreeDef *pDependant = GET_REF(pHiddenTreeNode->hTreeDef);
		eaClearFast(&eaParents);

		// Make a list of parents for the dependent tree
		for (j = 0; j < eaSize(&pPowersUI->eaDependantTreeNodes); j++)
		{
			PowerTreeDef *pTreeDef = GET_REF(pPowersUI->eaDependantTreeNodes[j]->hDependant);
			PowerTreeDef *pPossibleParent = GET_REF(pPowersUI->eaDependantTreeNodes[j]->hParent);
			if (pTreeDef == pDependant && pPossibleParent)
			{
				if (eaSize(&eaParents) > iParentsSize)
					eaParents[iParentsSize] = pPossibleParent;
				else
					eaPush(&eaParents, pPossibleParent);
				iParentsSize++;
			}
		}

		if (!eaSize(&eaParents) || !pDependant)
			continue;

		// If any of the parents are selected, select the dependent node
		for (j = 0; j < eaSize(&pPowersUI->eaUICategories); j++)
		{
			bool bFound = false;
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[j];
			for (k = 0; k < eaSize(&pCategoryNode->eaTreeNodes); k++)
			{
				PowersUITreeNode *pTreeNode = pCategoryNode->eaTreeNodes[k];
				PowerTreeDef *pTreeDef = GET_REF(pTreeNode->hTreeDef);
				if (pTreeNode->bSelected && pTreeDef && (eaFind(&eaParents, pTreeDef) >= 0))
				{
					pHiddenTreeNode->bSelected = true;
					bFound = true;
					break;
				}
			}
			if (bFound)
				break;
			else
				pHiddenTreeNode->bSelected = false;
		}
	}
}

void PowersUI_AddPowerTreesToCategory(PowersUIState *pPowersUI, Entity *pEnt, bool bAutoExpand)
{
	S32 i, j;

	eaDestroyStruct(&pPowersUI->eaUICategories, parse_PowersUICategoryNode);
	
	// For each power tree reference loaded,
	for (i = 0; i < eaSize(&pPowersUI->eaTreeDefRefs); i++)
	{
		PowerTreeDefRef *pDefRef = pPowersUI->eaTreeDefRefs[i];
		PowerTreeDef *pTreeDef = GET_REF(pDefRef->hRef);
		PowersUITreeNode *pTreeNode = NULL;
		S32 iCategoryIndex = -1;
		bool bTreeLoaded = false;
		if (!pTreeDef)
			continue;

		// Add the power tree to the category
		for (j = 0; j < eaSize(&pPowersUI->eaUICategories); j++)
		{
			if (pTreeDef->eUICategory == pPowersUI->eaUICategories[j]->eCategory)
			{
				iCategoryIndex = j;
				break;
			}
		}
		if (iCategoryIndex == -1)
		{
			PowersUICategoryNode *pCategoryNode = StructCreate(parse_PowersUICategoryNode);
			pCategoryNode->eCategory = pTreeDef->eUICategory;
			pCategoryNode->pchName = StaticDefineIntRevLookup(PowerTreeUICategoryEnum, pCategoryNode->eCategory);
			iCategoryIndex = eaSize(&pPowersUI->eaUICategories);
			eaPush(&pPowersUI->eaUICategories, pCategoryNode);
		}

		// and finally add the tree node to the category if its not there already
		for (j = 0; j < eaSize(&pPowersUI->eaUICategories[iCategoryIndex]->eaTreeNodes); j++)
		{
			PowersUITreeNode *pOtherTreeNode = pPowersUI->eaUICategories[iCategoryIndex]->eaTreeNodes[j];
			PowerTreeDef *pOtherTreeDef = GET_REF(pOtherTreeNode->hTreeDef);
			if (pTreeDef == pOtherTreeDef)
			{
				pTreeNode = pOtherTreeNode;
				bTreeLoaded = true;
				break;
			}
		}
		if (!bTreeLoaded)
		{
			pTreeNode = StructCreate(parse_PowersUITreeNode);
			COPY_HANDLE(pTreeNode->hTreeDef, pDefRef->hRef);
			pTreeNode->pchTreeName = pTreeDef->pchName;
			eaPush(&pPowersUI->eaUICategories[iCategoryIndex]->eaTreeNodes, pTreeNode);
		}

		// Select the tree if any powers in it are owned.
		if (pTreeDef && pTreeNode && pEnt && bAutoExpand)
		{
			CONST_EARRAY_OF(PowerTree) eaTrees = SAFE_MEMBER2(pEnt, pChar, ppPowerTrees);
			for (j = 0; j < eaSize(&eaTrees); j++)
			{
				PowerTreeDef *pOtherTreeDef = eaTrees[j] ? GET_REF(eaTrees[j]->hDef) : NULL;
				if (pTreeDef == pOtherTreeDef)
				{
					int k;
					pTreeNode->bSelected = true;

					// And also set the category to be expanded.
					for (k = 0; k < eaSize(&pPowersUI->eaUICategories); k++)
					{
						if (pPowersUI->eaUICategories[k]->eCategory == pTreeDef->eUICategory)
						{
							pPowersUI->eaUICategories[k]->bExpand = true;
							break;
						}
					}
					break;
				}
			}
		}
	}

	// Add hidden trees
	// This is mostly c+p from above, I suspect it should be refactored
	for (i = 0; i < eaSize(&pPowersUI->eaDependantTreeNodes); i++)
	{
		PowersUIDependentTreeNode *pDependentNode = pPowersUI->eaDependantTreeNodes[i];
		PowerTreeDef *pTreeDef = GET_REF(pDependentNode->hDependant);
		PowersUITreeNode *pTreeNode;
		bool bTreeLoaded = false;

		if (!pTreeDef)
			continue;

		// and finally add the tree node to the category if its not there already
		for (j = 0; j < eaSize(&pPowersUI->pHiddenCategoryNode->eaTreeNodes); j++)
		{
			PowersUITreeNode *pOtherTreeNode = pPowersUI->pHiddenCategoryNode->eaTreeNodes[j];
			PowerTreeDef *pOtherTreeDef = GET_REF(pOtherTreeNode->hTreeDef);
			if (pTreeDef == pOtherTreeDef)
			{
				pTreeNode = pOtherTreeNode;
				bTreeLoaded = true;
				break;
			}
		}
		if (!bTreeLoaded)
		{
			pTreeNode = StructCreate(parse_PowersUITreeNode);
			COPY_HANDLE(pTreeNode->hTreeDef, pDependentNode->hDependant);
			pTreeNode->pchTreeName = pTreeDef->pchName;
			eaPush(&pPowersUI->pHiddenCategoryNode->eaTreeNodes, pTreeNode);
		}
	}
}

void PowersUI_AddOwnedPowerTree(PowersUIState *pPowersUI, Entity *pEnt, PowerTree *pTree)
{
	if (pTree)
	{
		int i;
		for (i = 0; i < eaSize(&pTree->ppNodes); i++)
		{
			PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);
			PTNode *pNode = pTree->ppNodes[i];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
			PTNodeRankDef *pRank = (pNodeDef) ? eaGet(&pNodeDef->ppRanks, 0) : NULL;
			PowerDef *pPowerDef = (pRank) ? GET_REF(pRank->hPowerDef) : NULL;
			if (pPowerDef
				&& !(SAFE_MEMBER(pNodeDef, eFlag) & kNodeFlag_HideNode)
				&& isPowerUnfiltered(pNodeDef, pPowersUI->pchFilterText)
				&& isPowerAttribModUnfiltered(pNodeDef, pPowersUI->pchFilterModList))
			{
				PowerListNode* pListNode;
				PowersUIPurposeNode *pPurposeNode;
				if (pTreeDef->bIsTalentTree) // Talent trees ignore the power purpose
					pPurposeNode = PowersUI_GetPurposeNodeForTalentTree(pPowersUI, pTreeDef);
				else
					pPurposeNode = PowersUI_GetPurposeNode(pPowersUI, pPowerDef);
				if (!pPurposeNode)
					continue;
				pListNode = eaGetStruct(&pPurposeNode->eaPowerListNodes, parse_PowerListNode, pPurposeNode->iSize++);
				FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, NULL, pNode, NULL);
				pListNode->bIsOwned = true;
				//PowersUI_AddPowerListNode(pPowersUI, pListNode, pPurposeNode);
				if (pPowersUI->pSelectedPowerNode == GET_REF(pListNode->hNodeDef))
				{
					pPowersUI->pSelectedPower = pListNode;
				}
			}
		}
	}
}

// Gets the bounds of the talent tree
static void PowersUI_GetTalentTreeBounds(PowerTreeDef *pPowerTreeDef, S8 *piLowX, S8 *piHighX, S8 *piLowY, S8 *piHighY)
{
	S32 i, j;

	devassert(pPowerTreeDef);
	devassert(piLowX);
	devassert(piHighX);
	devassert(piLowY);
	devassert(piHighY);

	// Initialize the input values
	*piLowX = *piLowY = 127;
	*piHighX = *piHighY = 0;

	for (i = 0; i < eaSize(&pPowerTreeDef->ppGroups); i++)
	{
		for (j = 0; j < eaSize(&pPowerTreeDef->ppGroups[i]->ppNodes); j++)
		{
			PTNodeDef *pNodeDef = pPowerTreeDef->ppGroups[i]->ppNodes[j];

			// Is the node is assigned to a talent tree position
			if (pNodeDef->iUIGridRow > 0 && pNodeDef->iUIGridColumn > 0)
			{
				MIN1(*piLowX, pNodeDef->iUIGridColumn);
				MIN1(*piLowY, pNodeDef->iUIGridRow);
				MAX1(*piHighX, pNodeDef->iUIGridColumn);
				MAX1(*piHighY, pNodeDef->iUIGridRow);
			}
		}
	}

	if (*piHighX == 0)
		*piLowX = 0;
	if (*piHighY == 0)
		*piLowY = 0;
}

// Returns the power list node that should be displayed in the given talent tree position. Returns NULL if no node is assigned for the position.
static PowerListNode *PowersUI_FindPowerListNodeByTalentTreePos(SA_PARAM_NN_VALID PowersUIState *pPowersUI, S8 iRow, S8 iColumn, SA_PARAM_NN_STR const char *pchTalentTreeName)
{
	S32 i, j;
	PTNodeDef *pNodeDef;

	devassert(pPowersUI);
	devassert(pchTalentTreeName);
	devassert(iRow > 0);
	devassert(iColumn > 0);

	// Loop thru the purpose list nodes
	for (i = 0; i < eaSize(&pPowersUI->eaPurposeListNodes); i++)
	{
		// Each talent tree creates a purpose node. So we are only interested in the nodes
		// that exists in the purpose with the same name as the talent tree
		if (stricmp(pPowersUI->eaPurposeListNodes[i]->pchName, pchTalentTreeName) == 0)
		{
			for (j = 0; j < eaSize(&pPowersUI->eaPurposeListNodes[i]->eaPowerListNodes); j++)
			{
				pNodeDef = GET_REF(pPowersUI->eaPurposeListNodes[i]->eaPowerListNodes[j]->hNodeDef);
				if (pNodeDef && pNodeDef->iUIGridRow == iRow && pNodeDef->iUIGridColumn == iColumn)
					return pPowersUI->eaPurposeListNodes[i]->eaPowerListNodes[j];
			}

			break;
		}
	}

	return NULL;
}

static void PowersUI_UpdateTalentTreeTextureBits(SA_PARAM_NN_VALID TalentsUITree *pTalentTree, SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 i, j, iTotalNodeCount;
	PowerTreeDef *pPowerTreeDef;

	devassert(pTalentTree);

	iTotalNodeCount = pTalentTree->iNumCols * pTalentTree->iNumRows;

	// Set the number of texture bits
	ea32SetSize(&pTalentTree->eaTextureBits, iTotalNodeCount);
	ea32SetSize(&pTalentTree->eaTextureBitsPathMode, iTotalNodeCount);

	if (iTotalNodeCount > 0)
	{
		// Set all elements to zero
		memset((void *)pTalentTree->eaTextureBits, 0, ea32Size(&pTalentTree->eaTextureBits) * sizeof(S32));
		memset((void *)pTalentTree->eaTextureBitsPathMode, 0, ea32Size(&pTalentTree->eaTextureBitsPathMode) * sizeof(S32));

		// Get the power tree def
		pPowerTreeDef = GET_REF(pTalentTree->hTreeDef);

		devassert(pPowerTreeDef);

		// Loop thru all dependencies and walk in that path
		for (i = 0; i < eaSize(&pPowerTreeDef->ppGroups); i++)
		{
			for (j = 0; j < eaSize(&pPowerTreeDef->ppGroups[i]->ppNodes); j++)
			{
				if (IS_HANDLE_ACTIVE(pPowerTreeDef->ppGroups[i]->ppNodes[j]->hNodeRequire))
				{
					S32 iColIt, iRowIt, iColStep, iRowStep, iArrayIndex;
					PTNodeDef *pNodeDest = pPowerTreeDef->ppGroups[i]->ppNodes[j];
					PTNodeDef *pNodeSource = GET_REF(pNodeDest->hNodeRequire);
					PowerTree *pTree = NULL;
					PTNode *pNode = PowersUI_FindOwnedNode(pEnt, pNodeDest, &pTree);
					bool bIsOwned = !!pNode;
					bool bIsAvailable = bIsOwned || entity_CanBuyPowerTreeNodeHelper( ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pEnt), pPowerTreeDef->ppGroups[i], pNodeDest, 0, true, true, false, false);

					if (pNodeSource && pNodeSource != pNodeDest)
					{
						// There is a dependency, walk thru the path

						iColStep = pNodeSource->iUIGridColumn == pNodeDest->iUIGridColumn ? 0 :
							pNodeSource->iUIGridColumn > pNodeDest->iUIGridColumn ? -1 : 1;
						iRowStep = pNodeSource->iUIGridRow == pNodeDest->iUIGridRow ? 0 :
							pNodeSource->iUIGridRow > pNodeDest->iUIGridRow ? -1 : 1;

						// Move horizontally first if necessary
						if (iColStep != 0)
						{
							for (iColIt = pNodeSource->iUIGridColumn, iRowIt = pNodeSource->iUIGridRow;
								(iColStep > 0 && iColIt <= pNodeDest->iUIGridColumn) || (iColStep < 0  && iColIt >= pNodeDest->iUIGridColumn);
								iColIt += iColStep)
							{
								iArrayIndex = (iRowIt - pTalentTree->iLowY) * pTalentTree->iNumCols + (iColIt - pTalentTree->iLowX);

								if (iColIt == pNodeSource->iUIGridColumn)
								{
									// This is where the arrow originates from. We need a connector in correct direction
									if (iColStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ConnectorOnRight;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ConnectorOnRight;
									}
									else
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ConnectorOnLeft;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ConnectorOnLeft;
									}
								}
								else if (iColIt == pNodeDest->iUIGridColumn && iRowStep == 0)
								{
									// The last node on the horizontal line is the destination node
									if (iColStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ArrowOnLeft;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ArrowOnLeft;
									}
									else
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ArrowOnRight;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ArrowOnRight;
									}
								}
								else if (iColIt == pNodeDest->iUIGridColumn && iRowIt != pNodeDest->iUIGridRow)
								{
									// The last node on the horizontal line needs an L shaped texture going up or down
									if (iColStep > 0 && iRowStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_LShapeBottomLeft;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_LShapeBottomLeft;
									}
									else if (iColStep > 0 && iRowStep < 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_LShapeTopLeft;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_LShapeTopLeft;
									}
									else if (iColStep < 0 && iRowStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_LShapeBottomRight;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_LShapeBottomRight;
									}
									else if (iColStep < 0 && iRowStep < 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_LShapeTopRight;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_LShapeTopRight;
									}
								}
								else
								{
									pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_Horizontal;
									if (bIsAvailable)
										pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_Horizontal;
								}
							}
						}

						// Move vertically if necessary
						if (iRowStep != 0)
						{
							for (iRowIt = pNodeSource->iUIGridRow, iColIt = pNodeDest->iUIGridColumn;
								(iRowStep > 0 && iRowIt <= pNodeDest->iUIGridRow) || (iRowStep < 0  && iRowIt >= pNodeDest->iUIGridRow);
								iRowIt += iRowStep)
							{
								iArrayIndex = (iRowIt - pTalentTree->iLowY) * pTalentTree->iNumCols + (iColIt - pTalentTree->iLowX);

								if (iRowIt == pNodeSource->iUIGridRow)
								{
									if (iColStep == 0 && iRowStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ConnectorOnBottom;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ConnectorOnBottom;
									}
									else if (iColStep == 0 && iRowStep < 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ConnectorOnTop;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ConnectorOnTop;
									}
								}
								else if (iRowIt == pNodeDest->iUIGridRow)
								{
									// Reached the destination
									if (iRowStep > 0)
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ArrowOnTop;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ArrowOnTop;
									}
									else
									{
										pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_ArrowOnBottom;
										if (bIsAvailable)
											pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_ArrowOnBottom;
									}
								}
								else
								{
									pTalentTree->eaTextureBits[iArrayIndex] |= TalentsUITextureBits_Vertical;
									if (bIsAvailable)
										pTalentTree->eaTextureBitsPathMode[iArrayIndex] |= TalentsUITextureBits_Vertical;
								}
							}
						}
					}
				}
			}
		}
	}
}

// Updates the UI elements for the talent trees
static void PowersUI_UpdateTalentTree(SA_PARAM_NN_VALID PowersUIState *pPowersUI, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerTreeDef *pPowerTreeDef, S8 iPos)
{
	TalentsUITree *pTalentTree;
	TalentsUITreeNode *pTalentTreeNode;
	S32 i, j;
	S8 iLowX, iHighX, iLowY, iHighY, iTotalNodeCount;

	devassert(pPowersUI);
	devassert(pPowerTreeDef);

	// Set up the talent tree
	pTalentTree = eaGetStruct(&pPowersUI->eaTalentTrees, parse_TalentsUITree, iPos);
	pTalentTree->pchTreeName = pPowerTreeDef->pchName;
	SET_HANDLE_FROM_STRING("PowerTreeDef", pTalentTree->pchTreeName, pTalentTree->hTreeDef);

	// Get the bounds of the talent tree
	PowersUI_GetTalentTreeBounds(pPowerTreeDef, &iLowX, &iHighX, &iLowY, &iHighY);

	pTalentTree->iLowX = iLowX;
	pTalentTree->iHighX = iHighX;
	pTalentTree->iLowY = iLowY;
	pTalentTree->iHighY = iHighY;
	pTalentTree->iNumCols = (iLowX > 0 && iHighX > 0) ? (iHighX - iLowX + 1) : 0;
	pTalentTree->iNumRows = (iLowY > 0 && iHighY > 0) ? (iHighY - iLowY + 1) : 0;

	PowersUI_UpdateTalentTreeTextureBits(pTalentTree, pEnt);

	iTotalNodeCount = pTalentTree->iNumRows * pTalentTree->iNumCols;

	// Set the number of nodes in the talent tree
	eaSetSizeStruct(&pTalentTree->eaTalentNodes, parse_TalentsUITreeNode, iTotalNodeCount);

	if (iTotalNodeCount > 0)
	{
		// Create all nodes
		for (i = iLowY; i <= iHighY; i++)
		{
			for (j = iLowX; j <= iHighX; j++)
			{
				// Create the node
				pTalentTreeNode = eaGetStruct(&pTalentTree->eaTalentNodes, parse_TalentsUITreeNode, (i - iLowY) * (iHighX - iLowX + 1) + (j - iLowX));
				pTalentTreeNode->iColumn = j - iLowX;
				pTalentTreeNode->iRow = i - iLowY;
				pTalentTreeNode->iTextureBits = pTalentTree->eaTextureBits[pTalentTreeNode->iRow * pTalentTree->iNumCols + pTalentTreeNode->iColumn];
				pTalentTreeNode->iTextureBitsPathMode = pTalentTree->eaTextureBitsPathMode[pTalentTreeNode->iRow * pTalentTree->iNumCols + pTalentTreeNode->iColumn];
				pTalentTreeNode->pPowerListNode = PowersUI_FindPowerListNodeByTalentTreePos(pPowersUI, i, j, pTalentTree->pchTreeName);
				pTalentTreeNode->bHasPowerNode = pTalentTreeNode->pPowerListNode ? true : false;
			}
		}
	}
}

// Updates the UI elements for the talent trees
static void PowersUI_UpdateTalentTrees(SA_PARAM_NN_VALID PowersUIState *pPowersUI, SA_PARAM_OP_VALID Entity *pEnt)
{
	S32 i;
	S8 iTalentTreeCount = 0;

	devassert(pPowersUI);

	// Iterate thru all the trees requested
	for (i = 0; i < eaSize(&pPowersUI->eaTreeDefRefs); i++)
	{
		PowerTreeDef *pPowerTreeDef = GET_REF(pPowersUI->eaTreeDefRefs[i]->hRef);

		// Respect only the talent trees
		if (pPowerTreeDef && pPowerTreeDef->bIsTalentTree)
		{
			PowersUI_UpdateTalentTree(pPowersUI, pEnt, pPowerTreeDef, iTalentTreeCount++);
		}
	}

	// Trim the list if necessary
	eaSetSizeStruct(&pPowersUI->eaTalentTrees, parse_TalentsUITree, iTalentTreeCount);
}

S32 PowersUI_PowerListNodeLevelComparitor(const PowerListNode ** a, const PowerListNode ** b)
{
	S32 iLevelA = 0;
	S32 iLevelB = 0;

	if (a && *a)
	{
		iLevelA = (*a)->iLevel;
	}

	if (b && *b)
	{
		iLevelB = (*b)->iLevel;
	}
	return iLevelA - iLevelB;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_UpdateEx");
void PowersUI_UpdateEx(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pEntCompare, bool bAutoExpand)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);

	if (!pPowersUI)
		return;

	pPowersUI->uiFrameLastUpdate = pGen->uiFrameLastUpdate;

	if (pPowersUI->uiCategoryUpdateNum < s_uiCategoryUpdateIndex)
	{
		pPowersUI->uiCategoryUpdateNum = s_uiCategoryUpdateIndex;
		PowersUI_AddPowerTreesToCategory(pPowersUI, pEnt, bAutoExpand);
	}

	if (pEnt)
	{
		S32 i;
		pPowersUI->iNumPurposes = 0;
		pPowersUI->pSelectedPower = NULL;

		// Reset the max power level
		pPowersUI->iMaxPowerLevelInPurposeNodes = 0;

		// If there's only one tree, automatically display it
		if ((eaSize(&pPowersUI->eaTreeDefRefs) == 1) && (eaSize(&pPowersUI->eaUICategories) == 1))
		{
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[0];
			if (eaSize(&pCategoryNode->eaTreeNodes))
			{
				pCategoryNode->eaTreeNodes[0]->bSelected = true;
			}
		}

		for (i = 0; i < eaSize(&pPowersUI->eaUICategories); i++)
		{
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[i];
			PowersUI_AddPowersInCategory(pPowersUI, pEnt, pEntCompare, pCategoryNode);
		}

		PowersUI_UpdateDependentTrees(pPowersUI);
		PowersUI_AddPowersInCategory(pPowersUI, pEnt, pEntCompare, pPowersUI->pHiddenCategoryNode);

		for (i = 0; i < eaSize(&pPowersUI->eaPTGroupDefRefs); i++)
		{
			PTGroupDefRef *pGroupDefRef = pPowersUI->eaPTGroupDefRefs[i];
			PTGroupDef *pGroupDef = pGroupDefRef ? GET_REF(pGroupDefRef->hGroupRef) : NULL;
			PowerTreeDef *pTreeDef = pGroupDefRef ? GET_REF(pGroupDefRef->hTreeRef) : NULL;
			if (pGroupDef && pTreeDef)
			{
				PowersUI_AddPowersInPowerGroup(pPowersUI, pEnt, pEntCompare, pTreeDef, pGroupDef);
			}
		}

		PowersUI_TrimLists(pPowersUI);

		eaQSort(pPowersUI->eaUICategories, PowersUI_CategoryComparitor);
		eaQSort(pPowersUI->eaPurposeListNodes, PowersUI_PurposeComparitor);
		if (pPowersUI->ePurposeListPowerSortingMethod != PurposeListPowerSortingMethod_Default)
		{
			FOR_EACH_IN_CONST_EARRAY_FORWARDS(pPowersUI->eaPurposeListNodes, PowersUIPurposeNode, pUIPurposeNode)
			{
				if (pPowersUI->ePurposeListPowerSortingMethod == PurposeListPowerSortingMethod_Level)
				{
					eaQSort(pUIPurposeNode->eaPowerListNodes, PowersUI_PowerListNodeLevelComparitor);
				}
			}
			FOR_EACH_END
		}

		// Update all talent trees
		PowersUI_UpdateTalentTrees(pPowersUI, pEnt);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_Update");
void PowersUI_Update(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, bool bAutoExpand)
{
	PowersUI_UpdateEx(pContext, pGen, pEnt, NULL, bAutoExpand);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_OwnedPowers_Update");
void PowersUI_OwnedPowers_Update(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	CONST_EARRAY_OF(PowerTree) eaTrees = SAFE_MEMBER2(pEnt, pChar, ppPowerTrees);

	if (pPowersUI && eaTrees)
	{
		S32 i;

		pPowersUI->uiFrameLastUpdate = pGen->uiFrameLastUpdate;
		pPowersUI->iNumPurposes = 0;
		pPowersUI->pSelectedPower = NULL;

		for (i = 0; i < eaSize(&eaTrees); i++)
		{
			PowerTree *pTree = eaTrees[i];
			PowersUI_AddOwnedPowerTree(pPowersUI, pEnt, pTree);
		}

		PowersUI_TrimLists(pPowersUI);

		eaQSort(pPowersUI->eaUICategories, PowersUI_CategoryComparitor);
		eaQSort(pPowersUI->eaPurposeListNodes, PowersUI_PurposeComparitor);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_FindFirstPower");
SA_RET_OP_VALID PowerListNode *PowersUI_FindFirstPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && eaSize(&pPowersUI->eaPurposeListNodes))
	{
		PowersUIPurposeNode *pPurposeNode = pPowersUI->eaPurposeListNodes[0];
		if (pPurposeNode && eaSize(&pPurposeNode->eaPowerListNodes))
		{
			return pPurposeNode->eaPowerListNodes[0];
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetSelectedPower");
void PowersUI_SetSelectedPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && pListNode && GET_REF(pListNode->hNodeDef))
	{
		pPowersUI->pSelectedPower = pListNode;
		pPowersUI->pSelectedPowerNode = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ClearSelectedPower");
void PowersUI_ClearSelectedPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->pSelectedPower = NULL;
		pPowersUI->pSelectedPowerNode = NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetSelectedPower");
SA_RET_OP_VALID PowerListNode *PowersUI_GetSelectedPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	return SAFE_MEMBER(pPowersUI, pSelectedPower);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetSelectedPTNodeDef");
SA_RET_OP_VALID PTNodeDef *PowersUI_GetSelectedPTNodeDef(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	return SAFE_MEMBER(pPowersUI, pSelectedPowerNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsSelectedPower");
bool PowersUI_IsSelectedPower(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	return pPowersUI
		&& pPowersUI->pSelectedPower
		&& pListNode
		&& GET_REF(pListNode->hNodeDef)
		&& GET_REF(pPowersUI->pSelectedPower->hNodeDef) == GET_REF(pListNode->hNodeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowersUI_AddDependentTree);
void PowersUI_AddDependentTree(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char *pchParent, const char *pchDependant)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI && pchParent && pchDependant)
	{
		PowersUIDependentTreeNode *pDepenentNode = StructCreate(parse_PowersUIDependentTreeNode);
		SET_HANDLE_FROM_STRING("PowerTreeDef", pchParent, pDepenentNode->hParent);
		SET_HANDLE_FROM_STRING("PowerTreeDef", pchDependant, pDepenentNode->hDependant);
		eaPush(&pPowersUI->eaDependantTreeNodes, pDepenentNode);
	}
}

// This is the model for the tab group along the top of the Powers UI.
// This is the list of "supertabs"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetUICategoryList");
void PowersUI_GetUICategoryList(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && pGen)
	{
		ui_GenSetListSafe(pGen, &pPowersUI->eaUICategories, PowersUICategoryNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowersUICategoryNode);
	}
}

// This is the model for the subtabs. This is the list of trees within a
// UI category
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerTreeList");
void PowersUI_GetPowerTreeList(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, PowersUICategoryNode *pCategoryNode)
{
	if (pCategoryNode && pGen)
	{
		ui_GenSetListSafe(pGen, &pCategoryNode->eaTreeNodes, PowersUITreeNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowersUITreeNode);
	}
}

// This is the model for the list on the left hand side of the Powers UI.
// This is the list of Power Purposes that the powers are sorted into.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerPurposeList");
void PowersUI_GetPowerPurposeList(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && pGen)
	{
		ui_GenSetListSafe(pGen, &pPowersUI->eaPurposeListNodes, PowersUIPurposeNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowersUIPurposeNode);
	}
}



// This is the list of powers within a purpose. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerList");
void PowersUI_GetPowerList(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_VALID PowersUIPurposeNode *pPurposeNode)
{
	if (pGen && pPurposeNode)
	{
		ui_GenSetListSafe(pGen, &pPurposeNode->eaPowerListNodes, PowerListNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowerListNode);
	}
}

static int SortPowerListNodeByName(const PowerListNode **ppLeft, const PowerListNode **ppRight, const void *pContext)
{
	PowerDef *pLeftPower = GET_REF((*ppLeft)->hPowerDef);
	PowerDef *pRightPower = GET_REF((*ppRight)->hPowerDef);
	const char *pchLeftName;
	const char *pchRightName;

	if (!pLeftPower || !pRightPower)
		return (!pRightPower) - (!pLeftPower);

	pchLeftName = TranslateDisplayMessage(pLeftPower->msgDisplayName);
	pchRightName = TranslateDisplayMessage(pRightPower->msgDisplayName);

	if (!pchLeftName || !pchRightName)
		return (!pchRightName) - (!pchLeftName);

	return stricmp(pchLeftName, pchRightName);
}

void PowersUI_GetPowerListForPurposes(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, PowersUIPurposeNode **eaPurposeNodes)
{
	S32 i;
	static PowerListNode **s_eaPowerListNodes = NULL;
	eaClear(&s_eaPowerListNodes);

	for (i = 0; i < eaSize(&eaPurposeNodes); i++)
		eaPushEArray(&s_eaPowerListNodes, &eaPurposeNodes[i]->eaPowerListNodes);
	eaQSort(s_eaPowerListNodes, SortPowerListNodeByPurpose);

	ui_GenSetListSafe(pGen, &s_eaPowerListNodes, PowerListNode);
}

void PowersUI_GetPowerListForPurposesAtPointsSpent(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, PowersUIPurposeNode **eaPurposeNodes, int iPointsSpentInAnyTree)
{
	S32 i,j;
	static PowerListNode **s_eaPowerListNodes = NULL;
	eaClear(&s_eaPowerListNodes);

	for (i = 0; i < eaSize(&eaPurposeNodes); i++)
	{
		for( j=0; j < eaSize(&eaPurposeNodes[i]->eaPowerListNodes); ++j)
		{
			PowerListNode *pPowerListNode = eaPurposeNodes[i]->eaPowerListNodes[j];
			PTNodeDef* pPTNodeDef = GET_REF(pPowerListNode->hNodeDef);
			int iRank;
			for (iRank = 0; iRank < eaSize(&pPTNodeDef->ppRanks); iRank++)
			{
				PTNodeRankDef* pRankDef = pPTNodeDef->ppRanks[iRank];
				if( pRankDef->pRequires && pRankDef->pRequires->iMinPointsSpentInAnyTree == iPointsSpentInAnyTree )
				{
					eaPush(&s_eaPowerListNodes, pPowerListNode);
					break;
				}
				// Stocker says he only wants this to look at Rank 0
				break;
			}
		}
	}
	eaQSort(s_eaPowerListNodes, SortPowerListNodeByPurpose);

	ui_GenSetListSafe(pGen, &s_eaPowerListNodes, PowerListNode);
}

SA_RET_OP_VALID TalentsUITree * PowersUI_GetTalentTreeNodeByName(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && pGen && pchName && pchName[0])
	{
		S32 i;

		for (i = 0; i < eaSize(&pPowersUI->eaTalentTrees); i++)
		{
			if (pPowersUI->eaTalentTrees[i] &&
				stricmp(pchName, pPowersUI->eaTalentTrees[i]->pchTreeName) == 0)
			{
				return pPowersUI->eaTalentTrees[i];
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetNumColsInTalentTree");
S32 PowersUI_GetNumColsInTalentTree(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char *pchName)
{
	if (pGen && pchName && pchName[0])
	{
		TalentsUITree *pTalentTree = PowersUI_GetTalentTreeNodeByName(pContext, pGen, pchName);
		return pTalentTree ? pTalentTree->iNumCols : 0;
	}
	return 0;
}

// This is the list of powers within a talent tree. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListForTalentTree");
void PowersUI_GetPowerListForTalentTree(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_VALID TalentsUITree *pTalentTree)
{
	if (pGen && pTalentTree)
	{
		ui_GenSetListSafe(pGen, &pTalentTree->eaTalentNodes, TalentsUITreeNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, TalentsUITreeNode);
	}
}

// This is the list of powers within the specified talent tree. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByTalentTreeName");
void PowersUI_GetPowerListByTalentTreeName(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	TalentsUITree *pTalentTree = PowersUI_GetTalentTreeNodeByName(pContext, pGen, pchName);

	if (pTalentTree)
	{
		PowersUI_GetPowerListForTalentTree(pContext, pGen, pTalentTree);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, TalentsUITreeNode);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_EntGetTalentTabs");
void PowersUI_EntGetTalentTabs(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pTabGen, SA_PARAM_OP_VALID UIGen *pPowersUIGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pPowersUIGen);
	if(pPowersUI && pTabGen && pPowersUIGen && pEnt)
	{
		TalentsUITree ***peaTalents = ui_GenGetManagedListSafe(pTabGen, TalentsUITree);
		S32 i;
		S32 j;
		eaClearStruct(peaTalents, parse_TalentsUITree);
		for(i = 0; i < eaSize(&pPowersUI->eaTreeDefRefs); i++)
		{
			PowerTreeDef *pTreeDef = GET_REF(pPowersUI->eaTreeDefRefs[i]->hRef);
			if(!pTreeDef || !entity_CanBuyPowerTreeHelper(PARTITION_CLIENT,CONTAINER_NOCONST(Entity, pEnt), pTreeDef, pTreeDef->bTemporary))
				continue;

			for (j = 0; j < eaSize(&pPowersUI->eaTalentTrees); j++)
			{
				if(pPowersUI->eaTalentTrees[j]->pchTreeName == pTreeDef->pchName)
				{
					eaPush(peaTalents, StructClone(parse_TalentsUITree, pPowersUI->eaTalentTrees[j]));
				}
			}
		}

		ui_GenSetManagedListSafe(pTabGen, peaTalents, TalentsUITree, true);
	}
	else if (pTabGen)
	{
		ui_GenSetList(pTabGen, NULL, parse_TalentsUITree);
	}
}

// Returns a PowersUIPurposeNode for the given name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowersUIPurposeNodeByName");
SA_RET_OP_VALID PowersUIPurposeNode * PowersUI_GetPowersUIPurposeNodeByName(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI && pGen && pchName && pchName[0])
	{
		S32 i;

		for (i = 0; i < eaSize(&pPowersUI->eaPurposeListNodes); i++)
		{
			if (pPowersUI->eaPurposeListNodes[i] &&
				stricmp(pchName, pPowersUI->eaPurposeListNodes[i]->pchName) == 0)
			{
				return pPowersUI->eaPurposeListNodes[i];
			}
		}
	}
	return NULL;
}

// This is the list of powers within the specified purpose. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByPurposeNodeName");
void PowersUI_GetPowerListByPurposeNodeName(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchName);
	PowersUI_GetPowerList(pContext, pGen, pPurposeNode);
}

// This is the list of powers within the specified purpose. These are the powers themselves. The list is sorted by the power node level.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByPurposeNodeNameSortedByLevel");
void PowersUI_GetPowerListByPurposeNodeNameSortedByLevel(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchName);

	if (pGen && pPurposeNode)
	{
		static PowerListNode **s_eaPowerListNodes = NULL;
		eaCopy(&s_eaPowerListNodes, &pPurposeNode->eaPowerListNodes);
		eaQSort(s_eaPowerListNodes, PowersUI_PowerListNodeLevelComparitor);
		
		ui_GenSetListSafe(pGen, &s_eaPowerListNodes, PowerListNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowerListNode);
	}
}

// This is the list of powers within the specified purpose. These are the powers themselves. The list is sorted by the power node level.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByPurposeNodeNameSortedByLevelWithAllRanks");
void PowersUI_GetPowerListByPurposeNodeNameSortedByLevelWithAllRanks(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchName);
	PowerListNode*** peaUINodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	Entity* pEnt = entActivePlayerPtr();
	int iPowerListNodeCount = 0;
	int iNode = 0;

	if (pGen && pPurposeNode)
	{
		for (iNode = 0; iNode < eaSize(&pPurposeNode->eaPowerListNodes); iNode++)
		{
			PTNodeDef* pNodeDef = GET_REF(pPurposeNode->eaPowerListNodes[iNode]->hNodeDef);
			int iRank = 0;
			if (pNodeDef)
			{
				for (iRank = 0; iRank < eaSize(&pNodeDef->ppRanks); iRank++)
				{
					if (iPowerListNodeCount >= eaSize(peaUINodes))
						eaPush(peaUINodes, StructClone(parse_PowerListNode, pPurposeNode->eaPowerListNodes[iNode]));
					else
						StructCopyAll(parse_PowerListNode, pPurposeNode->eaPowerListNodes[iNode], ((*peaUINodes)[iPowerListNodeCount]));
					
					
					COPY_HANDLE(((*peaUINodes)[iPowerListNodeCount])->hPowerDef, pNodeDef->ppRanks[iRank]->hPowerDef);
					((*peaUINodes)[iPowerListNodeCount])->iRank = iRank+1;
					((*peaUINodes)[iPowerListNodeCount])->iLevel = pNodeDef->ppRanks[iRank]->pRequires ? pNodeDef->ppRanks[iRank]->pRequires->iTableLevel : 1;
					((*peaUINodes)[iPowerListNodeCount])->bIsOwned = (pPurposeNode->eaPowerListNodes[iNode]->iRank >= ((*peaUINodes)[iPowerListNodeCount])->iRank);

					iPowerListNodeCount++;
				}
			}
		}

		eaSetSizeStruct(peaUINodes, parse_PowerListNode, iPowerListNodeCount);

		eaQSort(*peaUINodes, PowersUI_PowerListNodeLevelComparitor);
		
		ui_GenSetManagedListSafe(pGen, peaUINodes, PowerListNode, true);
	}
	else if (pGen)
	{
		ui_GenSetManagedListSafe(pGen, NULL, PowerListNode, true);
	}
}

// This is the list of powers within the specified purpose. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_CharacterPowerTreesAreLoaded");
bool PowersUI_CharacterPowerTreesAreLoaded(SA_PARAM_OP_VALID Entity *pEnt)
{
	if (pEnt && pEnt->pChar)
	{
		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppPowerTrees, PowerTree, pTree)
		{
			if (REF_IS_SET_BUT_ABSENT(pTree->hDef))
			{
				return false;
			}
		}
		FOR_EACH_END
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_AllPowerTreesAreLoaded");
bool PowersUI_AllPowerTreesAreLoaded(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pStateGen)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pStateGen);
	S32 i;
	for( i=0; i < eaSize(&pPowersUI->eaTreeDefRefs); ++i )
	{
		PowerTreeDefRef *pPowerTreeDefRef = pPowersUI->eaTreeDefRefs[i];
		if( GET_REF(pPowerTreeDefRef->hRef) == NULL )
			return false;
	}

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPaddedPowerListByPurposeAndLevelSorting");
void PowersUI_GetPaddedPowerListByPurposeAndLevelSorting(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID UIGen *pStateGen, SA_PARAM_NN_STR const char * pchName)
{
	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pStateGen, pchName);
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pStateGen);
	bool bSkipThisFrame = false;

	if (pPowersUI && 
		pPowersUI->ePurposeListPowerSortingMethod != PurposeListPowerSortingMethod_Level)
	{
		// This function can only work with level sorting
		pPowersUI->ePurposeListPowerSortingMethod = PurposeListPowerSortingMethod_Level;

		// Skip this frame as the data is not sorted
		bSkipThisFrame = true;
	}

	if (pGen && pPowersUI && pPurposeNode && !bSkipThisFrame)
	{
		static PowerListNode **s_eaPurposeListNodes = NULL;
		static PowerListNode emptyPowerListNode = { 0 };
		S32 iPowerListNodeCount = 0;
		S32 iCurLevel;
		S32 iCurLevelPowerCount = 0;
		S32 itPower = 0;
		S32 itPowerLevelRangeBegin;

		emptyPowerListNode.bIsEmpty = true;

		for (iCurLevel = 1; iCurLevel <= pPowersUI->iMaxPowerLevelInPurposeNodes; iCurLevel++)
		{
			iCurLevelPowerCount = 0;
			itPowerLevelRangeBegin = itPower;

			while (itPower < eaSize(&pPurposeNode->eaPowerListNodes) &&
				pPurposeNode->eaPowerListNodes[itPower]->iLevel <= iCurLevel)
			{
				// Increment the number of powers for this level
				iCurLevelPowerCount++;

				// Add to the list
				if (iPowerListNodeCount < eaSize(&s_eaPurposeListNodes))
				{
					s_eaPurposeListNodes[iPowerListNodeCount] = pPurposeNode->eaPowerListNodes[itPower];
				}
				else
				{
					eaPush(&s_eaPurposeListNodes, pPurposeNode->eaPowerListNodes[itPower]);
				}
				++iPowerListNodeCount;

				// Go to the next power
				++itPower;
			}

			if (iCurLevelPowerCount > 0)
			{
				// Set the number of powers per level in each power list node
				S32 i;
				for (i = itPowerLevelRangeBegin; i < itPower; i++)
				{
					pPurposeNode->eaPowerListNodes[i]->iNumPowersInSameLevel = iCurLevelPowerCount;
				}
			}
			else
			{
				// Push an empty power list node
				if (iPowerListNodeCount < eaSize(&s_eaPurposeListNodes))
				{
					s_eaPurposeListNodes[iPowerListNodeCount] = &emptyPowerListNode;
				}
				else
				{
					eaPush(&s_eaPurposeListNodes, &emptyPowerListNode);
				}
				++iPowerListNodeCount;
			}
		}

		// Set the final size of the array
		eaSetSize(&s_eaPurposeListNodes, iPowerListNodeCount);

		ui_GenSetListSafe(pGen, &s_eaPurposeListNodes, PowerListNode);
	}
	else if (pGen)
	{
		ui_GenSetListSafe(pGen, NULL, PowerListNode);
	}
}

// This is the list of powers within the list of purposes. These are the powers themselves.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByPurposeNodeNameList");
void PowersUI_GetPowerListByPurposeNodeNameList(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchNameList)
{
	char *pchBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	PowersUIPurposeNode **eaPurposes = NULL;

	strdup_alloca(pchBuffer, pchNameList);
	eaStackCreate(&eaPurposes, 1000);

	if ((pchToken = strtok_r(pchBuffer, " ,\r\n\t", &pchContext)) != NULL)
	{
		do
		{
			PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchToken);

			if (pPurposeNode)
				eaPush(&eaPurposes, pPurposeNode);
		} while ((pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext)) != NULL);
	}

	PowersUI_GetPowerListForPurposes(pContext, pGen, eaPurposes);
}

// This is the list of powers within the list of purposes, but only the ones with exactly the specified PointsSpentInAnyTree.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListByPurposeNodeNameListAtPointsSpent");
void PowersUI_GetPowerListByPurposeNodeNameListAtPointsSpent(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchNameList, int iPointsSpent)
{
	char *pchBuffer = NULL;
	char *pchContext = NULL;
	char *pchToken;
	PowersUIPurposeNode **eaPurposes = NULL;

	strdup_alloca(pchBuffer, pchNameList);
	eaStackCreate(&eaPurposes, 1000);

	if ((pchToken = strtok_r(pchBuffer, " ,\r\n\t", &pchContext)) != NULL)
	{
		do
		{
			PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchToken);

			if (pPurposeNode)
				eaPush(&eaPurposes, pPurposeNode);
		} while ((pchToken = strtok_r(NULL, " ,\r\n\t", &pchContext)) != NULL);
	}

	PowersUI_GetPowerListForPurposesAtPointsSpent(pContext, pGen, eaPurposes, iPointsSpent);
}

// Returns the comma separated list of owned powers from the given group
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetStringListOfOwnedPowersFromGroup");
SA_RET_NN_STR const char * PowersUI_GetStringListOfOwnedPowersFromGroup(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_NN_STR const char * pchGroupName)
{
	S32 i, iOwned = 0;
	static char *estrOwnedPowerList = NULL;
	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchGroupName);

	if (pPurposeNode == NULL)
		return "";

	// Clear the string
	estrClear(&estrOwnedPowerList);

	for (i = 0; i < eaSize(&pPurposeNode->eaPowerListNodes); i++)
	{
		if (pPurposeNode->eaPowerListNodes[i] && pPurposeNode->eaPowerListNodes[i]->bIsOwned)
		{
			PowerDef *pPowerDef = GET_REF(pPurposeNode->eaPowerListNodes[i]->hPowerDef);
			Message *pMessage = NULL;
			if (pPowerDef && (pMessage = GET_REF(pPowerDef->msgDisplayName.hMessage)))
			{
				// Add the comma if needed
				if (iOwned > 0)
					estrAppend2(&estrOwnedPowerList, ", ");

				estrAppend2(&estrOwnedPowerList, TranslateMessagePtrSafe(pMessage, pPowerDef->pchName));

				iOwned++;
			}
		}
	}

	if (estrLength(&estrOwnedPowerList) == 0)
	{
		return "";
	}
	else
	{
		return exprContextAllocString(pContext, estrOwnedPowerList);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ExpandUICategory");
void PowersUI_ExpandUICategory(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowersUICategoryNode *pCategoryNode, bool bExpand)
{
	if (pCategoryNode)
	{
		pCategoryNode->bExpand = bExpand;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ExpandAllUICategories");
void PowersUI_ExpandAllUICategories(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, bool bExpand)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		int i;
		for (i = 0 ; i < eaSize(&pPowersUI->eaUICategories); i++)
		{
			pPowersUI->eaUICategories[i]->bExpand = bExpand;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SelectPowerTree");
void PowersUI_SelectPowerTree(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowersUITreeNode *pTreeNode, bool bSelected)
{
	if (pTreeNode)
	{
		pTreeNode->bSelected = bSelected;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SelectPowerTreeByName");
void PowersUI_SelectPowerTreeByName(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char* pchTreeName, bool bSelected)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	int i, j;
	if (pPowersUI)
	{
		for (i = 0; i < eaSize(&pPowersUI->eaUICategories); i++)
		{
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[i];
			for (j = 0; j < eaSize(&pCategoryNode->eaTreeNodes); j++)
			{
				PowersUITreeNode *pTreeNode = pCategoryNode->eaTreeNodes[j];
				if (stricmp(pTreeNode->pchTreeName, pchTreeName) == 0)
				{
					pTreeNode->bSelected = true;
					return;
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SelectAllPowerTreesInCategory");
void PowersUI_SelectAllPowerTreesInCategory(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID PowersUICategoryNode *pCategoryNode, bool bSelected)
{
	if (pCategoryNode)
	{
		int i;
		for (i = 0 ; i < eaSize(&pCategoryNode->eaTreeNodes); i++)
		{
			PowersUITreeNode *pTreeNode = pCategoryNode->eaTreeNodes[i];
			pTreeNode->bSelected = bSelected;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SelectAllPowerTrees");
void PowersUI_SelectAllPowerTrees(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, bool bSelected)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		int i;
		for (i = 0 ; i < eaSize(&pPowersUI->eaUICategories); i++)
		{
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[i];
			PowersUI_SelectAllPowerTreesInCategory(pContext, pGen, pCategoryNode, bSelected);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetFilterText");
void PowersUI_SetFilterText(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char *pchFilterText)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->pchFilterText = pchFilterText;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetFiltersEx");
void PowersUI_SetFiltersEx(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen,
						   bool bShowOwned,
						   bool bShowAvailable,
						   bool bShowUnavailable,
						   bool bFilterOwnedByComparisonEntity,
						   bool bAddAvailableForComparisonEntity)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->bShowOwned = bShowOwned;
		pPowersUI->bShowAvailable = bShowAvailable;
		pPowersUI->bShowUnavailable = bShowUnavailable;
		pPowersUI->bFilterOwnedByComparisonEntity = bFilterOwnedByComparisonEntity;
		pPowersUI->bAddAvailableForComparisonEntity = bAddAvailableForComparisonEntity;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetPurposeListPowerSortingMethod");
void PowersUI_SetPurposeListPowerSortingMethod(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, S32 eSortingMethod)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->ePurposeListPowerSortingMethod = eSortingMethod;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetFilters");
void PowersUI_SetFilters(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, bool bShowOwned, bool bShowAvailable, bool bShowUnavailable)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		PowersUI_SetFiltersEx(pContext, pGen, bShowOwned, bShowAvailable,
			bShowUnavailable, pPowersUI->bFilterOwnedByComparisonEntity, pPowersUI->bAddAvailableForComparisonEntity);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_SetAttribModFilters");
void PowersUI_SetAttribModFilters(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, const char *pchFilterModList)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->pchFilterModList = pchFilterModList;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_ExpandOwnedTreesOnLoad");
void PowersUI_ExpandOwnedTreesOnLoad(ExprContext *pContext, SA_PARAM_OP_VALID UIGen *pGen, bool bExpandOwned)
{
	PowersUIState *pPowersUI = PowersUI_GetPowersUIState(pContext, pGen);
	if (pPowersUI)
	{
		pPowersUI->bExpandOwnedOnLoad = bExpandOwned;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsPowerNodeOwned");
bool PowersUI_IsPowerNodeOwned(ExprContext *pContext, SA_PARAM_OP_VALID PowerListNode *pNode)
{
	if (pNode == NULL)
	{
		return false;
	}
	else
	{
		return pNode->bIsOwned;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsPowerNodeAvailable");
bool PowersUI_IsPowerNodeAvailable(ExprContext *pContext, SA_PARAM_OP_VALID PowerListNode *pNode)
{
	if (pNode == NULL)
	{
		return false;
	}
	else
	{
		return pNode->bIsAvailable;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_PowerTreeFromPowerInPurpose");
const char *PowersUI_PowerTreeFromPowerInPurpose(ExprContext *pContext,
												 SA_PARAM_OP_VALID Entity *pEnt,
												 const char *pchPurpose)
{
	S32 i, j;
	PowerPurpose ePurpose = StaticDefineIntGetInt(PowerPurposeEnum, pchPurpose);
	if (pEnt && pEnt->pChar)
	{
		for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
			PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);

			// EVIL
			if (stricmp(pTreeDef->pchName, "Innate") == 0)
				continue;

			if (pTree && pTree->ppNodes)
			{
				for (j = 0; j < eaSize(&pTree->ppNodes); j++)
				{
					PTNode *pNode = pTree->ppNodes[j];
					PTNodeDef *pNodeDef = pNode ? GET_REF(pNode->hDef) : NULL;

					if (pNodeDef)
					{
						PTNodeRankDef *pRankDef = eaTail(&pNodeDef->ppRanks);
						PowerDef *pPowerDef = GET_REF(pRankDef->hPowerDef);
						if (pPowerDef && pPowerDef->ePurpose == ePurpose)
						{
							return pTreeDef ? pTreeDef->pchName : "";
						}
					}
				}
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetMostPopulatedCategoryIndex");
S32 PowersUI_GetMostPopulatedCategoryIndex(ExprContext *pContext,
										   SA_PARAM_OP_VALID UIGen *pGen,
										   SA_PARAM_OP_VALID Entity *pEnt)
{
	PowersUIState *pPowersUI = PowersUI_GetUpdatedPowersUIState(pContext, pGen);
	S32 iIndex = -1;
	S32 iMax = -1;
	if (pPowersUI && SAFE_MEMBER(pEnt, pChar))
	{
		// The index of eaiPowerCounts matches up with pEnt->pChar->ppPowerTrees
		S32* eaiPowerCounts = NULL;
		S32* eaiCategoryCounts = NULL;
		int i, j, k;

		// Get the total powers owned in each tree
		for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
			if (pTree)
				eaiPush(&eaiPowerCounts, eaSize(&pTree->ppNodes));
		}

		// Get the total powers owned in each category
		for (i = 0; i < eaSize(&pPowersUI->eaUICategories); i++)
		{
			PowersUICategoryNode *pCategoryNode = pPowersUI->eaUICategories[i];
			eaiPush(&eaiCategoryCounts, 0);
			for (j = 0; j < eaSize(&pCategoryNode->eaTreeNodes); j++)
			{
				PowersUITreeNode *pTreeNode = pCategoryNode->eaTreeNodes[j];
				S32 iTreeIndex = -1;
				// Find the correct index into the array
				for (k = 0; k < eaSize(&pEnt->pChar->ppPowerTrees); k++)
				{
					PowerTree *pTree = pEnt->pChar->ppPowerTrees[k];
					if (GET_REF(pTree->hDef) && GET_REF(pTree->hDef) == GET_REF(pTreeNode->hTreeDef))
					{
						iTreeIndex = k;
						break;
					}
				}
				// Add the power count from that index to the running total for this category
				if (iTreeIndex >= 0)
				{
					S32* piCount = &eaiCategoryCounts[eaiSize(&eaiCategoryCounts)-1];
					*piCount += eaiPowerCounts[iTreeIndex];
				}
			}
		}

		// Find the max of the categories
		for (i = 0; i < eaiSize(&eaiCategoryCounts); i++)
		{
			if (eaiCategoryCounts[i] > iMax)
			{
				iMax = eaiCategoryCounts[i];
				iIndex = i;
			}
		}
	}
	return iIndex;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_HasEnoughEnergyForPower");
bool PowersUI_HasEnoughEnergyForPower(ExprContext *pContext,
									  SA_PARAM_OP_VALID Entity *pEntity,
									  SA_PARAM_OP_VALID PowerListNode *pNode)
{
	bool bEnoughPower = false;
	if (SAFE_MEMBER(pEntity, pChar) && pNode)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		static char *s_pchDescription = NULL;
		F32 fPowerMax = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPowerMax);
		AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
		Power *pPower = pNode->pPower;
		PowerDef *pPowerDef = GET_REF(pNode->hPowerDef);
		PowerTreeDef *pTreeDef = GET_REF(pNode->hTreeDef);
		PTGroupDef *pGroupDef = GET_REF(pNode->hGroupDef);
		if (pPower)
		{
			// NNO HACK: Only passing &s_pchDescription here because NNO hack breaks without it
			power_AutoDesc(entGetPartitionIdx(pEntity), pPower, pEntity->pChar, &s_pchDescription, pAutoDescPower, NULL, NULL, NULL, false, 0, entGetPowerAutoDescDetail(pEntity,false), pExtract,NULL);
		}
		else if (pPowerDef)
		{
			// NNO HACK: Only passing &s_pchDescription here because NNO hack breaks without it
			powerdef_AutoDesc(entGetPartitionIdx(pEntity), pPowerDef, &s_pchDescription, pAutoDescPower, NULL, NULL, NULL,
				pEntity->pChar, pPower, NULL, pEntity->pChar->iLevelCombat, true, entGetPowerAutoDescDetail(pEntity,false), pExtract, NULL);
		}
		if (pPower || pPowerDef)
		{
			powerdef_ConsolidateAutoDesc(pAutoDescPower);
			bEnoughPower = fPowerMax > pAutoDescPower->fCostMin;
		}
		StructDestroy(parse_AutoDescPower, pAutoDescPower);
	}
	return bEnoughPower;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListNodeDescriptionEx");
const char* PowersUI_GetPowerListNodeDescriptionEx(ExprContext *pContext,
	SA_PARAM_OP_VALID Entity *pEntity,
	SA_PARAM_OP_VALID PowerListNode *pNode,
	SA_PARAM_OP_STR   const char *pchMessageKey,
	SA_PARAM_OP_STR   const char *pchPowerAutoDescMessageKey)
{
	static char *s_pchDescription = NULL;

#define FormatRangedVar(varname) \
	if (!nearf(pAutoDescPower->f##varname##Min, -1.f)) \
		if (nearf(pAutoDescPower->f##varname##Min, pAutoDescPower->f##varname##Max))\
			FormatGameString(&pch##varname, "{Value}", STRFMT_FLOAT("Value", pAutoDescPower->f##varname##Min), STRFMT_END); \
		else \
			FormatGameString(&pch##varname, "{Min}-{Max}", STRFMT_FLOAT("Min", MIN(pAutoDescPower->f##varname##Min, pAutoDescPower->f##varname##Max)), STRFMT_FLOAT("Max", MAX(pAutoDescPower->f##varname##Min, pAutoDescPower->f##varname##Max)), STRFMT_END); \
	else \
		FormatGameString(&pch##varname, "{Value}", STRFMT_FLOAT("Value", 0), STRFMT_END);

	estrClear(&s_pchDescription);
	if (pEntity && pNode)
	{
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
		F32 fPowercost = 0;
		F32 fPowerMax = SAFE_MEMBER3(pEntity, pChar, pattrBasic, fPowerMax);
		Power *pPower = pNode->pPower;
		PowerDef *pPowerDef = GET_REF(pNode->hPowerDef);
		PowerTreeDef *pTreeDef = GET_REF(pNode->hTreeDef);
		PTGroupDef *pGroupDef = GET_REF(pNode->hGroupDef);

		char *pchPowerCost = NULL;
		char *pchPowerTime = NULL;
		char *pchRequirements = NULL;

		const char *pchMsgDescription = NULL;
		const char *pchMsgDescriptionLong = NULL;
		const char *pchMsgDescriptionFlavor = NULL;
		const char *pchMsgRankChange = NULL;

		const char *pchTreeDefName = pTreeDef ? TranslateDisplayMessage(pTreeDef->pDisplayMessage) : NULL;

		//////////////////////////////////////////////////////////////////////////
		// Get the Autodesc
		//////////////////////////////////////////////////////////////////////////
		AutoDescPower *pAutoDescPower = StructCreate(parse_AutoDescPower);
		if (pPower)
		{
			// NNO HACK: Only passing &s_pchDescription here because NNO hack breaks without it
			power_AutoDesc(entGetPartitionIdx(pEntity), pPower, pEntity->pChar, &s_pchDescription, pAutoDescPower, NULL, NULL, NULL, false, 0, entGetPowerAutoDescDetail(pEntity,false), pExtract, pchPowerAutoDescMessageKey);
		}
		else if (pPowerDef)
		{
			// NNO HACK: Only passing &s_pchDescription here because NNO hack breaks without it
			powerdef_AutoDesc(entGetPartitionIdx(pEntity), pPowerDef, &s_pchDescription, pAutoDescPower, NULL, NULL, NULL,
				pEntity->pChar, pPower, NULL, pEntity->pChar->iLevelCombat, true, entGetPowerAutoDescDetail(pEntity,false), pExtract, pchPowerAutoDescMessageKey);
		}
		else
		{
			StructDestroy(parse_AutoDescPower, pAutoDescPower);
			return "";
		}
		powerdef_ConsolidateAutoDesc(pAutoDescPower);

		//////////////////////////////////////////////////////////////////////////
		// Format Cost
		//////////////////////////////////////////////////////////////////////////
		{
			const char *pchCostMessageKey = NULL;

			if ((pAutoDescPower->fCostMin > 0) && (pAutoDescPower->fCostPeriodicMin > 0))
			{
				pchCostMessageKey = "PowersUI.PowerCost.CostAndCostPeriodic";
			}
			else if (pAutoDescPower->fCostPeriodicMin > 0)
			{
				pchCostMessageKey = "PowersUI.PowerCost.CostPeriodic";
			}
			else if (pAutoDescPower->fCostMin > 0)
			{
				pchCostMessageKey = "PowersUI.PowerCost.Cost";
			}

			if (pAutoDescPower && pchCostMessageKey)
			{
				char *pchCost = NULL;
				char *pchCostPeriodic = NULL;
				char *pchTimeActivatePeriod = NULL;
				FormatRangedVar(Cost);
				FormatRangedVar(CostPeriodic);

				if (!nearf(pAutoDescPower->fTimeActivatePeriodMin, 1.f))
				{
					FormatRangedVar(TimeActivatePeriod);
				}
				else
				{
					FormatGameString(&pchTimeActivatePeriod, "{Value}", STRFMT_STRING("Value", ""), STRFMT_END); \
				}

				fPowercost = pAutoDescPower->fCostMin;
				FormatGameMessageKey(&pchPowerCost, pchCostMessageKey,
					STRFMT_STRING("Cost", NULL_TO_EMPTY(pchCost)),
					STRFMT_STRING("CostPeriodic", NULL_TO_EMPTY(pchCostPeriodic)),
					STRFMT_STRING("TimeActivatePeriod", NULL_TO_EMPTY(pchTimeActivatePeriod)),
					STRFMT_END);
				estrDestroy(&pchCost);
				estrDestroy(&pchCostPeriodic);
				estrDestroy(&pchTimeActivatePeriod);
			}
		}

		//////////////////////////////////////////////////////////////////////////
		// format activate time
		//////////////////////////////////////////////////////////////////////////
		// There are five cases here that we care about:
		//  Activate
		//  Activate + Maintain
		//  Activate + Charge
		//  Maintain
		//  Charge
		//  and one special case: Instant, which is none
		{
			char *pchTimeActivate = NULL;
			char *pchTimeMaintain = NULL;
			char *pchTimeCharge = NULL;

			bool bChargeMinAdjusted = false;
			bool bChargeMaxAdjusted = false;

			const char *pchTimeMessageKey = NULL;

			FormatRangedVar(TimeActivate);
			FormatRangedVar(TimeMaintain);

			if (!nearf(pAutoDescPower->fTimeActivateMin, -1.f)
				&& !nearf(pAutoDescPower->fTimeChargeMin, -1.f))
			{
				pAutoDescPower->fTimeChargeMin += pAutoDescPower->fTimeActivateMin;
				bChargeMinAdjusted = true;
			}
			if (!nearf(pAutoDescPower->fTimeActivateMax, -1.f)
				&& !nearf(pAutoDescPower->fTimeChargeMax, -1.f))
			{
				pAutoDescPower->fTimeChargeMax += pAutoDescPower->fTimeActivateMax;
				bChargeMaxAdjusted = true;
			}

			FormatRangedVar(TimeCharge);

			if (bChargeMinAdjusted)
				pAutoDescPower->fTimeChargeMin -= pAutoDescPower->fTimeActivateMin;
			if (bChargeMaxAdjusted)
				pAutoDescPower->fTimeChargeMax -= pAutoDescPower->fTimeActivateMax;

			if (!nearf(pAutoDescPower->fTimeActivateMin, -1.f))
			{
				if (!nearf(pAutoDescPower->fTimeChargeMin, -1.f))
					pchTimeMessageKey = "PowersUI.PowerTime.ActivateAndCharge";
				else if (!nearf(pAutoDescPower->fTimeMaintainMin, -1.f))
					pchTimeMessageKey = "PowersUI.PowerTime.ActivateAndMaintain";
				else if (nearf(pAutoDescPower->fTimeActivateMin, 0.f))
					pchTimeMessageKey = "PowersUI.PowerTime.Instant";
				else
					pchTimeMessageKey = "PowersUI.PowerTime.Activate";
			}
			else
			{
				if (!nearf(pAutoDescPower->fTimeChargeMin, -1.f))
					pchTimeMessageKey = "PowersUI.PowerTime.Charge";
				else if (!nearf(pAutoDescPower->fTimeMaintainMin, -1.f))
					pchTimeMessageKey = "PowersUI.PowerTime.Maintain";
				else
				{
					pchTimeMessageKey = "PowersUI.PowerTime.Instant";
				}
			}
			if (pchTimeMessageKey)
			{
				FormatGameMessageKey(&pchPowerTime, pchTimeMessageKey,
					STRFMT_STRING("TimeActivate", NULL_TO_EMPTY(pchTimeActivate)),
					STRFMT_STRING("TimeMaintain", NULL_TO_EMPTY(pchTimeMaintain)),
					STRFMT_STRING("TimeCharge", NULL_TO_EMPTY(pchTimeCharge)),
					STRFMT_END);
			}
			estrDestroy(&pchTimeActivate);
			estrDestroy(&pchTimeMaintain);
			estrDestroy(&pchTimeCharge);
		}

		//////////////////////////////////////////////////////////////////////////
		// Format purchase requirements
		//////////////////////////////////////////////////////////////////////////
		// This is a kinda stupid CO specific hack.
		// The only reliable way to tell what a power's requirements
		// will be are by what tier group its in, and certain ones,
		// like sorcery, don't use the exact names "tier_1", but instead
		// use things like "tier_1_blah" so I'm just going to hard code this
		if(stricmp(GetShortProductName(),"FC")==0)
		{
			const char *pchTierMessageKey = "";
			if (pGroupDef && strchr(pGroupDef->pchGroup, '1'))
			{
				pchTierMessageKey = "PowersUI.PurchaseRequirementsTier1";
			}
			else if (pGroupDef && strchr(pGroupDef->pchGroup, '2'))
			{
				pchTierMessageKey = "PowersUI.PurchaseRequirementsTier2";
			}
			else if (pGroupDef && strchr(pGroupDef->pchGroup, '3'))
			{
				pchTierMessageKey = "PowersUI.PurchaseRequirementsTier3";
			}
			else if (pGroupDef && strchr(pGroupDef->pchGroup, '4'))
			{
				pchTierMessageKey = "PowersUI.PurchaseRequirementsTier4";
			}
			else
			{
				PTNodeDef *pNodeDef = pNode ? GET_REF(pNode->hNodeDef) : NULL;
				PTNodeRankDef *pRankDef = pNodeDef ? eaHead(&pNodeDef->ppRanks) : NULL;
				if (pRankDef && stricmp(pRankDef->pchCostTable, "endbuildpoints") == 0) // Really terrible.
				{
					pchTierMessageKey = "PowersUI.PurchaseRequirementsEndBuilder";
				}
			}
			if (pchTierMessageKey[0])
			{
				FormatGameMessageKey(&pchRequirements, pchTierMessageKey,
					STRFMT_STRING("Framework", NULL_TO_EMPTY(pchTreeDefName)),
					STRFMT_MESSAGE("Category", StaticDefineGetMessage(PowerTreeUICategoryEnum, pTreeDef->eUICategory)),
					STRFMT_END);
			}
		}

		if (pPowerDef)
		{
			pchMsgDescription = TranslateDisplayMessage(pPowerDef->msgDescription);
			pchMsgDescriptionLong = TranslateDisplayMessage(pPowerDef->msgDescriptionLong);
			pchMsgDescriptionFlavor = TranslateDisplayMessage(pPowerDef->msgDescriptionFlavor);
			pchMsgRankChange = TranslateDisplayMessage(pPowerDef->msgRankChange);
		}

		FormatGameMessageKey(&s_pchDescription, pchMessageKey,
			STRFMT_STRING("Name", NULL_TO_EMPTY(pAutoDescPower->pchName)),
			STRFMT_STRING("Framework", NULL_TO_EMPTY(pchTreeDefName)),
			STRFMT_STRING("Cost", NULL_TO_EMPTY(pchPowerCost)),
			STRFMT_STRING("TimeActivate", NULL_TO_EMPTY(pchPowerTime)),
			STRFMT_STRING("Damage", ""),
			STRFMT_INT("Rank", pNode->iRank),
			STRFMT_STRING("Targets", NULL_TO_EMPTY(pAutoDescPower->pchTarget)),
			STRFMT_STRING("Range", NULL_TO_EMPTY(pAutoDescPower->pchRange)),
			STRFMT_STRING("PurchseRequirements", pchRequirements),
			STRFMT_STRING("Description", NULL_TO_EMPTY(pchMsgDescription)),
			STRFMT_STRING("DescriptionLong", NULL_TO_EMPTY(pchMsgDescriptionLong)),
			STRFMT_STRING("DescriptionFlavor", NULL_TO_EMPTY(pchMsgDescriptionFlavor)),
			STRFMT_STRING("RankChange", NULL_TO_EMPTY(pchMsgRankChange)),
			STRFMT_INT("EnoughPower", (fPowerMax > fPowercost)),
			STRFMT_END);

		estrDestroy(&pchPowerCost);
		estrDestroy(&pchPowerTime);
		estrDestroy(&pchRequirements);
		StructDestroy(parse_AutoDescPower, pAutoDescPower);
	}

	return s_pchDescription;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListNodeDescription");
const char* PowersUI_GetPowerListNodeDescription(ExprContext *pContext,
	SA_PARAM_OP_VALID Entity *pEntity,
	SA_PARAM_OP_VALID PowerListNode *pNode,
	SA_PARAM_OP_STR   const char *pchMessageKey)
{
	return PowersUI_GetPowerListNodeDescriptionEx(pContext, pEntity, pNode, pchMessageKey, "AutoDesc.NNOPowerFormat");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_UseStatDescription");
bool PowersUI_UseStatDescription(ExprContext *pContext, SA_PARAM_OP_VALID PowerListNode *pNode)
{
	PowerTreeDef *pTreeDef = pNode ? GET_REF(pNode->hTreeDef) : NULL;
	if (pTreeDef)
	{
		// Stupid CO specific hack
		return (stricmp(pTreeDef->pchName, "Talents") == 0)
			|| (stricmp(pTreeDef->pchName, "Framework_Talents") == 0)
			|| (stricmp(pTreeDef->pchName, "SuperStats") == 0)
			|| (stricmp(pTreeDef->pchName, "SecondaryStats") == 0);
	}
	return false;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListNodeStatDescriptionHighlight");
const char* PowersUI_GetPowerListNodeStatDescriptionHighlight(ExprContext *pContext,
															  SA_PARAM_OP_VALID Entity *pEntity,
															  SA_PARAM_OP_VALID PowerListNode *pNode,
															  const char *pchHeaderMessageKey,
															  const char *pchRowMessageKey,
															  const char *pchHighlight)
{
	static char *s_pchDescription = NULL;
	static char *s_estrNbspName = NULL;
	PowerDef *pDef = pNode ? GET_REF(pNode->hPowerDef) : NULL;
	estrClear(&s_pchDescription);
	if (pDef && SAFE_MEMBER(pEntity, pChar))
	{
		AutoDescAttribMod **eaInfos = NULL;
		const char *pchName = TranslateDisplayMessage(pDef->msgDisplayName);
		const char *pchDescription = TranslateDisplayMessage(pDef->msgDescriptionLong);
		S32 i;
		bool bOneWord = false;
		AutoDesc_InnateAttribMods(entGetPartitionIdx(pEntity), pEntity->pChar, pDef, &eaInfos);
		if (!eaInfos)
			return s_pchDescription;

		estrClear(&s_estrNbspName);
		if (pchName && strchr(pchName, ' ') != NULL)
		{
			i = 0;
			for (; pchName[i] != '\0'; i++)
			{
				if (pchName[i] == ' ')
					estrAppend2(&s_estrNbspName, "&nbsp;");
				else
					estrConcatChar(&s_estrNbspName, pchName[i]);
			}
		}
		else
		{
			estrAppend2(&s_estrNbspName, pchName);
			bOneWord = true;
		}

		FormatGameMessageKey(&s_pchDescription, pchHeaderMessageKey,
			STRFMT_STRING("Name", NULL_TO_EMPTY(pchName)),
			STRFMT_STRING("Description", NULL_TO_EMPTY(pchDescription)),
			STRFMT_STRING("NameSMFNoWrap", NULL_TO_EMPTY(s_estrNbspName)),
			STRFMT_INT("OneWord", bOneWord),
			STRFMT_END);

		estrAppend2(&s_pchDescription, "<table>");
		for (i =0; i < eaSize(&eaInfos); i++)
		{
			AutoDescAttribMod *pInfo = eaGet(&eaInfos, i);
			const char *pchAttrib = StaticDefineIntRevLookup(AttribTypeEnum, pInfo->offAttrib);
			estrAppend2(&s_pchDescription, "<tr>");
			FormatGameMessageKey(&s_pchDescription, pchRowMessageKey,
				STRFMT_INT("Selected", pchAttrib && pchHighlight && *pchAttrib && strstri(pchHighlight, pchAttrib) != NULL ? 1 : 0),
				STRFMT_STRING("Magnitude", pInfo->pchMagnitude),
				STRFMT_STRING("Name", pInfo->pchAttribName),
				STRFMT_STRING("Description", pInfo->pchAttribDesc),
				STRFMT_END);
			estrAppend2(&s_pchDescription, "</tr>");
		}
		estrAppend2(&s_pchDescription, "</table>");
		eaDestroyStruct(&eaInfos, parse_AutoDescAttribMod);
	}
	return s_pchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListNodeStatDescription");
const char* PowersUI_GetPowerListNodeStatDescription(ExprContext *pContext,
													 SA_PARAM_OP_VALID Entity *pEntity,
													 SA_PARAM_OP_VALID PowerListNode *pNode,
													 const char *pchHeaderMessageKey,
													 const char *pchRowMessageKey)
{
	return PowersUI_GetPowerListNodeStatDescriptionHighlight(pContext, pEntity, pNode, pchHeaderMessageKey, pchRowMessageKey, NULL);
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetPowerListNodeAdvantages");
const char* PowersUI_GetPowerListNodeAdvantages(ExprContext *pContext,
												SA_PARAM_OP_VALID Entity *pEntity,
												SA_PARAM_OP_VALID PowerListNode *pNode,
												const char *pchHeaderMessageKey,
												const char *pchRowMessageKey)
{
	static char *s_pchDescription = NULL;
	PowerDef *pDef = pNode ? GET_REF(pNode->hPowerDef) : NULL;
	estrClear(&s_pchDescription);
	if (pDef && SAFE_MEMBER(pEntity, pChar))
	{
		AutoDescAttribMod **eaInfos = NULL;
		S32 i;
		const char *pchName = TranslateDisplayMessage(pDef->msgDisplayName);
		const char *pchDescription = TranslateDisplayMessage(pDef->msgDescriptionLong);
		AutoDesc_InnateAttribMods(entGetPartitionIdx(pEntity), pEntity->pChar, pDef, &eaInfos);
		if (!eaInfos)
			return s_pchDescription;

		FormatGameMessageKey(&s_pchDescription, pchHeaderMessageKey,
			STRFMT_STRING("Name", NULL_TO_EMPTY(pchName)),
			STRFMT_STRING("Description", NULL_TO_EMPTY(pchDescription)),
			STRFMT_END);

		estrAppend2(&s_pchDescription, "<table>");
		for (i =0; i < eaSize(&eaInfos); i++)
		{
			AutoDescAttribMod *pInfo = eaGet(&eaInfos, i);
			estrAppend2(&s_pchDescription, "<tr>");
			FormatGameMessageKey(&s_pchDescription, pchRowMessageKey,
				STRFMT_STRING("Magnitude", pInfo->pchMagnitude),
				STRFMT_STRING("Name", pInfo->pchAttribName),
				STRFMT_STRING("Description", pInfo->pchAttribDesc),
				STRFMT_END);
			estrAppend2(&s_pchDescription, "</tr>");
		}
		estrAppend2(&s_pchDescription, "</table>");
		eaDestroyStruct(&eaInfos, parse_AutoDescAttribMod);
	}
	return s_pchDescription;
}

// expression to allow checking for number of powers in power tree with thids category and purpose
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetNumPowersWithCatAndPurpose");
S32 PowersUI_GetNumPowersWithCatAndPurpose(SA_PARAM_OP_VALID Entity *pEntity, char *pcCategory, char *pcPurpose)
{
	return PowersUI_GetNumPowersWithCatAndPurposeInternal(pEntity, pcCategory, pcPurpose);
}

// Returns if the given node is the next suggested node from the given cost table
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsNextSuggestedNodeFromCostTable");
bool PowersUI_IsNextSuggestedNodeFromCostTable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_OP_VALID PowerListNode *pPowerListNode)
{
	PTNodeDef *pNodeDef;
	bool bResult;

	if (pchCostTable == NULL || pchCostTable[0] == 0 || pEntity == NULL || pPowerListNode == NULL || (pNodeDef = GET_REF(pPowerListNode->hNodeDef)) == NULL)
		return false;

	bResult = CharacterPath_IsNextSuggestedNodeFromCostTable(PARTITION_CLIENT, pEntity, pchCostTable, pNodeDef);
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsSuggestedNodeFromCostTableInCharacterCreation");
bool PowersUI_IsSuggestedNodeFromCostTableInCharacterCreation(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_OP_VALID PowerListNode *pPowerListNode)
{
	PTNodeDef *pNodeDef;
	if (pchCostTable == NULL || pchCostTable[0] == 0 || pEntity == NULL || pPowerListNode == NULL || (pNodeDef = GET_REF(pPowerListNode->hNodeDef)) == NULL)
		return false;
	return CharacterPath_IsSuggestedNodeFromCostTableInCharacterCreation(pEntity, pchCostTable, pNodeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_IsSuggestedNodeFromCostTable");
bool PowersUI_IsSuggestedNodeFromCostTable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchCostTable, SA_PARAM_OP_VALID PowerListNode *pPowerListNode, bool bInCharacterCreationMode)
{
	if (bInCharacterCreationMode)
		return PowersUI_IsSuggestedNodeFromCostTableInCharacterCreation(pContext, pEntity, pchCostTable, pPowerListNode);
	else
		return PowersUI_IsNextSuggestedNodeFromCostTable(pContext, pEntity, pchCostTable, pPowerListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_CostTableOfFirstRankFromNodeDef");
SA_RET_OP_VALID const char * PowersUI_CostTableOfFirstRankFromNodeDef(ExprContext *pContext, SA_PARAM_OP_VALID PowerListNode *pPowerListNode)
{
	PTNodeDef *pNodeDef;
	if (pPowerListNode == NULL || (pNodeDef = GET_REF(pPowerListNode->hNodeDef)) == NULL)
		return NULL;
	else
		return powertree_CostTableOfFirstRankFromNodeDef(pNodeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetSuggestNodeNamesFromCostTableInCharacterCreation");
SA_RET_NN_STR const char * PowersUI_GetSuggestNodeNamesFromCostTableInCharacterCreation(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_NN_STR const char *pchCharacterPathName, SA_PARAM_NN_STR const char *pchCostTable)
{
	static char *estrSuggestedNodeNames = NULL;

	if (pEntity == NULL || pchCharacterPathName == NULL || pchCharacterPathName[0] == '\0')
		return "";

	// Clear the string
	estrClear(&estrSuggestedNodeNames);

	// Update the estring
	CharacterPath_GetSuggestNodeNamesFromCostTableInCharacterCreation(pEntity, pchCharacterPathName, pchCostTable, &estrSuggestedNodeNames);

	if (estrLength(&estrSuggestedNodeNames) == 0)
	{
		return "";
	}
	else
	{
		return exprContextAllocString(pContext, estrSuggestedNodeNames);
	}
}

// Indicates if there is any suggested power nodes from a cost table
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_HasSuggestNodeFromCostTable");
bool PowersUI_HasSuggestNodeFromCostTable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchCostTable)
{
	bool bResult;

	if (pEnt == NULL || pchCostTable == NULL || pchCostTable[0] == '\0')
		return false;

	bResult = CharacterPath_GetNextSuggestedNodeFromCostTable(PARTITION_CLIENT, pEnt, pchCostTable, false) != NULL;
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_HasMaxNodesInGroup");
bool PowersUI_HasMaxNodesInGroup(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, const char* pchGroupDef)
{
	PTGroupDef *pGroupDef = powertreegroupdef_Find(pchGroupDef);
	if (pEnt && pEnt->pChar && pGroupDef && pGroupDef->iMax > 0)
	{
		PowerTree *pTree = NULL;
		PowerTreeDef *pTreeDef = powertree_TreeDefFromGroupDef(pGroupDef);
		if (pTreeDef)
		{
			S32 i;
			for (i = eaSize(&pEnt->pChar->ppPowerTrees)-1; i >= 0; i--)
			{
				if (pTreeDef == GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef))
				{
					pTree = pEnt->pChar->ppPowerTrees[i];
					break;
				}
			}
		}
		if (pTree)
		{
			S32 iCount = powertree_CountOwnedInGroupHelper(CONTAINER_NOCONST(PowerTree, pTree), pGroupDef);
			if (iCount >= pGroupDef->iMax)
			{
				return true;
			}
		}
	}
	return false;
}

// Adds all recommended powers to cart based on the character path and buys them on the fake entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_AutoAddToCartRecommendedPowersByCostTable");
void PowersUI_AutoAddToCartRecommendedPowersByCostTable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, SA_PARAM_NN_STR const char *pchCostTable)
{
	PTNodeDef *pNextSuggestedNodeDef = NULL;
	PowerListNode* pListNode = NULL;

	if (pEnt == NULL || pFakeEnt == NULL || pchCostTable == NULL || pchCostTable[0] == '\0')
		return;

	// Buy all powers from this cost table until there is nothing left to purchase
	while(pNextSuggestedNodeDef = CharacterPath_GetNextSuggestedNodeFromCostTable(PARTITION_CLIENT, pFakeEnt, pchCostTable, false))
	{
		// Create a temporary power list node
		pListNode = StructCreate(parse_PowerListNode);

		// Fill the power list node
		FillPowerListNode(pFakeEnt, pListNode, NULL, powertree_TreeDefFromNodeDef(pNextSuggestedNodeDef),
			NULL, powertree_GroupDefFromNodeDef(pNextSuggestedNodeDef),
			NULL, pNextSuggestedNodeDef);

		// Buy one rank at a time
		if (gclGenExprPowerCartModifyPowerTreeNodeRank(NULL, pEnt, pFakeEnt, pListNode, 1) <= 0)
		{
			// Clean up
			StructDestroy(parse_PowerListNode, pListNode);

			// This should not usually happen
			break;
		}

		// Clean up
		StructDestroy(parse_PowerListNode, pListNode);
	}
}

// Adds all recommended powers to cart based on the character path and buys them on the fake entity
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_BuyPowerListNodeOnFakeEnt");
void PowersUI_BuyPowerListNodeOnFakeEnt(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, SA_PARAM_OP_VALID PowerListNode* pListNode)
{
	if (pEnt && pFakeEnt && pListNode)
	{
		gclGenExprPowerCartModifyPowerTreeNodeRank(NULL, pEnt, pFakeEnt, pListNode, 1);
	}
}

// Adds all recommended powers to cart based on the character path
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_AutoBuyNonchoicesByCostTable");
void PowersUI_AutoBuyNonchoicesByCostTable(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, SA_PARAM_NN_STR const char *pchCostTable)
{
	CharacterPathChoice *pNextChoice = NULL;
	PowerListNode* pListNode = NULL;
	CharacterPath** eaPaths = NULL;
	int i;

	if (pEnt == NULL || pEnt->pChar == NULL || pFakeEnt == NULL || pchCostTable == NULL || pchCostTable[0] == '\0')
		return;

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &eaPaths);

	for(i = 0; i < eaSize(&eaPaths); i++)
	{
		CharacterPath* pPath = eaPaths[i];
		// Buy all powers from this cost table until there is nothing left to purchase
		while(pNextChoice = CharacterPath_GetNextChoiceFromCostTable(pFakeEnt, pPath, pchCostTable, false))
		{
			if (eaSize(&pNextChoice->eaSuggestedNodes) == 1)
			{
				PTNodeDef *pNextSuggestedNodeDef = GET_REF(pNextChoice->eaSuggestedNodes[0]->hNodeDef);

				// Create a temporary power list node
				pListNode = StructCreate(parse_PowerListNode);

				// Fill the power list node
				FillPowerListNode(pFakeEnt, pListNode, NULL, powertree_TreeDefFromNodeDef(pNextSuggestedNodeDef),
					NULL, powertree_GroupDefFromNodeDef(pNextSuggestedNodeDef),
					NULL, pNextSuggestedNodeDef);

				// Buy one rank at a time
				if (gclGenExprPowerCartModifyPowerTreeNodeRank(NULL, pEnt, pFakeEnt, pListNode, 1) <= 0)
				{
					// Clean up
					StructDestroy(parse_PowerListNode, pListNode);

					// This should not usually happen
					break;
				}

				// Clean up
				StructDestroy(parse_PowerListNode, pListNode);
			}
			else
			{
				break;
			}
		}
	}
}

S32 PowersUI_CategoryComparitor(const PowersUICategoryNode** a, const PowersUICategoryNode** b)
{
	if (a && *a && b && *b)
	{
		if ((*a)->eCategory < (*b)->eCategory)
		{
			return -1;
		}
		else if ((*a)->eCategory > (*b)->eCategory)
		{
			return 1;
		}
		else
		{
			return stricmp((*a)->pchName, (*b)->pchName);
		}
	}
	return 0;
}

S32 PowersUI_PurposeComparitor(const PowersUIPurposeNode** a, const PowersUIPurposeNode** b)
{
	if (a && *a && b && *b)
	{
		if ((*a)->ePurpose < (*b)->ePurpose)
		{
			return -1;
		}
		else if ((*a)->ePurpose > (*b)->ePurpose)
		{
			return 1;
		}
		else
		{
			return stricmp((*a)->pchName, (*b)->pchName);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntRequiresFullRespec");
bool PowersUI_EntRequiresFullRespec(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt && pEnt->pChar)
	{
		U32 uVersion;
		U32 uFullRespecVersion;
		character_GetPowerTreeVersion(pEnt->pChar, &uVersion, &uFullRespecVersion);
		if (!uVersion && uFullRespecVersion)
		{
			return true;
		}
	}
	return false;
}

// Check to see if we have any steps that we can respec for free
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("HasFreeRespecSteps");
bool PowersUI_HasFreeRespecSteps(SA_PARAM_OP_VALID Entity* pEnt, int eRespecType)
{
	if(pEnt->pChar && PowerTree_CanRespecGroupWithNumeric(eRespecType))
	{
		S32 i, s;
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);

		// Get everything
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pEnt->pChar), pSteps, false, eRespecType);
		character_PowerTreeStepsCostRespec(entGetPartitionIdx(pEnt), pEnt->pChar, pSteps, 0);
		s = eaSize(&pSteps->ppSteps);

		for (i = 0; i < s; i++)
		{
			if ((pSteps->ppSteps[i]->pchNode || (pSteps->ppSteps[i]->pchTree && !pSteps->ppSteps[i]->bStepIsLocked)) && pSteps->ppSteps[i]->iCostRespec == 0)
			{
				StructDestroy(parse_PowerTreeSteps, pSteps);
				return true;
			}
		}

		StructDestroy(parse_PowerTreeSteps, pSteps);
	}

	return false;
}

S32 PowerGetStackCount(Entity* e, SA_PARAM_OP_VALID Power* pPow, S32* piLastStackCount, U32* puiNextUpdate, GameAccountDataExtract *pExtract)
{
	if (pPow && pPow->eSource == kPowerSource_Item)
	{
		if ((*puiNextUpdate) <= g_ui_State.totalTimeInMs)
		{
			InvBagIDs iBagID;
			int iBagSlot, iItemPowerIdx;
			Item* pItem;
			if (item_FindPowerByID(e,pPow->uiID,&iBagID,&iBagSlot,&pItem,&iItemPowerIdx))
			{
				ItemPowerDef* pItemPowerDef = item_GetItemPowerDef(pItem, iItemPowerIdx);
				S32 iCount = inv_ent_GetSlotItemCount(e,iBagID,iBagSlot, pExtract);
				if (pItemPowerDef && (pItemPowerDef->flags & kItemPowerFlag_Rechargeable)==0)
				{
					InventoryBag* pBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, e), iBagID, pExtract);
					const InvBagDef* pBagDef = invbag_def(pBag);
					if (pBagDef && pBagDef->bUseItemsInInventoryFirst)
					{
						InventoryBag* pInvBag = (InventoryBag*)inv_GetBag(CONTAINER_NOCONST(Entity, e),InvBagIDs_Inventory, pExtract);
						ItemDef* pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;
						if (pInvBag && pItemDef)
						{
							iCount += inv_bag_CountItems(pInvBag, pItemDef->pchName);
						}
					}
				}
				(*puiNextUpdate) = g_ui_State.totalTimeInMs + 100;
				(*piLastStackCount) = iCount;
				return iCount;
			}
		}
		else
		{
			return (*piLastStackCount);
		}
	}
	return -1;
}


AUTO_RUN;
void SetupPowersUIConstants(void)
{
	ui_GenInitStaticDefineVars(TalentsUITextureBitsEnum, "TTConnector_");
	ui_GenInitStaticDefineVars(PurposeListPowerSortingMethodEnum, "PurposeListPowerSortingMethod_");
}

AUTO_STARTUP(PowersUI,1) ASTRT_DEPS(Powers);
void SetupPowersUIConstantsAfterPowers(void)
{
	ui_GenInitStaticDefineVars(PowerPurposeEnum, "PowerPurpose_");
}

AUTO_EXPR_FUNC(UIGen);
void CharacterCreation_GetPowerListFromClassPath(UIGen* pGen)
{
	PowerListNode ***peaPowerList =  ui_GenGetManagedListSafe(pGen, PowerListNode);
	CharacterPath* pPath = GET_REF(g_pFakePlayer->pChar->hPath);
	int i, j, iNodes = 0;


	if (pPath)
	{
		for (i = 0; i < eaSize(&pPath->eaSuggestedPurchases); i++)
		{
			int iPoints = powertable_Lookup(pPath->eaSuggestedPurchases[i]->pchPowerTable, 0);
			for (j = 0; j < eaSize(&pPath->eaSuggestedPurchases[i]->eaChoices) && j < iPoints; j++)
			{
				PowerListNode *pListNode = eaGetStruct(peaPowerList, parse_PowerListNode, iNodes++);
				PTNodeDef* pNodeDef = GET_REF(pPath->eaSuggestedPurchases[i]->eaChoices[j]->eaSuggestedNodes[0]->hNodeDef);

				FillPowerListNode(NULL, pListNode, NULL, NULL, NULL, NULL, NULL, pNodeDef);
			}
		}
	}

	while (eaSize(peaPowerList) > iNodes)
		StructDestroy(parse_PowerListNode, eaPop(peaPowerList));

	ui_GenSetListSafe(pGen, peaPowerList, PowerListNode);
}

//only returns auto-buy powers for now
AUTO_EXPR_FUNC(UIGen);
void CharacterCreation_GetPowerListFromSpecies(UIGen* pGen)
{
	PowerListNode ***peaPowerList = ui_GenGetManagedListSafe(pGen, PowerListNode);
	int i, j, iNodes = 0;
	SpeciesDef* pSpecies = NULL;

    if ( g_pFakePlayer )
    {
        pSpecies = GET_REF(g_pFakePlayer->pChar->hSpecies);
    }

	if (pSpecies && pSpecies->pchUIPowerTree)
	{
		PowerTreeDef* pDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pSpecies->pchUIPowerTree);
		if (pDef)
		{
			for (i = 0; i < eaSize(&pDef->ppGroups); i++)
			{
				for (j = 0; j < eaSize(&pDef->ppGroups[i]->ppNodes); j++)
				{
					PTNodeDef* pNodeDef = pDef->ppGroups[i]->ppNodes[j];

					if (pNodeDef->eFlag & kNodeFlag_AutoBuy)
					{
						PowerListNode *pListNode = eaGetStruct(peaPowerList, parse_PowerListNode, iNodes++);
						FillPowerListNode(NULL, pListNode, NULL, NULL, NULL, NULL, NULL, pNodeDef);
					}
				}
			}
		}
	}

	while (eaSize(peaPowerList) > iNodes)
		StructDestroy(parse_PowerListNode, eaPop(peaPowerList));

	ui_GenSetListSafe(pGen, peaPowerList, PowerListNode);
}

// Returns the names of the power trees the entity owns matching the given UI category.
// The string is space separated to work with the powers UI.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowersUI_GetOwnedPowerTreeNamesByUICategory");
const char * PowersUI_GetOwnedPowerTreeNamesByUICategory(SA_PARAM_OP_VALID Entity *pEnt, S32 ePowerTreeUICategory, S32 iMaxRecordsToReturn)
{
	if (pEnt && pEnt->pChar)
	{
		S32 iCount = 0;
		static char *pchCategoryNames = NULL;
		estrClear(&pchCategoryNames);

		if (iMaxRecordsToReturn == 0)
		{
			iMaxRecordsToReturn = INT_MAX;
		}

		FOR_EACH_IN_CONST_EARRAY_FORWARDS(pEnt->pChar->ppPowerTrees, PowerTree, pTree)
		{
			PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);

			if (pTreeDef && pTreeDef->eUICategory == ePowerTreeUICategory)
			{
				if (iCount > 0)
				{
					estrAppend2(&pchCategoryNames, " ");
				}
				estrAppend2(&pchCategoryNames, pTreeDef->pchName);
				++iCount;
			}

			if (iCount >= iMaxRecordsToReturn)
			{
				break;
			}
		}
		FOR_EACH_END

		return pchCategoryNames;
	}
	return "";
}

// Returns the number of power trees that the entity owns marked with the SendToClient flag
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowersUI_GetNumClientSentPowerTrees);
S32 PowersUI_GetNumClientSentPowerTrees(SA_PARAM_OP_VALID Entity *pEnt)
{
	return pEnt && pEnt->pChar && pEnt->pChar->pClientPowerTreeInfo ? eaSize(&pEnt->pChar->pClientPowerTreeInfo->ppTrees) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowersUI_GetClientSentPowerTreeLogicalNameByIndex);
const char * PowersUI_GetClientSentPowerTreeLogicalNameByIndex(SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	S32 iNumTrees = PowersUI_GetNumClientSentPowerTrees(pEnt);

	if (iIndex >= 0 && iIndex < iNumTrees)
	{
		return pEnt->pChar->pClientPowerTreeInfo->ppTrees[iIndex]->pchName;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowersUI_GetClientSentPowerTreeDisplayMessageByIndex);
const char * PowersUI_GetClientSentPowerTreeDisplayMessageByIndex(SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	S32 iNumTrees = PowersUI_GetNumClientSentPowerTrees(pEnt);

	if (iIndex >= 0 && iIndex < iNumTrees)
	{
		return TranslateMessageRef(pEnt->pChar->pClientPowerTreeInfo->ppTrees[iIndex]->hDisplayMessage);
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen);
const char * PowersUI_GetPowerTreeDisplayName(const char *pchPowerTreeName)
{
	PowerTreeDef *pDef = powertreedef_Find(pchPowerTreeName);
	if (pDef)
	{
		return TranslateDisplayMessage(pDef->pDisplayMessage);
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen);
const char * PowersUI_GetPowerTreeDescription(const char *pchPowerTreeName)
{
	PowerTreeDef *pDef = powertreedef_Find(pchPowerTreeName);
	if (pDef)
	{
		return TranslateDisplayMessage(pDef->pDescriptionMessage);
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen);
const char * PowersUI_GetPowerTreeIcon(const char *pchPowerTreeName)
{
	PowerTreeDef *pDef = powertreedef_Find(pchPowerTreeName);
	if (pDef)
	{
		return pDef->pchIconName;
	}

	return "";
}

void CharacterCreation_RemovePowers(const char *pchTreeName);
void CharacterCreation_BuyPowerTreeNode(const char *pchTree, const char *pchNodeFull);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_PickRandomPowerNodeByPurpose);
void exprCharacterCreation_PickRandomPowerNodeByPurpose(ExprContext* pContext, SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_NN_STR const char * pchTreeName, SA_PARAM_NN_STR const char * pchPurposeName)
{
	int iOriginalValue = -1;
	int iIndex;
	int iTriesRemaining;
	int n;
	PTNodeDef *pOriginalPTNode = NULL;

	PowersUIPurposeNode *pPurposeNode = PowersUI_GetPowersUIPurposeNodeByName(pContext, pGen, pchPurposeName); // All possible powers
	PowerTreeDef *pPurposeTreeDef = NULL;
	if( !pPurposeNode || !g_pFakePlayer || !g_pFakePlayer->pChar )
		return;

	if( pPurposeNode->eaPowerListNodes && (eaSize(&pPurposeNode->eaPowerListNodes) > 0) && pPurposeNode->eaPowerListNodes[0] )
		pPurposeTreeDef = GET_REF(pPurposeNode->eaPowerListNodes[0]->hTreeDef); // The one the player owns

	// Figure out the initial value.
	// Loop through all the owned trees to find the one that owns pPurposeNode
	for(n = eaSize(&g_pFakePlayer->pChar->ppPowerTrees)-1; n >= 0; n--)
	{
		NOCONST(PowerTree) *pTree = g_pFakePlayer->pChar->ppPowerTrees[n];
		if(GET_REF(pTree->hDef) == pPurposeTreeDef)
		{
			// Find the node ref of the currently-owned node
			pOriginalPTNode = (eaSize(&pTree->ppNodes) > 0) ? GET_REF(pTree->ppNodes[0]->hDef) : NULL;
			break;
		}
	}

	for(n = eaSize(&pPurposeNode->eaPowerListNodes)-1; n >= 0; n--)
	{
		PowerListNode *pNode = pPurposeNode->eaPowerListNodes[n];
		if( GET_REF(pNode->hNodeDef) == pOriginalPTNode )
		{
			iOriginalValue = n;
			break;
		}
	}

	// Clear the old tree node
	CharacterCreation_RemovePowers(pchTreeName);

	// Pick a new random power node from the tree. If it matches the old value, try again.
	iIndex = iOriginalValue;
	for(iTriesRemaining = 20; iTriesRemaining > 0 && iIndex == iOriginalValue; --iTriesRemaining )
	{
		iIndex = randomIntRange(0, eaSize(&pPurposeNode->eaPowerListNodes)-1);
	}

	// Buy it
	{
		PowerListNode* pPowerListNode = pPurposeNode->eaPowerListNodes[iIndex];
		PTNodeDef* pPTNodeDef = GET_REF(pPowerListNode->hNodeDef);
		PowerTreeDef* pPowerTree = GET_REF(pPowerListNode->hTreeDef);
		CharacterCreation_BuyPowerTreeNode(pPowerTree->pchName, pPTNodeDef->pchNameFull);
	}
}

#include "Autogen/PowersUI_h_ast.c"
