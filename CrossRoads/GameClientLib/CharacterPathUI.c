/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "UIGen.h"
#include "Entity.h"
#include "gclEntity.h"
#include "Character.h"
#include "CharacterClass.h"
#include "GameAccountDataCommon.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerGrid.h"
#include "PowerVars.h"
#include "gclCostumeUI.h"
#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/PowerGrid_h_ast.h"
#include "AutoGen/CharacterClass_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

#define INCLUDE_UNOWNED_ADVANTAGES 1
#define INCLUDE_UNOWNED_POWERS 2
#define INCLUDE_ALL_POWERS 4
#define INCLUDE_ALL_POWERS_WITH_ADVANTAGES 8

static PowerTreeDefRef **s_eaCharacterPathTrees = NULL;
static const char *s_pchCharacterPathName = NULL;

static int CharacterPath_PowerListNodeComparitor(const PowerListNode** ppNodeA, const PowerListNode** ppNodeB, const void* unused)
{
	if (!ppNodeA || !*ppNodeA || !ppNodeB || !*ppNodeB)
		return 0;

	if ((*ppNodeA)->iLevel > (*ppNodeB)->iLevel)
		return 1;
	else if ((*ppNodeA)->iLevel < (*ppNodeB)->iLevel)
		return -1;
	else
		return 0;
}

static int CharacterPath_PowerCartListNodeComparitor(const PowerCartListNode** ppNodeA, const PowerCartListNode** ppNodeB, const void* unused)
{
	S32 iLevelA = ppNodeA && *ppNodeA ? (*ppNodeA)->pPowerListNode->iLevel : -1;
	S32 iLevelB = ppNodeB && *ppNodeB ? (*ppNodeB)->pPowerListNode->iLevel : -1;

	if (iLevelA > 0 && iLevelB <= 0)
		return -1;
	else if (iLevelA <= 0 && iLevelB > 0)
		return 1;
	else if (iLevelA <= 0 && iLevelB <= 0)
		return 0;

	if (iLevelA > iLevelB)
		return 1;
	else if (iLevelA < iLevelB)
		return -1;
	else
		return 0;
}

// Get the complete power list from the CharacterPath
void CharacterPath_ChoiceListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CharacterPath** eaPaths, bool bHideTreeChoices)
{
	CharacterPathChoice ***peaChoices = ui_GenGetManagedListSafe(pGen, CharacterPathChoice);
	int iPath;
	eaClearFast(peaChoices);

	for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
	{
		int iPurchIndex;
		for (iPurchIndex = 0; iPurchIndex < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); iPurchIndex++)
		{
			CharacterPathSuggestedPurchase *pPurchase = eaPaths[iPath]->eaSuggestedPurchases[iPurchIndex];
			int iSugIndex;
			for (iSugIndex = 0; iSugIndex < eaSize(&pPurchase->eaChoices); iSugIndex++)
			{
				if(bHideTreeChoices && eaSize(&pPurchase->eaChoices[iSugIndex]->eaSuggestedNodes) == 0)
					continue;

				eaPush(peaChoices, pPurchase->eaChoices[iSugIndex]);
			}
		}
	}
	ui_GenSetListSafe(pGen, peaChoices, CharacterPathChoice);
}

// Get the complete power list from the entity by using character path
AUTO_EXPR_FUNC(UIGen);
void CharacterPath_ChoiceList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchPathName, int bHideTreeChoices)
{
	CharacterPath** eaPaths = NULL;
	eaStackCreate(&eaPaths, 1);
	eaPaths[0] = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);
	
	CharacterPath_ChoiceListEx(pGen, NULL, eaPaths, bHideTreeChoices);
	return;
}

// Get the complete power list from the entity by using character path
AUTO_EXPR_FUNC(UIGen);
void CharacterPath_PlayerChoiceList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, int bHideTreeChoices)
{
	CharacterPath** eaPaths = NULL;
	if (pEnt && pEnt->pChar)
	{
		eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
		entity_GetChosenCharacterPaths(pEnt, &eaPaths);
	}
	CharacterPath_ChoiceListEx(pGen, pEnt, eaPaths, bHideTreeChoices);
}

// Get the complete power list from the CharacterPath
void CharacterPath_PowerListEx(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CharacterPath** eaPaths)
{
	PowerListNode ***peaListNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	int iCount = 0;
	int iPath;

	for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
	{
		int iPurchIndex;
		for (iPurchIndex = 0; iPurchIndex < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); iPurchIndex++)
		{
			int iSugIndex;
			int iPowerLevelIndex = 0;
			int iPoints = 0;
			CharacterPathSuggestedPurchase *pPurchase = eaPaths[iPath]->eaSuggestedPurchases[iPurchIndex];

			for (iSugIndex = 0; iSugIndex < eaSize(&pPurchase->eaChoices); iSugIndex++)
			{
				int iChoice;
				CharacterPathChoice *pChoice = pPurchase->eaChoices[iSugIndex];
				for (iChoice = 0; iChoice < eaSize(&pChoice->eaSuggestedNodes); iChoice++)
				{
					CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[iChoice];
					PowerListNode *pListNode = eaGetStruct(peaListNodes, parse_PowerListNode, iCount++);
					PTNodeDef *pNodeDef = GET_REF(pSuggestedNode->hNodeDef);
					PTGroupDef *pGroupDef = powertree_GroupDefFromNodeDef(pNodeDef);
					PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
					FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
				}
			}
		}
	}
	eaSetSizeStruct(peaListNodes, parse_PowerListNode, iCount);
	eaStableSort(*peaListNodes, NULL, CharacterPath_PowerListNodeComparitor);
	ui_GenSetListSafe(pGen, peaListNodes, PowerListNode);
}

// Get the complete power list from the entity by using character path
AUTO_EXPR_FUNC(UIGen);
void CharacterPath_CharacterCreationPowerList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchPathName)
{
	CharacterPath *pPath = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);
	PowerListNode ***peaListNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	int iCount = 0;
	//eaClearFast(peaListNodes);
	if (pPath)
	{
		int iPurchIndex;
		for (iPurchIndex = 0; iPurchIndex < eaSize(&pPath->eaSuggestedPurchases); iPurchIndex++)
		{
			int iSugIndex;
			int iPowerLevelIndex = 0;
			int iPoints = 0;
			CharacterPathSuggestedPurchase *pPurchase = pPath->eaSuggestedPurchases[iPurchIndex];

			for (iSugIndex = 0; iSugIndex < eaSize(&pPurchase->eaChoices); iSugIndex++)
			{
				CharacterPathSuggestedNode *pSuggestedNode = pPurchase->eaChoices[iSugIndex]->eaSuggestedNodes[0];
				PTNodeDef *pNodeDef = GET_REF(pSuggestedNode->hNodeDef);
				PTGroupDef *pGroupDef = powertree_GroupDefFromNodeDef(pNodeDef);
				if (pGroupDef && stricmp(pGroupDef->pchGroup, "Auto") == 0)
				{
					PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
					PowerListNode *pListNode = eaGetStruct(peaListNodes, parse_PowerListNode, iCount++);
					FillPowerListNode(NULL, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
				}
			}
		}
	}
	eaStableSort(*peaListNodes, NULL, CharacterPath_PowerListNodeComparitor);
	ui_GenSetListSafe(pGen, peaListNodes, PowerListNode);
}

// Get the complete power list from the entity by using character path
AUTO_EXPR_FUNC(UIGen);
void CharacterPath_PowerList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchPathName)
{
	CharacterPath** eaPaths = NULL;
	eaStackCreate(&eaPaths, 1);
	eaPaths[0] = RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);

	CharacterPath_PowerListEx(pGen, NULL, eaPaths);
}

// Get the complete power list from the entity by using character path
AUTO_EXPR_FUNC(UIGen);
void CharacterPath_PlayerPowerList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	CharacterPath** eaPaths = NULL;
	if (pEnt && pEnt->pChar)
	{
		eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
		entity_GetChosenCharacterPaths(pEnt, &eaPaths);
	}
	CharacterPath_PowerListEx(pGen, pEnt, eaPaths);
}

// Get the primary character path for the entity. This may be used to determine
// if the entity is on a character path. i.e. if (EntGetCharacterPath(Player))
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCharacterPath", "EntGetPrimaryCharacterPath");
SA_RET_OP_VALID CharacterPath *CharacterPath_GetCharacterPath(SA_PARAM_OP_VALID Entity *pEntity)
{
	return entity_GetPrimaryCharacterPath(pEntity);
}

// Get the name of the entity's primary character path
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCharacterPathName", "EntGetPrimaryCharacterPathName");
const char *CharacterPath_GetCharacterPathName(SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterPath *pPath = entity_GetPrimaryCharacterPath(pEntity);
	return pPath ? TranslateDisplayMessage(pPath->pDisplayName) : "";
}

// Get the logical name of the entity's primary character path
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetPrimaryCharacterPathLogicalName");
const char *CharacterPath_GetCharacterPathLogicalName(SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterPath *pPath = entity_GetPrimaryCharacterPath(pEntity);
	return pPath ? pPath->pchName : "";
}

// Returns the display name of a character path given the path's logical name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetCharacterPathDisplayNameByLogicalName);
const char *CharacterPath_GetCharacterPathDisplayNameByLogicalName(SA_PARAM_OP_STR const char *pchPathName)
{
	CharacterPath *pPath = (CharacterPath *)RefSystem_ReferentFromString(g_hCharacterPathDict, pchPathName);
	return pPath ? TranslateDisplayMessage(pPath->pDisplayName) : "";
}

// Get the description of the entity's primary character path
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetCharacterDescription", "EntGetPrimaryCharacterPathDescription");
const char *CharacterPath_GetCharacterPathDescription(SA_PARAM_OP_VALID Entity *pEntity)
{
	CharacterPath *pPath = entity_GetPrimaryCharacterPath(pEntity);
	return pPath ? TranslateDisplayMessage(pPath->pDescription) : "";
}

// Get the minimum level required before this character path becomes a potentially valid option.
// It does not ensure that the power is actually buyable, just the point at which the player has
// enough points to make the purchase if the player is following the path.
// It will return -1 if it's unable to determine a level.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CharacterPath_GetMinimumSuggestionLevelEx);
S32 CharacterPath_GetMinimumSuggestionLevelEx(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CharacterPath *pPath, SA_PARAM_OP_VALID PTNodeDef *pDef, S32 iRank)
{
	if (pEnt && pPath && pDef && 0 < iRank && iRank <= eaSize(&pDef->ppRanks))
	{
		CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
		PTNodeRankDef *pRank = pDef->ppRanks[iRank - 1];
		CharacterPathSuggestedPurchase *pPurchase = NULL;
		const char *pchPowerTable;
		S32 i, j, iDepth, iLevel, iCost, iRanksToBuy;
		S32 iChoice;
		PowerTable *pPowerTable;
		for (i = 0; i < eaSize(&pPath->eaSuggestedPurchases); i++)
		{
			pchPowerTable = pPath->eaSuggestedPurchases[i]->pchPowerTable;
			if (pchPowerTable && ((pRank->pchCostTable == pchPowerTable) || (pRank->pchCostTable && stricmp(pRank->pchCostTable, pchPowerTable) == 0)))
			{
				pPurchase = pPath->eaSuggestedPurchases[i];
				break;
			}
		}
		if (pPurchase && eaSize(&pPurchase->eaChoices) > 0)
		{
			// Find out where the def is
			iDepth = -1;
			iChoice = -1;
			for (i = 0; i < eaSize(&pPurchase->eaChoices); i++)
			{
				CharacterPathChoice *pChoice = pPurchase->eaChoices[i];
				for(j=0; j< eaSize(&pChoice->eaSuggestedNodes); j++)
				{
					CharacterPathSuggestedNode *pSuggest = pChoice->eaSuggestedNodes[j];
					PTNodeDef *pNode = pSuggest ? GET_REF(pSuggest->hNodeDef) : NULL;
					if (pNode == pDef && (!pSuggest->iMaxRanksToBuy || iRank <= pSuggest->iMaxRanksToBuy))
					{
						iDepth = i;
						iChoice = j;
						break;
					}
				}
			}
			if (iDepth >= 0)
			{
				// Start counting how many points are required
				iCost = 0;
				for (i = iDepth; i >= 0; i--)
				{
					CharacterPathChoice *pChoice = pPurchase->eaChoices[i];
					CharacterPathSuggestedNode *pSuggest = pPurchase->eaChoices[i]->eaSuggestedNodes[0];
					PTNodeDef *pNode = GET_REF(pSuggest->hNodeDef);

					// Check for a matching def later on
					for (j = i + 1; j < iDepth; j++)
					{
						int iIdx;
						for(iIdx = 0; iIdx < eaSize(&pPurchase->eaChoices[j]->eaSuggestedNodes); iIdx++)
						{
							if (pNode == GET_REF(pPurchase->eaChoices[j]->eaSuggestedNodes[iIdx]->hNodeDef))
								break;
						}
					}

					// If it found a matching def, then this node should have already been
					// counted. Unless the data is setup poorly.
					if (j < iDepth)
						continue;

					// Add all the suggested ranks
					if (pNode)
					{
						iRanksToBuy = pNode == pDef ? iRank : pSuggest->iMaxRanksToBuy ? pSuggest->iMaxRanksToBuy : eaSize(&pNode->ppRanks);
						for (j = 0; j < iRanksToBuy; j++)
						{
							// Add the cost of the rank
							if (stricmp(pNode->ppRanks[j]->pchCostTable, pRank->pchCostTable) == 0)
								iCost += entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pNode, j);
						}
					}
				}

				iLevel = -1;
				if (iCost != 0)
				{
					pPowerTable = powertable_FindInClass(pPurchase->pchPowerTable, pClass);
					if (pPowerTable)
					{
						// Scan the power table for the specified cost
						for (iLevel = 0; iLevel < eafSize(&pPowerTable->pfValues); iLevel++)
						{
							if (pPowerTable->pfValues[iLevel] == iCost)
								break;
						}
					}
				}

				return iLevel >= 0 ? iLevel + 1 : iLevel;
			}
		}
	}
	return -1;
}

// Like CharacterPath_GetMinimumSuggestionLevelEx, except it takes a name to a NodeDef instead of a pointer.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(CharacterPath_GetMinimumSuggestionLevelNameEx);
S32 CharacterPath_GetMinimumSuggestionLevelNameEx(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CharacterPath *pPath, const char *pchDef, S32 iRank)
{
	PTNodeDef *pPTNodeDef = RefSystem_ReferentFromString(g_hPowerTreeNodeDefDict, pchDef);
	return CharacterPath_GetMinimumSuggestionLevelEx(pEnt, pPath, pPTNodeDef, iRank);
}

// Get the powers in the CharacterPath and all the Advantages for the powers.
// Two flags allow customization of what unowned ranks and advantages will be included.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPathAndAdvantages);
void CharacterPath_GetPathAndAdvantages(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, const char *pchCostTable, S32 iFlags)
{
	static PTNodeDef **s_eaNodeDefs = NULL;
	static PTNode **s_eaNodes = NULL;
	PowerCartListNode ***peaCartNodes = ui_GenGetManagedListSafe(pGen, PowerCartListNode);
	CharacterClass *pClass = pEntity && pEntity->pChar ? GET_REF(pEntity->pChar->hClass) : NULL;
	S32 iLength = 0;
	S32 i, j;
	CharacterPath** eaPaths = NULL;
	int iPath = 0;

	eaClearFast(&s_eaNodeDefs);
	eaClearFast(&s_eaNodes);

	if (pEntity && pEntity->pChar && entity_HasAnyCharacterPath(pEntity))
	{
		eaStackCreate(&eaPaths, eaSize(&pEntity->pChar->ppSecondaryPaths) + 1);
		entity_GetChosenCharacterPaths(pEntity, &eaPaths);

		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++)
			{
				CharacterPathSuggestedPurchase *pSuggestion = eaPaths[iPath]->eaSuggestedPurchases[i];
				for (j = 0; j < eaSize(&pSuggestion->eaChoices); j++)
				{
					CharacterPathChoice *pChoice = pSuggestion->eaChoices[j];
					PTNode *pChosenNode = CharacterPath_GetChosenNode(pEntity, pChoice);
					if(pChosenNode)
					{
						eaPush(&s_eaNodes, pChosenNode);
					}
					else
					{
						CharacterPathSuggestedNode *pSuggestedNode = eaGet(&pChoice->eaSuggestedNodes, 0);
						if (SAFE_GET_REF(pSuggestedNode, hNodeDef))
						{
							PTNodeDef *pNodeDef = GET_REF(pSuggestedNode->hNodeDef);
							// find the PTNode
							PTNode *pNode = powertree_FindNode(pEntity->pChar, NULL, pNodeDef->pchNameFull);
							if (pNode)
							{
								eaPush(&s_eaNodes, pNode);
							}
							else if ((iFlags & INCLUDE_UNOWNED_POWERS) != 0)
							{
								// add the def
								eaPush(&s_eaNodeDefs, pNodeDef);
							}
						}
					}
				}
			}
		}

		if ((iFlags & (INCLUDE_ALL_POWERS | INCLUDE_ALL_POWERS_WITH_ADVANTAGES)))
		{
			for (i = 0; i < eaSize(&pEntity->pChar->ppPowerTrees); i++)
			{
				PowerTree *pPowerTree = pEntity->pChar->ppPowerTrees[i];
				for (j = 0; j < eaSize(&pPowerTree->ppNodes); j++)
				{
					PTNode *pNode = pPowerTree->ppNodes[j];
					eaPushUnique(&s_eaNodes, pNode);
				}
			}
		}
	}

	iLength = gclGetPowerAndAdvantageList(iLength, peaCartNodes, pEntity, s_eaNodes, s_eaNodeDefs, pchCostTable, !(iFlags & INCLUDE_UNOWNED_ADVANTAGES), !!(iFlags & INCLUDE_ALL_POWERS));

	// Fill in power minimum levels
	if (eaSize(&eaPaths) > 0)
	{
		S32 iNode;
		for (iNode = 0; iNode < iLength; iNode++)
		{
			PowerCartListNode *pCartNode = (*peaCartNodes)[iNode];
			S32 iRank = pCartNode->pPowerListNode->iRank;
			PTNodeDef *pNodeDef = GET_REF(pCartNode->pPowerListNode->hNodeDef);
			S32 iSuggestRank = 0;
			CharacterPath* pFoundPath = NULL;
			if (iSuggestRank <= 0)
			{
				for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
				{
					for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases) && iSuggestRank <= 0; i++)
					{
						CharacterPathSuggestedPurchase *pSuggPurchase = eaGet(&eaPaths[iPath]->eaSuggestedPurchases, i);
						for (j = 0; pSuggPurchase && j < eaSize(&pSuggPurchase->eaChoices) && iSuggestRank <= 0; j++)
						{
							CharacterPathChoice *pChoice = eaGet(&pSuggPurchase->eaChoices, j);
							CharacterPathSuggestedNode *pSuggNode = pChoice ? eaGet(&pChoice->eaSuggestedNodes, 0) : NULL;
							if (SAFE_GET_REF(pSuggNode, hNodeDef) == pNodeDef)
							{
								iSuggestRank = pSuggNode->iMaxRanksToBuy;
								if (iSuggestRank == 0)
								{
									iSuggestRank = eaSize(&pNodeDef->ppRanks);
								}
								pFoundPath = eaPaths[iPath];
							}
						}
					}
				}
			}
			pCartNode->pPowerListNode->iLevel = CharacterPath_GetMinimumSuggestionLevelEx(pEntity, pFoundPath, pNodeDef, iSuggestRank);
		}
	}

	eaSetSizeStruct(peaCartNodes, parse_PowerCartListNode, iLength);
	eaStableSort(*peaCartNodes, NULL, CharacterPath_PowerCartListNodeComparitor);
	ui_GenSetManagedListSafe(pGen, peaCartNodes, PowerCartListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PlayerGetPathPowerTrees);
void CharacterPath_GetPathPowerTrees(void)
{
	Entity *pEnt = entActivePlayerPtr();
	S32 i, j, k, iChoice;
	S32 iUsed = 0;
	char achBuffer[1000];

	if (entity_HasAnyCharacterPath(pEnt))
	{
		CharacterPath** eaPaths = NULL;
		int iPath;
		eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
		entity_GetChosenCharacterPaths(pEnt, &eaPaths);

		for (iPath = 0; iPath < eaSize(&eaPaths); iPath++)
		{
			if (eaPaths[iPath]->pchName != s_pchCharacterPathName)
			{
				for (i = 0; i < eaSize(&eaPaths[iPath]->eaSuggestedPurchases); i++)
				{
					CharacterPathSuggestedPurchase *pPurchasePath = eaPaths[iPath]->eaSuggestedPurchases[i];
					for(iChoice = 0; iChoice < eaSize(&pPurchasePath->eaChoices); iChoice++)
					{
						CharacterPathChoice *pChoice = pPurchasePath->eaChoices[iChoice];
						for (j = 0; j < eaSize(&pChoice->eaSuggestedNodes); j++)
						{
							// Somewhat hacky, but there's no other alternative.
							const char *pchName = REF_STRING_FROM_HANDLE(pChoice->eaSuggestedNodes[j]->hNodeDef);
							if (pchName)
							{
								const char *pchDot = strchr(pchName, '.');
								strncpy(achBuffer, pchName, pchDot - pchName);
								for (k = eaSize(&s_eaCharacterPathTrees)-1; k >= 0; k--)
								{
									if (!stricmp(REF_STRING_FROM_HANDLE(s_eaCharacterPathTrees[k]->hRef), achBuffer))
									{
										break;
									}
								}
								if (k < 0)
								{
									PowerTreeDefRef *pRef = eaGetStruct(&s_eaCharacterPathTrees, parse_PowerTreeDefRef, iUsed++);
									SET_HANDLE_FROM_STRING(g_hPowerTreeDefDict, achBuffer, pRef->hRef);
								}
							}
						}
					}
				}
				s_pchCharacterPathName = eaPaths[iPath]->pchName;
			}
			else
			{
				iUsed = eaSize(&s_eaCharacterPathTrees);
			}
		}
	}
	else
	{
		s_pchCharacterPathName = NULL;
	}

	eaSetSizeStruct(&s_eaCharacterPathTrees, parse_PowerTreeDefRef, iUsed);
}

// Gets a list of choices the player will have to make (though most will probably be singles)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPath_GetUnpurchasedChoiceList);
void CharacterPath_GetUnpurchasedChoiceList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	CharacterClass *pClass = pEnt && pEnt->pChar ? GET_REF(pEnt->pChar->hClass) : NULL;
	CharacterPath *pPath = CharacterPath_GetCharacterPath(pEnt);
	CharacterPathChoice ***peaChoices = ui_GenGetManagedListSafe(pGen, CharacterPathChoice);
	int i;
	PowerListNode tempPowerListNode = {0};

	StructInit(parse_PowerListNode, &tempPowerListNode);
	eaClearFast(peaChoices);
	for (i = 0; pPath && i < eaSize(&pPath->eaSuggestedPurchases); i++)
	{
		CharacterPathSuggestedPurchase *pSuggPurchase = pPath->eaSuggestedPurchases[i];
		int iPointsLeft = entity_PointsRemaining(NULL, CONTAINER_NOCONST(Entity, pEnt), NULL, pSuggPurchase->pchPowerTable);
		int j;
		for (j = 0; j < eaSize(&pSuggPurchase->eaChoices); j++)
		{
			CharacterPathChoice *pChoice = pSuggPurchase->eaChoices[j];
			int k;
			bool bShow = true;
			for (k = 0; k < eaSize(&pChoice->eaSuggestedNodes); k++)
			{
				CharacterPathSuggestedNode *pSuggNode = pChoice->eaSuggestedNodes[k];
				PTNodeDef *pNodeDef = GET_REF(pSuggNode->hNodeDef);
				if (iPointsLeft==0)
				{
					bShow = false;
					break;
				}
				StructReset(parse_PowerListNode, &tempPowerListNode);
				FillPowerListNode(pEnt, &tempPowerListNode, NULL, NULL, NULL, NULL, NULL, pNodeDef);
				if (tempPowerListNode.bIsOwned)
				{
					bShow = false;
					break;
				}
			}
			if(eaSize(&pChoice->eaSuggestedNodes) == 0)
				bShow = false;

			if (bShow)
			{
				eaPush(peaChoices, pChoice);
			}
			if (iPointsLeft==0)
			{
				break;
			}
			iPointsLeft--;
		}
	}
	StructDeInit(parse_PowerListNode, &tempPowerListNode);
	ui_GenSetListSafe(pGen, peaChoices, CharacterPathChoice);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPath_GetPowersInChoice);
bool CharacterPath_GetPowersInChoice(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID CharacterPathChoice *pChoice)
{
	PowerListNode ***peaListNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	int i, iCount = 0;
	if (pChoice)
	{
		for (i = 0; i < eaSize(&pChoice->eaSuggestedNodes); i++)
		{
			CharacterPathSuggestedNode *pSuggestedNode = pChoice->eaSuggestedNodes[i];
			PTNodeDef *pNodeDef = GET_REF(pSuggestedNode->hNodeDef);
			PTGroupDef *pGroupDef = powertree_GroupDefFromNodeDef(pNodeDef);
			PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
			PowerListNode *pListNode = eaGetStruct(peaListNodes, parse_PowerListNode, iCount++);
			FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
		}
	}
	eaSetSizeStruct(peaListNodes, parse_PowerListNode, iCount);
	ui_GenSetListSafe(pGen, peaListNodes, PowerListNode);
	return !!iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSetDataPowerListNode);
void GenSetDataPowerListNode(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pchPowerNodeName)
{
	PTNodeDef *pNodeDef = RefSystem_ReferentFromString(g_hPowerTreeNodeDefDict,pchPowerNodeName);
	if (pNodeDef)
	{
		PTGroupDef *pGroupDef = powertree_GroupDefFromNodeDef(pNodeDef);
		PowerTreeDef *pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
		PowerListNode *pListNode = ui_GenGetPointer(pGen, parse_PowerListNode, NULL);
		if (pListNode == NULL)
			pListNode = StructCreate(parse_PowerListNode);

		FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
		ui_GenSetManagedPointer(pGen, pListNode, parse_PowerListNode, true);
	}
	else
	{
		ui_GenClearPointer(pGen);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterPath_NextChoice);
SA_RET_OP_VALID CharacterPathChoice *CharacterPath_NextChoice(SA_PARAM_OP_VALID Entity *pEnt, const char* pchCostTable)
{
	CharacterPath** eaPaths = NULL;
	int i;

	eaStackCreate(&eaPaths, eaSize(&pEnt->pChar->ppSecondaryPaths) + 1);
	entity_GetChosenCharacterPaths(pEnt, &eaPaths);

	for (i = 0; i < eaSize(&eaPaths); i++)
	{
		CharacterPathChoice *pChoice = CharacterPath_GetNextChoiceFromCostTable(pEnt, eaPaths[i], pchCostTable, false);

		if (pChoice && eaSize(&pChoice->eaSuggestedNodes) > 1)
		{
			PTNodeDef *pDef = GET_REF(pChoice->eaSuggestedNodes[0]->hNodeDef);
			int iLevel = CharacterPath_GetMinimumSuggestionLevelEx(pEnt, CharacterPath_GetCharacterPath(pEnt), pDef, 1);
			if (iLevel <= entity_GetSavedExpLevel(pEnt))
			{
				return pChoice;
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetCharacterPath");
SA_RET_OP_VALID CharacterPath* GetCharacterPath(const char* pchCharacterPath)
{
	return RefSystem_ReferentFromString(g_hCharacterPathDict, pchCharacterPath);
}

AUTO_RUN;
void InitCharacterPathUI(void)
{
	ui_GenInitIntVar("IncludeUnownedAdvantages", INCLUDE_UNOWNED_ADVANTAGES);
	ui_GenInitIntVar("IncludeUnownedPowers", INCLUDE_UNOWNED_POWERS);
	ui_GenInitIntVar("IncludePowersWithAdvantages", INCLUDE_ALL_POWERS_WITH_ADVANTAGES);
	ui_GenInitIntVar("IncludeAllPowers", INCLUDE_ALL_POWERS);
}

