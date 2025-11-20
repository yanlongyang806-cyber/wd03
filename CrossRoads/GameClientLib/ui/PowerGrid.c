
#include <string.h>
#include "estring.h"
#include "Expression.h"
#include "Powers.h"
#include "StringFormat.h"
#include "StringCache.h"
#include "StringUtil.h"

#include "BlockEarray.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntitySavedData.h"
#include "gclEntity.h"
#include "gclUtils.h"
#include "Character.h"
#include "CharacterClass.h"
#include "CharacterCreationUI.h"
#include "CombatEnums.h"
#include "CombatEval.h"
#include "GameAccountDataCommon.h"
#include "itemCommon.h"
#include "itemCommon_h_ast.h"
#include "inventoryCommon.h"
#include "microtransactions_common.h"
#include "microtransactions.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "PowerGrid.h"
#include "PowerGrid_h_ast.h"
#include "PowerTree_h_ast.h"
#include "Powers_h_ast.h"
#include "PowersEnums_h_ast.h"
#include "PowersAutoDesc.h"
#include "PowersAutoDesc_h_ast.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeHelpers_h_ast.h"
#include "ResourceInfo.h"
#include "ResourceManager.h"
#include "ExpressionPrivate.h"
#include "GameStringFormat.h"
#include "PowerTree.h"
#include "Tray.h"
#include "UITray.h"
#include "rgb_hsv.h"
#include "gclGroupProjectUI.h"
#include "PowerVars.h"

#include "gclUIGen_h_ast.h"
#include "Autogen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "PowerGrid_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

typedef enum
{
	PowerCartStatus_Ready,
	PowerCartStatus_PendingSet,
	PowerCartStatus_Pending,
	PowerCartStatus_ChangeComingSet,
	PowerCartStatus_ChangeComing,
	PowerCartStatus_Failed,
	PowerCartStatus_Success,

} enumPowerCartStatus;

AUTO_ENUM;
typedef enum PowerNodeFilter
{
	kPowerNodeFilter_ShowOwned				= 1 << 0, // 1
	kPowerNodeFilter_ShowAvailable			= 1 << 1, // 2
	kPowerNodeFilter_ShowUnavailable		= 1 << 2, // 4
	kPowerNodeFilter_ShowStarred			= 1 << 3, // 8
	kPowerNodeFilter_GetPowerNodeRanks		= 1 << 4, // 16
	kPowerNodeFilter_IgnoreUncategorized	= 1 << 5, // 32
	kPowerNodeFilter_ShowAvailableOwned		= 1 << 6, // 64
	kPowerNodeFilter_ShowOwnedNonEscrow		= 1 << 7, // 128
	kPowerNodeFilter_ShowPotential			= 1 << 8, // 256
} PowerNodeFilter;

static bool s_bPowerDefsUpdated = false;
static PowerListNode **s_eaCartPowerNodes = NULL;
static enumPowerCartStatus s_iPowersStatus = PowerCartStatus_Ready;
static ItemDefRef **s_eaSpentSkillPointNumerics;
static bool s_bPowersUIRequestRefsDebug;
extern S32 g_iNumOfPurposes;

AUTO_CMD_INT(s_bPowersUIRequestRefsDebug, PowersUI_RequestRefsDebug) ACMD_ACCESSLEVEL(9) ACMD_CMDLINEORPUBLIC ACMD_CATEGORY(Debug);

bool g_bCartRespecRequest = false;

static U32 s_uiLatestServerRespecTime = 0;

AUTO_RUN;
void PowerNodeFilterInitialize(void)
{
	ui_GenInitStaticDefineVars(PowerNodeFilterEnum, "PowerNode");
}

void gclPowersUIRequestRefsTreeType(PTTypeDef *pTypeDef)
{
	if (pTypeDef && pTypeDef->pchSpentPointsNumeric && *pTypeDef->pchSpentPointsNumeric)
	{
		S32 i;
		for (i = eaSize(&s_eaSpentSkillPointNumerics) - 1; i >= 0; i--)
		{
			ItemDef *pItemDef = GET_REF(s_eaSpentSkillPointNumerics[i]->hDef);
			const char *pchName = pItemDef ? pItemDef->pchName : REF_STRING_FROM_HANDLE(s_eaSpentSkillPointNumerics[i]->hDef);
			if (pchName && !stricmp(pchName, pTypeDef->pchSpentPointsNumeric))
				break;
		}
		if (i < 0)
		{
			ItemDefRef *pRef = StructCreate(parse_ItemDefRef);
			SET_HANDLE_FROM_STRING(g_hItemDict, pTypeDef->pchSpentPointsNumeric, pRef->hDef);
			eaPush(&s_eaSpentSkillPointNumerics, pRef);
			if (s_bPowersUIRequestRefsDebug)
				printf("[PowersUI:RequestRefs] Requesting spent points numeric '%s'", pTypeDef->pchSpentPointsNumeric);
		}
	}
}

void gclPowersUIRequestRefsTree(PowerTreeDef *pTreeDef)
{
	PTTypeDef *pTypeDef = pTreeDef ? GET_REF(pTreeDef->hTreeType) : NULL;
	if (pTypeDef)
		gclPowersUIRequestRefsTreeType(pTypeDef);
}

static void gclPowersUIRequestRefsDictChanged(enumResourceEventType eType, const char *pDictName, const char *pResourceName, PowerTreeDef *pResource, const char ***peapchPendingTrees)
{
	if (peapchPendingTrees && pResource && eaFindAndRemove(peapchPendingTrees, pResource->pchName) >= 0)
	{
		if (s_bPowersUIRequestRefsDebug)
		{
			printf("[PowersUI:RequestRefs] PowerTree '%s' was %s (%d pending trees remaining)", pResourceName,
				eType == RESEVENT_RESOURCE_ADDED ? "added" :
				eType == RESEVENT_RESOURCE_PRE_MODIFIED ? "pre-modified" :
				eType == RESEVENT_RESOURCE_MODIFIED ? "modified" :
				eType == RESEVENT_RESOURCE_REMOVED ? "removed" :
				eType == RESEVENT_RESOURCE_LOCKED ? "locked" :
				eType == RESEVENT_RESOURCE_UNLOCKED ? "unlocked" :
				eType == RESEVENT_INDEX_MODIFIED ? "index-modified" :
				eType == RESEVENT_NO_REFERENCES ? "no-references" :
				"poked",
				eaSize(peapchPendingTrees));
		}

		gclPowersUIRequestRefsTree(pResource);
		if (eaSize(peapchPendingTrees) <= 0)
		{
			resDictRemoveEventCallback(g_hPowerTreeDefDict, gclPowersUIRequestRefsDictChanged);
			if (s_bPowersUIRequestRefsDebug)
				printf("[PowersUI:RequestRefs] Stopped watching PowerTree dictionary");
		}
	}
}

void gclPowersUIRequestRefsTreeName(const char *pchTreeDef)
{
	static const char **s_eapchPendingTrees = NULL;
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchTreeDef);

	if (pTreeDef)
	{
		gclPowersUIRequestRefsTree(pTreeDef);
		return;
	}

	pchTreeDef = allocAddString(pchTreeDef);
	if (eaFind(&s_eapchPendingTrees, pchTreeDef) < 0)
	{
		eaPush(&s_eapchPendingTrees, pchTreeDef);
		resDictRegisterEventCallback(g_hPowerTreeDefDict, gclPowersUIRequestRefsDictChanged, (void*)&s_eapchPendingTrees);
		if (s_bPowersUIRequestRefsDebug)
			printf("[PowersUI:RequestRefs] Started watching PowerTree dictionary for '%s'", pchTreeDef);
	}
}

static void gclPowerListNodesInsertHeaders( PowerListNode ***peaNodes )
{
	S32 i, iLastPurpose = -1;
	for ( i = 0; i < eaSize(peaNodes); i++ )
	{
		PowerListNode* pCurNode = (*peaNodes)[i];
		PowerDef* pCurDef = GET_REF(pCurNode->hPowerDef);
		S32 iCurPurpose = pCurDef ? pCurDef->ePurpose : -1;
		if ( iCurPurpose != iLastPurpose )
		{
			PowerListNode* pNode = StructCreate( parse_PowerListNode );
			COPY_HANDLE(pNode->hTreeDef,pCurNode->hTreeDef);
			COPY_HANDLE(pNode->hNodeDef,pCurNode->hNodeDef);
			COPY_HANDLE(pNode->hPowerDef,pCurNode->hPowerDef);
			pNode->bIsHeader = true;
			eaInsert( peaNodes, pNode, i++ );
			iLastPurpose = iCurPurpose;
		}
	}
}

static int SortNodesByName(const PowerListNode **ppNodeA, const PowerListNode **ppNodeB)
{
	PTNodeDef *pNodeDefA = GET_REF((*ppNodeA)->hNodeDef);
	PTNodeDef *pNodeDefB = GET_REF((*ppNodeB)->hNodeDef);
	const char *pchA = pNodeDefA ? TranslateDisplayMessage(pNodeDefA->pDisplayMessage) : NULL;
	const char *pchB = pNodeDefB ? TranslateDisplayMessage(pNodeDefB->pDisplayMessage) : NULL;
	return strcoll(NULL_TO_EMPTY(pchA), NULL_TO_EMPTY(pchB));
}

S32 SortPowerListNodeByPurpose( const PowerListNode** pNodeA, const PowerListNode** pNodeB )
{
	const PowerListNode* pA = *pNodeA;
	const PowerListNode* pB = *pNodeB;
	PTNodeDef* pNodeDefA = GET_REF(pA->hNodeDef);
	PTNodeDef* pNodeDefB = GET_REF(pB->hNodeDef);
	PowerDef* pPowerDefA = GET_REF(pA->hPowerDef);
	PowerDef* pPowerDefB = GET_REF(pB->hPowerDef);
	PowerPurpose ePurposeA = kPowerPurpose_Uncategorized;
	PowerPurpose ePurposeB = kPowerPurpose_Uncategorized;

	if ( pPowerDefA )
	{
		ePurposeA = pPowerDefA->ePurpose;
	}
	else if ( pNodeDefA )
	{
		ePurposeA = pNodeDefA->ePurpose;
	}
	if ( pPowerDefB )
	{
		ePurposeB = pPowerDefB->ePurpose;
	}
	else if ( pNodeDefB )
	{
		ePurposeB = pNodeDefB->ePurpose;
	}
	if ( ePurposeA != ePurposeB )
	{
		return ePurposeA - ePurposeB;
	}
	if ( !pNodeDefB )
		return -1;
	if ( !pNodeDefA )
		return 1;
	return stricmp(pNodeDefA->pchName, pNodeDefB->pchName);
}

static const char *power_GetIcon(PowerDef *pDef)
{
	if (pDef && pDef->pchIconName)
		return pDef->pchIconName;
	else
		return "power_generic";
}

void FillPowerListNode(	Entity *pEnt,
						PowerListNode *pListNode,
						PowerTree *pTree, PowerTreeDef *pTreeDef,
						const char *pchGroup, PTGroupDef *pGroupDef,
						PTNode *pNode, PTNodeDef *pNodeDef)
{
	PTNodeDef *pParentNodeDef = NULL;
	char *pchGroupTemp = NULL;
	Power *pPower = pNode ? eaTail(&pNode->ppPowers) : NULL;
	S32 i;
	S32 j;

	if (!pTreeDef && pTree)
		pTreeDef = GET_REF(pTree->hDef);
	if (!pNodeDef && pNode)
		pNodeDef = GET_REF(pNode->hDef);

	if (pEnt && !pTree)
		pTree = (PowerTree *)entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pEnt), pTreeDef);
	if (pEnt && pEnt->pChar && !pNode && pNodeDef)
	{
		PTNodeDef *pTopNodeDef = pNodeDef;
		while (GET_REF(pTopNodeDef->hNodeClone))
		{
			pTopNodeDef = GET_REF(pTopNodeDef->hNodeClone);
		}

		for (j = 0; j < eaSize(&pEnt->pChar->ppPowerTrees); j++)
		{
			PowerTree *pCheckTree = pEnt->pChar->ppPowerTrees[j];
			for (i = 0; i < eaSize(&pCheckTree->ppNodes) && !pNode; i++)
			{
				PTNodeDef *pOtherNodeDef = GET_REF(pCheckTree->ppNodes[i]->hDef);
				PTNodeDef *pOtherTopNodeDef = pOtherNodeDef;
				while (pOtherTopNodeDef && GET_REF(pOtherTopNodeDef->hNodeClone))
				{
					pOtherTopNodeDef = GET_REF(pOtherTopNodeDef->hNodeClone);
				}
				if (pNodeDef == pOtherNodeDef || pOtherTopNodeDef == pTopNodeDef)
				{
					pNode = pCheckTree->ppNodes[i];
					pTree = pCheckTree;
				}
			}
		}
	}

	if (pGroupDef && !pchGroup)
		pchGroup = pGroupDef->pchNameFull;

	if (pNode && !pchGroup)
	{
		estrStackCreate(&pchGroupTemp);
		if (powertree_GroupNameFromNode(pNode, &pchGroupTemp))
			pchGroup = pchGroupTemp;
	}
	else if (pNodeDef && !pchGroup)
	{
		estrStackCreate(&pchGroupTemp);
		if (powertree_GroupNameFromNodeDef(pNodeDef, &pchGroupTemp))
			pchGroup = pchGroupTemp;
	}

	if (!pGroupDef && pchGroup)
		pGroupDef = RefSystem_ReferentFromString(g_hPowerTreeGroupDefDict, pchGroup);

	if (pTree)
	{
		COPY_HANDLE(pListNode->hTreeDef, pTree->hDef);
	}
	else
	{
		SET_HANDLE_FROM_REFERENT(g_hPowerTreeDefDict, pTreeDef, pListNode->hTreeDef);
	}

	if (pchGroup)
	{
		SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict, pchGroup, pListNode->hGroupDef);
	}
	else
	{
		SET_HANDLE_FROM_REFERENT(g_hPowerTreeGroupDefDict, pGroupDef, pListNode->hGroupDef);
	}

	if (pNode)
	{
		COPY_HANDLE(pListNode->hNodeDef, pNode->hDef);
	}
	else
	{
		SET_HANDLE_FROM_REFERENT(g_hPowerTreeNodeDefDict, pNodeDef, pListNode->hNodeDef);
	}

	pListNode->pTree = pTree;
	pListNode->pNode = pNode;
	pListNode->pPower = pPower;
	pListNode->pEnt = pEnt;
	// Clear the required MT. If applicable, it will get set later in this function.
	pListNode->pRequiredMicroTransaction = NULL;

	if (pPower)
	{
		pListNode->pchButtonIcon = allocAddString(exprEntGetXBoxButtonForPowerID(pEnt, pPower->uiID));
		pListNode->pchShiftButtonIcon = allocAddString(exprEntGetXBoxShiftButtonForPowerID(pEnt, pPower->uiID));
	}
	else
	{
		pListNode->pchButtonIcon = NULL;
		pListNode->pchShiftButtonIcon = NULL;
	}

	pListNode->iRank = (pNode && !pNode->bEscrow) ? (pNode->iRank + 1) : 0;
	pListNode->iMaxRank = pNodeDef ? eaSize(&pNodeDef->ppRanks) : 0;
	pListNode->iLevel = 1;
	pListNode->bIsOwned = pListNode->iRank > 0;

	if (pNodeDef)
	{
		PTNodeRankDef *pFirstRank = eaGet(&pNodeDef->ppRanks, 0);
		if(pFirstRank && pFirstRank->pRequires)
		{
			if(pFirstRank->pRequires->iTableLevel)
			{
				pListNode->iLevel = pFirstRank->pRequires->iTableLevel;
			}
			else if(pFirstRank->pRequires->pExprPurchase)
			{
				MicroTransactionDef *pRequiredTransaction;

				pRequiredTransaction = microtrans_FindDefFromPermissionExpr(pFirstRank->pRequires->pExprPurchase);
				pListNode->bPremiumEntitlement = microtrans_PremiumSatisfiesPermissionExpr(pFirstRank->pRequires->pExprPurchase);
				pListNode->bAlreadyEntitled = pListNode->bPremiumEntitlement ? (SAFE_MEMBER2(pEnt, pPlayer, playerType) == kPlayerType_Premium) : false;
				if (pRequiredTransaction)
				{
					pListNode->pRequiredMicroTransaction = pRequiredTransaction;
				}
			}
		}
		else if(pGroupDef && pGroupDef->pRequires)
		{
			if(pGroupDef->pRequires->iTableLevel)
			{
				pListNode->iLevel = pGroupDef->pRequires->iTableLevel;
			}
			else if(pGroupDef->pRequires->pExprPurchase)
			{
				// Assumption is an expression <& ExpLevel >= x &>
				if(beaSize(&pGroupDef->pRequires->pExprPurchase->postfixEArray)==3)
				{
					if(!stricmp(MultiValGetString(&pGroupDef->pRequires->pExprPurchase->postfixEArray[0],NULL),"ExpLevel"))
					{
						pListNode->iLevel = MultiValGetInt(&pGroupDef->pRequires->pExprPurchase->postfixEArray[1],NULL);
					}
				}
			}
		}
		MAX1(pListNode->iLevel, pNodeDef->iRequired);
	}

	// Get the PowerDef from the Power, or else try to find it from the matching or first rank
	//  of the Node
	if(pPower)
	{
		COPY_HANDLE(pListNode->hPowerDef, pPower->hDef);
	}
	else if (pNodeDef)
	{
		for (i = pNode ? pNode->iRank : 0; i >= 0; i--)
		{
			PTNodeRankDef *pRankDef = eaGet(&pNodeDef->ppRanks, i);
			if (pRankDef && IS_HANDLE_ACTIVE(pRankDef->hPowerDef))
			{
				COPY_HANDLE(pListNode->hPowerDef, pRankDef->hPowerDef);
				break;
			}
		}
		if (i < 0)
		{
			REMOVE_HANDLE(pListNode->hPowerDef);
		}
	}
	else
	{
		REMOVE_HANDLE(pListNode->hPowerDef);
	}

	if(GET_REF(pListNode->hPowerDef))
	{
		pListNode->pchPowerIcon = allocAddString(power_GetIcon(GET_REF(pListNode->hPowerDef)));
	}
	else if(pNodeDef)
	{
		pListNode->pchPowerIcon = allocAddString(pNodeDef->pchIconName);
	}
	else
	{
		pListNode->pchPowerIcon = NULL;
	}

	pListNode->bIsTree = (pTree || pTreeDef) && !(pNode || pNodeDef || pchGroup || pGroupDef);
	pListNode->bIsGroup = (pTree || pTreeDef) && (pchGroup || pGroupDef) && !(pNode || pNodeDef);
	pListNode->bIsHeader = !(pListNode->bIsTree || pListNode->bIsGroup) && !(pNode || pNodeDef);

	if (!(pListNode->bIsTree || pListNode->bIsGroup || pListNode->bIsHeader)
		&& (!GET_REF(pListNode->hNodeDef)
			|| (IS_HANDLE_ACTIVE(pListNode->hPowerDef) && !GET_REF(pListNode->hPowerDef))))
	{
		pListNode->bIsLoading = true;
	}
	else
	{
		pListNode->bIsLoading = false;
	}

	pListNode->bIsChildTree = false;
	estrDestroy(&pchGroupTemp);
}

void FillPowerListNodeForEnt(Entity* pRealEnt, Entity *pFakeEnt,
							 PowerListNode *pListNode,
							 PowerTree *pTree, PowerTreeDef *pTreeDef,
							 const char *pchGroup, PTGroupDef *pGroupDef,
							 PTNode *pNode, PTNodeDef *pNodeDef)
{
	if (pRealEnt && pRealEnt->pChar)
	{
		if (!pNodeDef && pNode)
			pNodeDef = GET_REF(pNode->hDef);

		if (pNodeDef)
		{
			S32 i;
			for (i = eaSize(&pRealEnt->pChar->ppTraining)-1; i >= 0; i--)
			{
				PTNodeDef* pTrainNewNode = GET_REF(pRealEnt->pChar->ppTraining[i]->hNewNodeDef);
				PTNodeDef* pTrainOldNode = GET_REF(pRealEnt->pChar->ppTraining[i]->hOldNodeDef);
				if (pTrainNewNode == pNodeDef || pTrainOldNode == pNodeDef)
				{
					pNodeDef = pTrainNewNode;
					pListNode->pTrainingInfo = pRealEnt->pChar->ppTraining[i];
					pListNode->bIsTraining = true;
					break;
				}
			}
			if (i < 0)
				pListNode->bIsTraining = false;
		}
	}

	FillPowerListNode(pFakeEnt, pListNode, pTree, pTreeDef, pchGroup, pGroupDef, pNode, pNodeDef);
}

void FillPowerListNodeFromFilterData( PowerNodeFilterCallbackData* d, PowerListNode *pListNode )
{
	pListNode->bIsOwned = d->bIsOwned;
	pListNode->bIsAvailable = d->bIsAvailable;
	pListNode->bIsAvailableForFakeEnt = d->bIsAvailableForFakeEnt;
	FillPowerListNodeForEnt(d->pRealEnt,d->pFakeEnt,pListNode,NULL,d->pTreeDef,NULL,d->pGroupDef,NULL,d->pNodeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMyTrees");
bool gclGenExprGetMyTrees(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iLength = 0;
	S32 i;

	if (SAFE_MEMBER2(pEnt, pChar, ppPowerTrees))
	{
		for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
		{
			PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
			if (eaSize(&pTree->ppNodes) > 0)
			{
				PowerListNode *pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
				FillPowerListNode(pEnt, pListNode, pTree, NULL, NULL, NULL, NULL, NULL);
			}
		}
	}
	while (eaSize(peaNodes) > iLength)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMyPowersInTree");
bool gclGenExprGetMyPowersInTree(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerListNode *pPowerListNode)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iLength = 0;
	S32 i;
	if (pPowerListNode->pTree)
	{
		for (i = 0; i < eaSize(&pPowerListNode->pTree->ppNodes); i++)
		{
			PTNode *pNode = pPowerListNode->pTree->ppNodes[i];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
			if (pNodeDef && !(pNodeDef->eFlag & kNodeFlag_HideNode))
			{
				PowerListNode *pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
				FillPowerListNode(pEnt, pListNode, pPowerListNode->pTree, NULL, NULL, NULL, pNode, NULL);
			}
		}
	}
	while (eaSize(peaNodes) > iLength)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
	return true;
}

static bool gclGenGetMyPowerList(Entity *pEnt, UIGen *pGen, bool bIncludeTrees, bool bIncludeFutureAuto)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iLength = 0;
	S32 i;
	S32 j;
	for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		PowerListNode *pListNode;
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
		int iNodes = eaSize(&pTree->ppNodes);

		if (bIncludeTrees && iNodes > 0)
		{
			pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
			FillPowerListNode(pEnt, pListNode, pTree, NULL, NULL, NULL, NULL, NULL);
		}

		for (j = 0; j < iNodes; j++)
		{
			PTNode *pNode = pTree->ppNodes[j];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
			if (pNodeDef && !(pNodeDef->eFlag & kNodeFlag_HideNode))
			{
				pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
				FillPowerListNode(pEnt, pListNode, pTree, NULL, NULL, NULL, pNode, NULL);
			}
		}

		if(bIncludeFutureAuto)
		{
			PowerTreeDef *pTreeDef = GET_REF(pTree->hDef);
			if(pTreeDef)
			{
				for(j=0; j<eaSize(&pTreeDef->ppGroups); j++)
				{
					int k;
					PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];
					for(k=0; k<eaSize(&pGroupDef->ppNodes); k++)
					{
						PTNodeDef *pNodeDef = pGroupDef->ppNodes[k];
						if(pNodeDef->eFlag & kNodeFlag_AutoBuy && !(pNodeDef->eFlag & kNodeFlag_HideNode))
						{
							int m;
							for(m=iNodes-1; m>=0; m--)
							{
								if(pNodeDef==GET_REF(pTree->ppNodes[m]->hDef))
									break;
							}
							if(m<0)
							{
								pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
								FillPowerListNode(pEnt, pListNode, pTree, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
							}
						}
					}
				}
			}
		}
	}
	while (eaSize(peaNodes) > iLength)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
	return true;
}

static S32 gclAddPowerAndAdvantageList(S32 iLength, SA_PARAM_NN_VALID PowerCartListNode ***peaCartNodes, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PTNode *pNode, SA_PARAM_OP_VALID PTNodeDef *pNodeDef, const char* pchCostTables, bool bExcludeUnownedAdvantages, bool bIncludeAllPowers)
{
	S32 iRank, iAdv;
	if (pNodeDef
		&& (!(pNodeDef->eFlag & kNodeFlag_HideNode))
		&& (eaSize(&pNodeDef->ppRanks) > 1 || eaSize(&pNodeDef->ppEnhancements) || bIncludeAllPowers))
	{
		PowerCartListNode* pCartNode = eaGetStruct(peaCartNodes, parse_PowerCartListNode, iLength++);
		PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
		PowerTree* pTree = NULL;
		S32 iNumRanksToShow = eaSize(&pNodeDef->ppRanks);
		if (!pCartNode->pPowerListNode)
			pCartNode->pPowerListNode = StructCreate(parse_PowerListNode);

		if (!pNode)
			pNode = powertree_FindNode(pEnt->pChar, &pTree, pNodeDef->pchNameFull);

		FillPowerListNode(pEnt, pCartNode->pPowerListNode, pTree, pTreeDef, NULL, NULL, pNode, pNodeDef);
		if (pCartNode->pUpgrade)
		{
			StructDestroy(parse_PTNodeUpgrade, pCartNode->pUpgrade);
			pCartNode->pUpgrade = NULL;
		}

		if (bExcludeUnownedAdvantages)
		{
			iNumRanksToShow = pCartNode->pPowerListNode->iRank;
		}

		// Fix /analyze
		assert(eaSize(&pNodeDef->ppRanks) >= 1);

		// Get ranks
		for (iRank = 0; iRank < iNumRanksToShow; iRank++)
		{
			if (!stricmp(pNodeDef->ppRanks[iRank]->pchCostTable, pchCostTables))
			{
				PowerCartListNode *pCartUpgradeNode = eaGetStruct(peaCartNodes, parse_PowerCartListNode, iLength++);
				PTNodeUpgrade *pUpgrade;
				if (!pCartUpgradeNode->pUpgrade)
					pUpgrade = pCartUpgradeNode->pUpgrade = StructCreate(parse_PTNodeUpgrade);
				else
					pUpgrade = pCartUpgradeNode->pUpgrade;
				if (pCartUpgradeNode->pPowerListNode != pCartNode->pPowerListNode)
				{
					StructDestroy(parse_PowerListNode, pCartUpgradeNode->pPowerListNode);
					pCartUpgradeNode->pPowerListNode = StructClone(parse_PowerListNode, pCartNode->pPowerListNode);
				}
				COPY_HANDLE(pUpgrade->hPowerDef, pNodeDef->ppRanks[iRank]->hPowerDef);
				SET_HANDLE_FROM_REFERENT("PTEnhTypeDef", NULL, pUpgrade->hEnhType);
				SET_HANDLE_FROM_REFERENT("PowerTreeNodeDef", pNodeDef, pUpgrade->hNode);
				pUpgrade->iCost = entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pNodeDef, iRank);
				pUpgrade->pchCostTable = allocAddString(pNodeDef->ppRanks[iRank]->pchCostTable);
				pUpgrade->pRequires = pNodeDef->ppRanks[iRank]->pRequires;
				pUpgrade->bIsRank = true;
				pUpgrade->iRank = iRank;
				pCartUpgradeNode->bIsEnhancement = false;
				pCartUpgradeNode->bIsRank = true;
			}
		}

		// Get enhancements
		for (iAdv = 0; iAdv < eaSize(&pNodeDef->ppEnhancements); iAdv++)
		{
			if (bExcludeUnownedAdvantages)
			{
				S32 iEnhancementRank;
				if (!pNode || pNode->bEscrow)
					continue;
				iEnhancementRank = powertreenode_FindEnhancementRankHelper(CONTAINER_NOCONST(PTNode, pNode), GET_REF(pNodeDef->ppEnhancements[iAdv]->hPowerDef));
				if (iEnhancementRank <= 0)
					continue;
			}

			if (!stricmp(pNodeDef->ppEnhancements[iAdv]->pchCostTable, pchCostTables))
			{
				PowerCartListNode *pCartUpgradeNode = eaGetStruct(peaCartNodes, parse_PowerCartListNode, iLength++);
				PTNodeUpgrade *pUpgrade;
				if (!pCartUpgradeNode->pUpgrade)
					pUpgrade = pCartUpgradeNode->pUpgrade = StructCreate(parse_PTNodeUpgrade);
				else
					pUpgrade = pCartUpgradeNode->pUpgrade;
				if (pCartUpgradeNode->pPowerListNode != pCartNode->pPowerListNode)
				{
					StructDestroy(parse_PowerListNode, pCartUpgradeNode->pPowerListNode);
					pCartUpgradeNode->pPowerListNode = StructClone(parse_PowerListNode, pCartNode->pPowerListNode);
				}

				COPY_HANDLE(pUpgrade->hPowerDef, pNodeDef->ppEnhancements[iAdv]->hPowerDef);
				COPY_HANDLE(pUpgrade->hEnhType, pNodeDef->ppEnhancements[iAdv]->hEnhType);
				SET_HANDLE_FROM_REFERENT("PowerTreeNodeDef", pNodeDef, pUpgrade->hNode);
				pUpgrade->iCost = pNodeDef->ppEnhancements[iAdv]->iCost;
				pUpgrade->pchCostTable = allocAddString(pNodeDef->ppEnhancements[iAdv]->pchCostTable);
				pUpgrade->pRequires = NULL;
				pUpgrade->bIsRank = false;
				pUpgrade->iRank = 0;
				pCartUpgradeNode->bIsEnhancement = true;
				pCartUpgradeNode->bIsRank = false;
			}
		}
	}
	return iLength;
}

S32 gclGetPowerAndAdvantageList(S32 iLength, SA_PARAM_NN_VALID PowerCartListNode ***peaCartNodes, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PTNode **eaNodes, SA_PARAM_OP_VALID PTNodeDef **eaNodeDefs, const char* pchCostTables, bool bExcludeUnownedAdvantages, bool bIncludeAllPowers)
{
	S32 i;

	for (i = 0; i < eaSize(&eaNodes); i++)
	{
		PTNode *pNode = eaGet(&eaNodes, i);
		PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
		iLength = gclAddPowerAndAdvantageList(iLength, peaCartNodes, pEnt, pNode, pNodeDef, pchCostTables, bExcludeUnownedAdvantages, bIncludeAllPowers);
	}

	for (i = 0; i < eaSize(&eaNodeDefs); i++)
	{
		PTNodeDef *pNodeDef = eaGet(&eaNodeDefs, i);
		iLength = gclAddPowerAndAdvantageList(iLength, peaCartNodes, pEnt, NULL, pNodeDef, pchCostTables, bExcludeUnownedAdvantages, bIncludeAllPowers);
	}

	return iLength;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMyPowerAndAdvantageList");
bool gclGenExprGetMyPowerAndAdvantageList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pchCostTable)
{
	static PTNode **s_eaNodes = NULL;
	PowerCartListNode ***peaCartNodes = ui_GenGetManagedListSafe(pGen, PowerCartListNode);
	S32 iLength = 0;
	S32 i, j;

	if (!pEnt || !pEnt->pChar || !pEnt->pChar->ppPowerTrees)
		return false;

	eaClearFast(&s_eaNodes);
	for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];

		for (j = 0; j < eaSize(&pTree->ppNodes); j++)
		{
			PTNode *pNode = pTree->ppNodes[j];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);

			if (pNodeDef
				&& !(pNodeDef->eFlag & kNodeFlag_HideNode)
				&& (eaSize(&pNodeDef->ppRanks) > 1 || eaSize(&pNodeDef->ppEnhancements)))
			{
				eaPush(&s_eaNodes, pNode);
			}
		}
	}

	iLength = gclGetPowerAndAdvantageList(iLength, peaCartNodes, pEnt, s_eaNodes, NULL, pchCostTable, false, false);
	eaSetSizeStruct(peaCartNodes, parse_PowerCartListNode, iLength);
	ui_GenSetManagedListSafe(pGen, peaCartNodes, PowerCartListNode, true);
	return true;
}

static int SortPowerListNodes(const PowerListNode **ppNodeA,
							  const PowerListNode **ppNodeB,
							  bool bSortByCategory)
{
	PTNodeDef *pNodeDefA = GET_REF((*ppNodeA)->hNodeDef);
	PTNodeDef *pNodeDefB = GET_REF((*ppNodeB)->hNodeDef);
	S32 iLevelCmp, iCategoryCmp;

	if (!pNodeDefB)
		return -1;
	else if (!pNodeDefA)
		return 1;
	else if (bSortByCategory && (iCategoryCmp = pNodeDefA->eUICategory-pNodeDefB->eUICategory))
		return iCategoryCmp;
	else if (iLevelCmp = (*ppNodeA)->iLevel-(*ppNodeB)->iLevel)
		return iLevelCmp;
	else if(!(pNodeDefA->eFlag&kNodeFlag_AutoBuy) && (pNodeDefB->eFlag&kNodeFlag_AutoBuy))
		return 1;
	else if((pNodeDefA->eFlag&kNodeFlag_AutoBuy) && !(pNodeDefB->eFlag&kNodeFlag_AutoBuy))
		return -1;
	else
	{
		const char *pchA = TranslateDisplayMessage(pNodeDefA->pDisplayMessage);
		const char *pchB = TranslateDisplayMessage(pNodeDefB->pDisplayMessage);
		return strcoll(NULL_TO_EMPTY(pchA), NULL_TO_EMPTY(pchB));
	}
}

static int SortPowerListNodesByUICategory(const PowerListNode **ppNodeA, const PowerListNode **ppNodeB)
{
	return SortPowerListNodes(ppNodeA, ppNodeB, true);
}

static int SortPowerListNodesByLevel(const PowerListNode **ppNodeA, const PowerListNode **ppNodeB)
{
	return SortPowerListNodes(ppNodeA, ppNodeB, false);
}

// This is a really stupid comparator.
static int SortPowerListNodesForCC(const PowerListNode **ppNodeA, const PowerListNode **ppNodeB)
{
	PTNodeDef *pNodeDefA = GET_REF((*ppNodeA)->hNodeDef);
	PTNodeDef *pNodeDefB = GET_REF((*ppNodeB)->hNodeDef);
	PowerDef *pPowerDefA = GET_REF((*ppNodeA)->hPowerDef);
	PowerDef *pPowerDefB = GET_REF((*ppNodeB)->hPowerDef);
	PTGroupDef *pGroupDefA = GET_REF((*ppNodeA)->hGroupDef);
	PTGroupDef *pGroupDefB = GET_REF((*ppNodeB)->hGroupDef);
	int a = -1;
	int b = -1;
	int eFighting = StaticDefineIntGetInt(PowerCategoriesEnum, "Fighting");

	if (!pNodeDefA || !pNodeDefB || !pPowerDefA || !pPowerDefB || !pGroupDefA || !pGroupDefB)
		return 0;

	if (stricmp(pNodeDefA->pchName, "block") == 0)
		a = 0;
	else if (eaiFind(&pPowerDefA->piCategories, eFighting) >= 0)
		a = 1;
	else if (stricmp(pGroupDefA->pchGroup, "auto") == 0)
		a = 2;
	else
		a = 3;

	if (stricmp(pNodeDefB->pchName, "block") == 0)
		b = 0;
	else if (eaiFind(&pPowerDefB->piCategories, eFighting) >= 0)
		b = 1;
	else if (stricmp(pGroupDefB->pchGroup, "auto") == 0)
		b = 2;
	else
		b = 3;

	if (a == b)
		return 0;
	else if (a > b)
		return 1;
	else
		return -1;
}

static int SortPTNodesByLevel(const PTNodeDef **ppNodeA, const PTNodeDef **ppNodeB)
{
	const PTNodeDef *pNodeDefA = *ppNodeA;
	const PTNodeDef *pNodeDefB = *ppNodeB;
	S32 iLevelA = 1;
	S32 iLevelB = 1;
	if (!pNodeDefA && pNodeDefB)
		return -1;
	else if (!pNodeDefB)
		return 1;


	if (eaSize(&pNodeDefA->ppRanks) && pNodeDefA->ppRanks[0]->pRequires && pNodeDefA->ppRanks[0]->pRequires->iDerivedTableLevel)
		iLevelA = pNodeDefA->ppRanks[0]->pRequires->iDerivedTableLevel;
	if (eaSize(&pNodeDefB->ppRanks) && pNodeDefB->ppRanks[0]->pRequires && pNodeDefB->ppRanks[0]->pRequires->iDerivedTableLevel)
		iLevelB = pNodeDefB->ppRanks[0]->pRequires->iDerivedTableLevel;

	if (iLevelA != iLevelB)
		return iLevelA - iLevelB;
	else
	{
		const char *pchA = pNodeDefA ? TranslateDisplayMessage(pNodeDefA->pDisplayMessage) : NULL;
		const char *pchB = pNodeDefB ? TranslateDisplayMessage(pNodeDefB->pDisplayMessage) : NULL;
		return strcoll(NULL_TO_EMPTY(pchA), NULL_TO_EMPTY(pchB));
	}
}

static S32 gclGenMergeNodesIntoList(Entity *pEnt, PowerTreeDef *pTreeDef, PTNodeDef ***peaNodes, PowerListNode ***peaListNodes, S32 iLength)
{
	S32 i;
	for (i = 0; i < eaSize(peaNodes); i++)
	{
		PowerListNode *pListNode = eaGetStruct(peaListNodes, parse_PowerListNode, iLength + i);
		FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, NULL, NULL, (*peaNodes)[i]);
	}
	return iLength + i;
}

static void gclGenGetPowerNodes(Entity *pEnt, PTNodeDef ***peaNodes, PowerTreeDef *pTreeDef)
{
	S32 i, j;

	eaClear(peaNodes);

	for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
	{
		PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];

		if (!stricmp(pGroupDef->pchGroup, "Auto"))
			continue;

		if (!entity_CanBuyPowerTreeGroupHelper(ATR_EMPTY_ARGS, entGetPartitionIdx(pEnt), CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(Entity, pEnt), pGroupDef))
			continue;

		for (j = 0; j < eaSize(&pGroupDef->ppNodes); j++)
		{
			PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
			if (pNodeDef && !(pNodeDef->eFlag & kNodeFlag_HideNode))
				eaPush(peaNodes, pNodeDef);
		}
	}

	eaQSort(*peaNodes, SortPTNodesByLevel);
}

static bool gclGenGetTreePowerList(Entity *pEnt, UIGen *pGen, const char *pchTree, bool bIncludeTree, bool bIncludeChildren)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchTree);
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	static PTNodeDef **s_eaNodes = NULL;
	S32 k, iLength = 0;
	if (pTreeDef)
	{
		PowerListNode *pListNode;
		if (bIncludeTree)
		{
			pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
			FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, NULL, NULL, NULL);
		}

		gclGenGetPowerNodes(pEnt, &s_eaNodes, pTreeDef);
		iLength = gclGenMergeNodesIntoList(pEnt, pTreeDef, &s_eaNodes, peaNodes, iLength);

		if (bIncludeChildren)
		{
			for (k = 0; k < eaSize(&pTreeDef->ppLinks); k++)
			{
				PowerTreeLink *pLink = pTreeDef->ppLinks[k];
				PowerTreeDef *pChildDef = GET_REF(pLink->hTree);
				if (pLink->eType != kPowerTreeRelationship_DependencyOf)
					continue;

				if (bIncludeTree)
				{
					pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
					FillPowerListNode(pEnt, pListNode, NULL, pChildDef, NULL, NULL, NULL, NULL);
					pListNode->bIsChildTree = true;
				}

				gclGenGetPowerNodes(pEnt, &s_eaNodes, pChildDef);
				iLength = gclGenMergeNodesIntoList(pEnt, pChildDef, &s_eaNodes, peaNodes, iLength);
			}
		}
	}

	eaSetSizeStruct(peaNodes, parse_PowerListNode, iLength);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerBuyingList");
bool gclGenExprGetPowerBuyingList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTree)
{
	if (!pEnt || !pchTree || !*pchTree)
	{
		ui_GenSetManagedListSafe(pGen, NULL, PowerListNode, true);
		return false;
	}
	else if (!stricmp(pchTree, "Mine"))
	{
		return gclGenGetMyPowerList(pEnt, pGen, true, false);
	}
	else
	{
		PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchTree);
		if (!pTreeDef)
		{
			PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
			PowerListNode *pListNode = eaGetStruct(peaNodes, parse_PowerListNode, 0);
			if (!IS_HANDLE_ACTIVE(pListNode->hTreeDef) || stricmp(REF_STRING_FROM_HANDLE(pListNode->hTreeDef), pchTree))
			{
				StructReset(parse_PowerListNode, pListNode);
				SET_HANDLE_FROM_STRING(g_hPowerTreeDefDict, pchTree, pListNode->hTreeDef);
				pListNode->bIsLoading = true;
			}
			eaSetSizeStruct(peaNodes, parse_PowerListNode, 1);
			ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
			return false;
		}
		else
		{
			gclGenGetTreePowerList(pEnt, pGen, pchTree, true, true);
			return true;
		}
	}
}

// pchMessageKey is optional. If it isn't specified, this function uses a hardcoded format for describing the attrib on a node
static void CreateNodeDefAttribDesc(char **ppchDesc, PTNodeDef* pDef, Language lang, const char* pchMessageKey)
{
	estrClear(ppchDesc);
	if(pDef && pDef->eAttrib >= 0)
	{
		const char *pchName = attrib_AutoDescName(pDef->eAttrib, lang);
		const char *pchDesc = attrib_AutoDescDesc(pDef->eAttrib, lang);
		const char *pchDescLong = attrib_AutoDescDescLong(pDef->eAttrib, lang);

		if (pchMessageKey && pchMessageKey[0])
		{
			FormatMessageKey(ppchDesc, pchMessageKey,
				STRFMT_STRING("Name", pchName),
				STRFMT_STRING("Desc", pchDesc),
				STRFMT_STRING("DescLong", pchDescLong),
				STRFMT_END);
		}
		else
		{
			estrPrintf(ppchDesc, "%s<br>%s<br><br>%s<br><br>", pchName, pchDesc, pchDescLong);
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetUnlockedNodesDescription);
const char *gclGenExprGetUnlockedNodesDescription(ExprContext *pContext, SA_PARAM_OP_VALID PowerListNode *pNode,
												  SA_PARAM_OP_VALID Entity *pEntity,
												  const char *pchUnlockMessageKey)
{
	static char *s_pchUnlockNodeDesc = NULL;
	if (pEntity && pNode && pEntity->pChar)
	{
		PTNodeDef *pNodeDef = GET_REF(pNode->hNodeDef);
		estrClear(&s_pchUnlockNodeDesc);

		// Get Unlock Nodes Desc
		if(pNodeDef) {
			int i,j,k;

			for ( i = 0; i < eaSize( &pEntity->pChar->ppPowerTrees ); i++ )
			{
				PowerTree *pTree = pEntity->pChar->ppPowerTrees[i];
				PowerTreeDef *pTreeDef = pTree ? GET_REF(pTree->hDef) : NULL;
				if(pTreeDef) {
					for ( j = 0; j < eaSize( &pTreeDef->ppGroups ); j++ )
					{
						if(character_CanBuyPowerTreeGroup(PARTITION_CLIENT, pEntity->pChar, pTreeDef->ppGroups[j]))
						{
							for(k=0; k < eaSize(&pTreeDef->ppGroups[j]->ppNodes); k++)
							{
								PTNodeDef* pUnlockNodeDef = pTreeDef->ppGroups[j]->ppNodes[k];
								if(pUnlockNodeDef && GET_REF(pUnlockNodeDef->hNodeRequire) == pNodeDef && (pUnlockNodeDef->iRequired == pNode->iRank)
									&& (EntityPTPurchaseReqsHelper(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pEntity), CONTAINER_NOCONST(Entity, pEntity), CONTAINER_NOCONST(PowerTree, pTree), pUnlockNodeDef->ppRanks[0]->pRequires)))
								{
									FormatMessageKey(&s_pchUnlockNodeDesc, pchUnlockMessageKey,
										STRFMT_STRUCT("UnlockedNodeDef", pUnlockNodeDef, parse_PTNodeDef),
										STRFMT_END);
								}
							}
						}
					}
				}
			}
		}
	}
	return s_pchUnlockNodeDesc ? s_pchUnlockNodeDesc : "";
}


static Entity* gclGetPowerListNodeDescEntity(Entity* pPlayerEnt, PowerListNode* pNode)
{
	Entity* pDescEnt = pPlayerEnt;
	PowerDef* pPowerDef = pNode ? GET_REF(pNode->hPowerDef) : NULL;

	// If this is a prop power, then we need to find the right puppet for the power
	if(pDescEnt && pNode && pPowerDef && pPowerDef->powerProp.bPropPower)
	{
		CharClassTypes ePlayerType = 0;
		if (pPlayerEnt->pChar)
		{
			CharacterClass *pClass = GET_REF(pPlayerEnt->pChar->hClass);
			if (pClass)
				ePlayerType = pClass->eType;
		}

		// Only search for a puppet if the power cannot be applied to the player's class
		if(!ePlayerType || eaiFind(&pPowerDef->powerProp.eCharacterTypes, ePlayerType)==-1)
		{
			int i;
			for(i = 0; i < eaiSize(&pPowerDef->powerProp.eCharacterTypes) && pDescEnt == pPlayerEnt; i++) {
				pDescEnt = entity_GetPuppetEntityByType( pPlayerEnt, StaticDefineIntRevLookup(CharClassTypesEnum, pPowerDef->powerProp.eCharacterTypes[i]), NULL, true, true );
				if(!pDescEnt)
					pDescEnt = pPlayerEnt;
			}
		}
	}
	if (pNode->pEnt)
	{
		return pNode->pEnt;
	}
	return pDescEnt;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeDescriptionEx");
const char *gclGenExprGetPowerListNodeDescriptionEx(ExprContext *pContext, 
													SA_PARAM_OP_VALID PowerListNode *pNode,
													SA_PARAM_OP_VALID Entity *pEntity,
													const char *pchTreeMessageKey,
													const char *pchGroupMessageKey,
													const char *pchPowerMessageKey,
													const char *pchNodeAttribMessageKey)
{
	const char *pchLine = "<br>";
	const char *pchIndent = "<bsp><bsp>";
	const char *pchModPrefix = "* ";
	static char *s_pchDescription = NULL;
	static char *s_pchAutoDesc = NULL;
	static char *s_pchNodeAttrib = NULL;
	Entity* pDescEnt = gclGetPowerListNodeDescEntity(pEntity, pNode);
	if(!pDescEnt)
		pDescEnt = pEntity;
	estrClear(&s_pchDescription);
	if (pDescEnt && pNode)
	{
		if (pNode->bIsTree)
			FormatMessageKey(&s_pchDescription, pchTreeMessageKey,
			STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
			STRFMT_END);
		else if (pNode->bIsGroup)
			FormatMessageKey(&s_pchDescription, pchGroupMessageKey,
			STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
			STRFMT_STRUCT("GroupDef", GET_REF(pNode->hGroupDef), parse_PTGroupDef),
			STRFMT_END);
		else if (pDescEnt->pChar)
		{
			Power *pPower = pNode->pPower;
			PowerDef *pPowerDef = GET_REF(pNode->hPowerDef);
			PTNodeDef *pNodeDef = GET_REF(pNode->hNodeDef);
			PTNodeRankDef *pRankDef = pNodeDef ? eaGet(&pNodeDef->ppRanks,pNode->iRank) : NULL;
			Language lang = entGetLanguage(pDescEnt);
			estrClear(&s_pchAutoDesc);
			CreateNodeDefAttribDesc(&s_pchNodeAttrib, pNodeDef, lang, pchNodeAttribMessageKey);

			if (!pPower)
			{
				// For STO:
				// If the power wasn't included, first check the description entity to see if it's on there.
				// If it was found, then check to see if pEntity has the same power via Propagation.
				// If the power is available via Propagation, use that for AutoDesc instead of the DescEnt's power.
				PTNode *pEntNode = pNodeDef ? powertree_FindNode(pDescEnt->pChar, NULL, pNodeDef->pchNameFull) : NULL;
				Power *pPropPower;
				if (pEntNode && pEntNode->iRank >= 0 && pEntNode->iRank < eaSize(&pEntNode->ppPowers))
					pPower = pEntNode->ppPowers[pEntNode->iRank];
				if (pEntNode && pNode->iRank - 1 != pEntNode->iRank && pNode->iRank - 1 >= 0 && pNode->iRank - 1 < eaSize(&pEntNode->ppPowers))
					pPower = pEntNode->ppPowers[pNode->iRank - 1];
				pPropPower = pPower && pEntity && pEntity->pChar ? character_FindPowerByID(pEntity->pChar, pPower->uiID) : NULL;
				if (pPropPower && pPropPower->eSource == kPowerSource_Propagation)
				{
					pPower = pPropPower;
					pDescEnt = pEntity;
				}
				else if (pPowerDef && pPowerDef->powerProp.bPropPower && pEntity)
				{
					S32 iType;
					CharClassCategorySet *pSet = pEntity->pSaved && pEntity->pSaved->pPuppetMaster ? GET_REF(pEntity->pSaved->pPuppetMaster->hPreferredCategorySet) : NULL;
					for (iType = eaiSize(&pPowerDef->powerProp.eCharacterTypes) - 1; iType >= 0; iType--)
					{
						Entity *pSubEnt = entity_GetPuppetEntityByType(pEntity, StaticDefineIntRevLookup(CharClassTypesEnum, pPowerDef->powerProp.eCharacterTypes[iType]), pSet, true, true);
						if (pSubEnt)
						{
							pDescEnt = pSubEnt;
							pPower = NULL;
						}
					}
				}
			}

			if(!pDescEnt->pChar->iLevelCombat)
			{
				// Someone has done something naughty, like pass a severely fake Character.  Make sure the
				//  combat level is at least zero, though this really implies the wrong Character is being
				//  used for description.
				pDescEnt->pChar->iLevelCombat = 1;
			}

			if (pPower)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				power_AutoDesc(entGetPartitionIdx(pEntity), pPower, pDescEnt->pChar, &s_pchAutoDesc, NULL, pchLine, pchIndent, pchModPrefix, false, 0, entGetPowerAutoDescDetail(pDescEnt,false), pExtract,NULL);
			}
			else if (pPowerDef)
			{
				GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
				g_CombatEvalOverrides.bEnabled = true;
				g_CombatEvalOverrides.bAttrib = true;
				STONormalizeAttribs(pDescEnt);
				//g_CombatEvalOverrides.bNodeRank = true;
				//g_CombatEvalOverrides.iNodeRank = 1;

				powerdef_AutoDesc(entGetPartitionIdx(pEntity), pPowerDef, &s_pchAutoDesc, NULL, pchLine, pchIndent, pchModPrefix,
					pDescEnt->pChar, pPower, NULL, pDescEnt->pChar->iLevelCombat, true, entGetPowerAutoDescDetail(pDescEnt,false), pExtract, NULL);

				// Disable attribute overrides
				g_CombatEvalOverrides.bEnabled = false;
				g_CombatEvalOverrides.bAttrib = false;
				//g_CombatEvalOverrides.bNodeRank = false;
			}
			else if(!pRankDef || !pRankDef->bEmpty)
				estrCopy2(&s_pchAutoDesc, "...");

			FormatMessageKey(&s_pchDescription, pchPowerMessageKey,
				STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
				STRFMT_STRUCT("GroupDef", GET_REF(pNode->hGroupDef), parse_PTGroupDef),
				STRFMT_STRUCT("NodeDef", GET_REF(pNode->hNodeDef), parse_PTNodeDef),
				STRFMT_STRING("NodeDefAttrib", s_pchNodeAttrib),
				STRFMT_STRUCT("PowerDef", pPowerDef, parse_PowerDef),
				STRFMT_STRUCT("Power", pPower, parse_Power),
				STRFMT_STRING("PowerDescription", s_pchAutoDesc),
				STRFMT_END);
		}
	}
	return s_pchDescription ? s_pchDescription : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeDescription");
const char *gclGenExprGetPowerListNodeDescription(ExprContext *pContext, 
												  SA_PARAM_OP_VALID PowerListNode *pNode,
												  SA_PARAM_OP_VALID Entity *pEntity,
												  const char *pchTreeMessageKey,
												  const char *pchGroupMessageKey,
												  const char *pchPowerMessageKey)
{
	return gclGenExprGetPowerListNodeDescriptionEx(pContext, pNode, pEntity, pchTreeMessageKey, pchGroupMessageKey, pchPowerMessageKey, NULL);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeDescriptionShortEx");
const char *gclGenExprGetPowerListNodeDescriptionShortEx(ExprContext *pContext, 
														 SA_PARAM_OP_VALID PowerListNode *pNode,
														 SA_PARAM_OP_VALID Entity *pEntity,
														 const char *pchTreeMessageKey,
														 const char *pchGroupMessageKey,
														 const char *pchPowerMessageKey,
														 const char *pchNodeAttribMessageKey)
{
	const char *pchLine = "<br>";
	const char *pchIndent = "<bsp><bsp>";
	const char *pchModPrefix = "* ";
	static char *s_pchDescription = NULL;
	static char *s_pchAutoDesc = NULL;
	static char *s_pchNodeAttrib = NULL;
	estrClear(&s_pchDescription);
	if (pEntity && pNode)
	{
		if (pNode->bIsTree)
			FormatMessageKey(&s_pchDescription, pchTreeMessageKey,
			STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
			STRFMT_END);
		else if (pNode->bIsGroup)
			FormatMessageKey(&s_pchDescription, pchGroupMessageKey,
			STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
			STRFMT_STRUCT("GroupDef", GET_REF(pNode->hGroupDef), parse_PTGroupDef),
			STRFMT_END);
		else if (pEntity->pChar)
		{
			Power *pPower = pNode->pPower;
			PowerDef *pPowerDef = GET_REF(pNode->hPowerDef);
			PTNodeDef *pNodeDef = GET_REF(pNode->hNodeDef);
			Language lang = entGetLanguage(pEntity);
			estrClear(&s_pchAutoDesc);
			estrCopy2(&s_pchAutoDesc, "...");
			CreateNodeDefAttribDesc(&s_pchNodeAttrib, pNodeDef, lang, pchNodeAttribMessageKey);
			FormatMessageKey(&s_pchDescription, pchPowerMessageKey,
				STRFMT_STRUCT("TreeDef", GET_REF(pNode->hTreeDef), parse_PowerTreeDef),
				STRFMT_STRUCT("GroupDef", GET_REF(pNode->hGroupDef), parse_PTGroupDef),
				STRFMT_STRUCT("NodeDef", pNodeDef, parse_PTNodeDef),
				STRFMT_STRING("NodeDefAttrib", s_pchNodeAttrib),
				STRFMT_STRUCT("PowerDef", pPowerDef, parse_PowerDef),
				STRFMT_STRUCT("Power", pPower, parse_Power),
				STRFMT_STRING("PowerDescription", s_pchAutoDesc),
				STRFMT_END);
		}
	}
	return s_pchDescription ? s_pchDescription : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeDescriptionShort");
const char *gclGenExprGetPowerListNodeDescriptionShort(ExprContext *pContext, 
													   SA_PARAM_OP_VALID PowerListNode *pNode,
													   SA_PARAM_OP_VALID Entity *pEntity,
													   const char *pchTreeMessageKey,
													   const char *pchGroupMessageKey,
													   const char *pchPowerMessageKey)
{
	return gclGenExprGetPowerListNodeDescriptionShortEx(pContext, pNode, pEntity, pchTreeMessageKey, pchGroupMessageKey, pchPowerMessageKey, NULL);
}

//iRank is 1-based, as that is what is commonly used in the UI - i.e. PowerListNode::iRank
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeAttribDescriptionForRank");
const char *gclGenExprGetPowerListNodeAttribDescriptionForRank(SA_PARAM_OP_VALID PowerListNode *pNode, S32 iRank)
{
	static char *s_pchDescription = NULL;
	Entity* pEntity = entActivePlayerPtr();

	estrClear(&s_pchDescription);

	if(pEntity && pNode)
	{
		PTNodeDef* pDef = GET_REF(pNode->hNodeDef);

		if(pDef && pDef->eAttrib >= 0)
		{
			if(iRank > 0)
			{
				powertreenode_AutoDescAttrib(&s_pchDescription, pDef, iRank-1, entGetLanguage(pEntity));
			}
		}
	}

	return s_pchDescription ? s_pchDescription : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListNodeAttribDescription");
const char *gclGenExprGetPowerListNodeAttribDescription(SA_PARAM_OP_VALID PowerListNode *pNode)
{
	return gclGenExprGetPowerListNodeAttribDescriptionForRank( pNode, pNode ? pNode->iRank : 0 );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetOriginModel");
void gclGenExprSetOriginModel(ExprContext *pContext,
	SA_PARAM_NN_VALID UIGen *pGen,
	SA_PARAM_OP_VALID Entity *pEntity,
	const char *pchPowerName)
{
	AutoDescAttribMod ***peaInfos = ui_GenGetManagedListSafe(pGen, AutoDescAttribMod);
	PowerDef *pdef;

	if (pEntity
		&& pEntity->pChar
		&& (pdef = RefSystem_ReferentFromString(g_hPowerDefDict, pchPowerName)))
	{
		AutoDesc_InnateAttribMods(entGetPartitionIdx(pEntity), pEntity->pChar, pdef, peaInfos);
		ui_GenSetManagedListSafe(pGen, peaInfos, AutoDescAttribMod, true);
	}
	else
	{
		ui_GenSetManagedListSafe(pGen, peaInfos, AutoDescAttribMod, true);
	}
}

const char* gclAutoDescPower(Entity *pEnt, SA_PARAM_OP_VALID Power *pPower, PowerDef *pPowerDef, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey, bool bTooltip)
{
	static char *s_pchDescription = NULL;
	AutoDescPower *pAutoDescPower = NULL;

	estrClear(&s_pchDescription);
	if (pEnt && pEnt->pChar && pchPowerMessageKey && pchAttribModsMessageKey)
	{
		pAutoDescPower = StructCreate(parse_AutoDescPower);

		if (pPower)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			power_AutoDesc(entGetPartitionIdx(pEnt), pPower, pEnt->pChar, NULL, pAutoDescPower, NULL, NULL, NULL, false, 0, entGetPowerAutoDescDetail(pEnt, bTooltip), pExtract,NULL);
		}
		else if (pPowerDef)
		{
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			powerdef_AutoDesc(entGetPartitionIdx(pEnt), pPowerDef, NULL, pAutoDescPower, NULL, NULL, NULL,
				pEnt->pChar, NULL, NULL, pEnt->pChar->iLevelCombat, true, entGetPowerAutoDescDetail(pEnt, bTooltip), pExtract, NULL);
		}
		if (pPower || pPowerDef)
		{
			if (!pPowerDef)
				pPowerDef = GET_REF(pPower->hDef);

			powerdef_AutoDescCustom(pEnt, &s_pchDescription, pPowerDef, pAutoDescPower, pchPowerMessageKey, pchAttribModsMessageKey);
		}

		StructDestroy(parse_AutoDescPower, pAutoDescPower);
	}

	return s_pchDescription;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAutoDescPower");
const char* gclGenExprAutoDescPower(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Power *pPower, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	return gclAutoDescPower(pEnt, pPower, NULL, pchPowerMessageKey, pchAttribModsMessageKey, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAutoDescPowerDef");
const char* gclGenExprAutoDescPowerDef(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerDef *pPowerDef, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	return gclAutoDescPower(pEnt, NULL, pPowerDef, pchPowerMessageKey, pchAttribModsMessageKey, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAutoDescTrayElem");
const char* gclGenExprAutoDescTrayElem(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID UITrayElem *pElem, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	if (pElem && pElem->pPetData)
	{
		Entity *pPet = entFromEntityRefAnyPartition(pElem->pPetData->erOwner);
		PowerDef *pDef = powerdef_Find(pElem->pPetData->pchPower);
		if (pDef)
		{
			if (!pPet && pEnt && pEnt->pPlayer)
			{
				int i, s=eaSize(&pEnt->pPlayer->petInfo);
				for (i=0;i<s;i++)
				{
					PetPowerState *pState = playerPetInfo_FindPetPowerStateByName(pEnt->pPlayer->petInfo[i], pDef->pchName);
					if (pState)
					{
						pPet = entFromEntityRefAnyPartition(pEnt->pPlayer->petInfo[i]->iPetRef);
						break;
					}
				}
			}
			if (pPet)
				pDef = PetEntGuessExecutedPower(pPet, pDef);

			// See TODO above
			return gclAutoDescPower(pPet, NULL, pDef, pchPowerMessageKey, pchAttribModsMessageKey, true);
		}
		else if (pElem->estrShortDesc && *pElem->estrShortDesc)
		{
			return pElem->estrShortDesc;
		}
	}
	else if(pElem && pElem->pTrayElem)
	{
		if(pElem->pTrayElem->eType == kTrayElemType_Macro)
		{
			S32 iMacro = entity_FindMacroByID(pEnt, pElem->pTrayElem->lIdentifier);
			if (iMacro >= 0)
			{
				return pEnt->pPlayer->pUI->eaMacros[iMacro]->pchDescription;
			}
		}
		else if(pElem->pTrayElem->erOwner)
		{
			PetPowerState *pState = UITrayElemGetPetPowerState(pElem);
			if(pState && GET_REF(pState->hdef))
			{
				// TODO(JW): This is not the technically correct call, there should be no entity passed
				//  but then I'd have to fix all the rest of this code
				return gclAutoDescPower(entFromEntityRefAnyPartition(pElem->pTrayElem->erOwner), NULL, GET_REF(pState->hdef), pchPowerMessageKey, pchAttribModsMessageKey, true);
			}
		}
		else if(pEnt)
		{
			Power *pPower = EntTrayGetActivatedPower(pEnt,pElem->pTrayElem,pElem->bUseBaseReplacePower,NULL);
			if (pPower)
				return gclAutoDescPower(pEnt, pPower, NULL, pchPowerMessageKey, pchAttribModsMessageKey, true);
		}
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAutoDescPowerListNode");
const char* gclGenExprAutoDescPowerListNode(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode *pListNode, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	PowerDef *pPowerDef = pListNode ? GET_REF(pListNode->hPowerDef) : NULL;
	if (SAFE_MEMBER(pListNode, pPower) || pPowerDef)
		return gclAutoDescPower(pEnt, pListNode->pPower, pPowerDef, pchPowerMessageKey, pchAttribModsMessageKey, false);
	else
		return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAutoDescPowerDefByName");
const char* gclGenExprAutoDescPowerDefByName(SA_PARAM_OP_VALID Entity *pEnt, const char *pchPowerName, const char* pchPowerMessageKey, const char* pchAttribModsMessageKey)
{
	PowerDef *pPowerDef = powerdef_Find(pchPowerName);
	if (pPowerDef)
		return gclAutoDescPower(pEnt, NULL, pPowerDef, pchPowerMessageKey, pchAttribModsMessageKey, false);
	else
		return "";
}

bool isPowerOwned(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerDef *pPowerDef)
{
	S32 i, j, k;
	// For each tree
	for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
		if (pTree && pTree->ppNodes)
		{
			for (j = 0; j < eaSize(&pTree->ppNodes); j++)
			{
				PTNode *pNode = pTree->ppNodes[j];
				PTNodeDef *pNodeDef = pNode ? GET_REF(pNode->hDef) : NULL;

				if (pNodeDef)
				{
					for (k = 0; k < eaSize(&pNodeDef->ppRanks); k++)
					{
						PowerDef *pOtherPowerDef = pNodeDef ? GET_REF(pNodeDef->ppRanks[k]->hPowerDef) : NULL;

						if (pPowerDef && pOtherPowerDef && (pPowerDef == pOtherPowerDef))
						{
							return true;
						}
					}
				}
			}
		}
	}
	return false;
}

static void powerDefsDictUpdated(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	s_bPowerDefsUpdated = true;
}

static PowerTreeUIData *s_pPowerTreeHolder = NULL;
static PTUICategoryListNode **s_eaCategoryTabs = NULL;
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenInitPowerPurchaseUI");
void gclGenExprInitPowerPurchaseUI(const char* pchAllTrees)
{
	int i;
	char *pchTreeCopy;
	char *pchContext;
	char *pchStart;

	// Register a callback to listen for updates
	resDictRegisterEventCallback(g_hPowerDefDict, powerDefsDictUpdated, NULL);

	// Create a list of UI category tabs
	for (i = 0 ; i < g_iNumOfPowerUICategories; i++)
	{
		PTUICategoryListNode *pNode = StructCreate(parse_PTUICategoryListNode);
		eaPush(&s_eaCategoryTabs, pNode);
		//pNode->eCategoryEnum = i;
		pNode->pchUICategoryName = StaticDefineIntRevLookup(PowerTreeUICategoryEnum, i);
	}

	// Create references to each power tree to force them to load on the client
	strdup_alloca(pchTreeCopy, pchAllTrees);
	pchStart = strtok_r(pchTreeCopy, " ,\t\r\n", &pchContext);
	s_pPowerTreeHolder = StructCreate(parse_PowerTreeUIData);
	if (pchStart)
	{
		do
		{
			// Find out if this is a tree name or a specific group in the tree
			// Gets the delimiting token
			char* c = strpbrk(pchStart, ". ,\t\r\n\0");

			PowerTreeDefRef *pPTDefRef = StructCreate(parse_PowerTreeDefRef);
			if (c && *c == '.')
			{
				*c = '\0';
				SET_HANDLE_FROM_STRING("PowerTreeDef", pchStart, pPTDefRef->hRef);
			}
			else
			{
				SET_HANDLE_FROM_STRING("PowerTreeDef", pchStart, pPTDefRef->hRef);
			}
			eaPush(&s_pPowerTreeHolder->eaPTRefs, pPTDefRef);
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenDeinitPowerPurchaseUI");
void gclGenExprDeinitPowerPurchaseUI()
{
	resDictRemoveEventCallback(g_hPowerDefDict, powerDefsDictUpdated);

	StructDestroy(parse_PowerTreeUIData, s_pPowerTreeHolder);

	// Create a list of UI category tabs
	while(eaSize(&s_eaCategoryTabs))
	{
		StructDestroy(parse_PTUICategoryListNode, eaPop(&s_eaCategoryTabs));
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenShowCategoriesWithOwnedPowers");
void gclGenExprShowCategoriesWithOwnedPowers(SA_PARAM_OP_VALID Entity* pEnt)
{
	S32 i, j, k;
	for (i = 0 ; i < eaSize(&s_pPowerTreeHolder->eaPTRefs); i++)
	{
		PowerTreeDef *pTreeDef = GET_REF(s_pPowerTreeHolder->eaPTRefs[i]->hRef);
		if (pTreeDef)
		{
			PTUICategoryListNode *pCategory = s_eaCategoryTabs[pTreeDef->eUICategory];
			if (pCategory)
			{
				PowerTreeHolder* pHolder = NULL;
				for (j = 0; j < eaSize(&pCategory->eaTreeHolder); j++)
				{
					if (pCategory->eaTreeHolder[j]->pTreeDefRef
						&& pTreeDef == GET_REF(pCategory->eaTreeHolder[j]->pTreeDefRef->hRef))
					{
						pHolder = pCategory->eaTreeHolder[j];
						break;
					}
				}
				if (pHolder)
				{
					for (j = 0; j < eaSize(&pTreeDef->ppGroups); j++)
					{
						PTGroupDef *pGroupDef = pTreeDef->ppGroups[j];
						for (k = 0; k < eaSize(&pGroupDef->ppNodes); k++)
						{
							PTNodeDef *pNodeDef = pGroupDef->ppNodes[k];
							if (pNodeDef && pNodeDef->ppRanks[0])
							{
								PowerDef *pPowerDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);
								if (isPowerOwned(pEnt, pPowerDef))
								{
									pCategory->bShowCategory = true;
									pHolder->bShowTree = true;
								}
							}
						}
					}
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGenerateFilterString");
const char* gclGenExprGenerateFilterString(SA_PARAM_NN_VALID UIGen *pGen)
{
	PTUICategoryListNode ***peaUICategories = ui_GenGetManagedListSafe(pGen, PTUICategoryListNode);
	static char buffer[512];
	int i, j;
	buffer[0] = 0;
	for (i = 0; i < eaSize(peaUICategories); i++)
	{
		PTUICategoryListNode* pCategory = (*peaUICategories)[i];
		for (j = 0; j < eaSize(&pCategory->eaTreeHolder); j++)
		{
			PowerTreeHolder *pHolder = pCategory->eaTreeHolder[j];
			if(pHolder->bShowTree)
			{
				strcat_s(buffer, sizeof(buffer), pHolder->pchTreeName);
				strcat_s(buffer, sizeof(buffer), " ");
			}
		}
	}
	ui_GenSetListSafe(pGen, peaUICategories, PTUICategoryListNode);
	return buffer;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTrees");
void gclGenExprGetPowerTrees(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID PTUICategoryListNode *pUICategory)
{
	ui_GenSetManagedListSafe(pGen, &pUICategory->eaTreeHolder, PowerTreeHolder, false);
}

//
// Expressions to set and clear tab selection (Both supertabs
// and their subtabs) in the power ui.
//

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetAllUICategoryFilters");
void gclGenExprSetAllUICategoryFilters(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bState)
{
	PTUICategoryListNode ***peaUICategories = ui_GenGetManagedListSafe(pGen, PTUICategoryListNode);
	S32 i;
	for (i = 0; i < eaSize(peaUICategories); i++)
	{
		(*peaUICategories)[i]->bShowCategory = bState;
	}
	ui_GenSetListSafe(pGen, peaUICategories, PTUICategoryListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetUICategoryFilter");
void gclGenExprSetUICategoryFilter(ExprContext *pContext, SA_PARAM_NN_VALID PTUICategoryListNode *pCategory, bool bState)
{
	pCategory->bShowCategory = bState;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetAllPowerTreeFiltersInCategory");
void gclGenExprSetAllPowerTreeFiltersInCategory(ExprContext *pContext, SA_PARAM_NN_VALID PTUICategoryListNode *pCategory, bool bState)
{
	int i;
	for (i = 0; i < eaSize(&pCategory->eaTreeHolder); i++)
	{
		pCategory->eaTreeHolder[i]->bShowTree = bState;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetAllPowerTreeFiltersExcept");
void gclGenExprSetAllPowerTreeFiltersExcept(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID PTUICategoryListNode *pCategory, bool bState)
{
	PTUICategoryListNode ***peaUICategories = ui_GenGetManagedListSafe(pGen, PTUICategoryListNode);
	int i, j;
	for (i = 0; i < eaSize(peaUICategories); i++)
	{
		PTUICategoryListNode* pOtherCategory = (*peaUICategories)[i];
		if (pCategory != pOtherCategory)
		{
			pOtherCategory->bShowCategory |= bState;
			for (j = 0; j < eaSize(&pOtherCategory->eaTreeHolder); j++)
			{
				PowerTreeHolder *pHolder = pOtherCategory->eaTreeHolder[j];
				pHolder->bShowTree = bState;
			}
		}
	}
	ui_GenSetListSafe(pGen, peaUICategories, PTUICategoryListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetAllPowerTreeFilters");
void gclGenExprSetAllPowerTreeFilters(SA_PARAM_NN_VALID UIGen *pGen, bool bState)
{
	PTUICategoryListNode ***peaUICategories = ui_GenGetManagedListSafe(pGen, PTUICategoryListNode);
	int i, j;
	for (i = 0; i < eaSize(peaUICategories); i++)
	{
		PTUICategoryListNode* pCategory = (*peaUICategories)[i];
		pCategory->bShowCategory |= bState;
		for (j = 0; j < eaSize(&pCategory->eaTreeHolder); j++)
		{
			PowerTreeHolder *pHolder = pCategory->eaTreeHolder[j];
			pHolder->bShowTree = bState;
		}
	}
	ui_GenSetListSafe(pGen, peaUICategories, PTUICategoryListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSetPowerTreeFilter");
void gclGenExprSetPowerTreeFilter(ExprContext *pContext, SA_PARAM_NN_VALID PowerTreeHolder *pHolder, bool bState)
{
	pHolder->bShowTree = bState;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeFilter");
const char* gclGenExprGetPowerTreeFilter(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, bool bShowAll)
{
	PowerTreeHolder ***peaHolder = ui_GenGetManagedListSafe(pGen, PowerTreeHolder);
	static char buffer[512];
	int i;

	buffer[0] = 0;
	for (i = 0; i < eaSize(peaHolder); i++)
	{
		if ((*peaHolder)[i]
		    && ((*peaHolder)[i]->bShowTree || bShowAll))
		{
			strcat_s(buffer, sizeof(buffer), (*peaHolder)[i]->pchTreeName);
			strcat_s(buffer, sizeof(buffer), " ");
		}
	}
	ui_GenSetListSafe(pGen, peaHolder, PowerTreeHolder);
	return buffer;
}

S32 gclFrameworkComparitor(const void *unused, const PowerTreeDef** a, const PowerTreeDef** b)
{
	if (a && *a && b && *b)
	{
		if ((*a)->eUICategory < (*b)->eUICategory)
		{
			return -1;
		}
		else if ((*a)->eUICategory > (*b)->eUICategory)
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


// Same comparitor as above, but for power list nodes (PLN).
S32 gclPLNFrameworkComparitor(const void *unused, const PowerListNode** a, const PowerListNode** b)
{
	if (a && *a && b && *b)
	{
		PowerTreeDef* aTree = GET_REF((*a)->hTreeDef);
		PowerTreeDef* bTree = GET_REF((*b)->hTreeDef);
		if (aTree->eUICategory < bTree->eUICategory)
		{
			return -1;
		}
		else if (aTree->eUICategory > bTree->eUICategory)
		{
			return 1;
		}
		else
		{
			return stricmp(aTree->pchName, bTree->pchName);
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSortFrameworks");
void gclGenExprSortFrameworks(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	PowerTreeDef ***peaHolder = ui_GenGetManagedListSafe(pGen, PowerTreeDef);
	if (peaHolder && *peaHolder)
	{
		eaQSort_s(*peaHolder, gclFrameworkComparitor, pGen);
	}
	ui_GenSetListSafe(pGen, peaHolder, PowerTreeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSortPowerListNodesByFramework");
void gclGenExprSortPowerListNodesByFramework(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	PowerListNode ***peaPowerListNode = ui_GenGetManagedListSafe(pGen, PowerListNode);
	if (peaPowerListNode && *peaPowerListNode)
	{
		eaQSort_s(*peaPowerListNode, gclPLNFrameworkComparitor, pGen);
	}
	ui_GenSetListSafe(pGen, peaPowerListNode, PowerListNode);
}

//a helper function to get all power tree names in the filter format similar to the function above
//to be injected into the expression "GenFillCategorizedPowerLists"
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetAllPowerTreesAsString");
const char* gclGenExprGetAllPowerTreesAsString(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt )
{
	static char buffer[1024];

	buffer[0] = 0;

	if ( pEnt )
	{
		int i;

		for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
		{
			PowerTreeDef* pDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef );

			if ( pDef )
			{
				strcat_s(buffer, sizeof(buffer), pDef->pchName);
				strcat_s(buffer, sizeof(buffer), " ");
			}
		}
	}

	return buffer;
}

// Gets all the UI Categories for the given trees.
// This is a little more complicated than simply looking at the list of categories, because we
// only want to pull up the categories for the trees we're given
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeUICategories");
void gclGenExprGetPowerTreeUICategories(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen)
{
	PTUICategoryListNode ***peaUICategories = ui_GenGetManagedListSafe(pGen, PTUICategoryListNode);
	int i, count = 0;

	if (s_eaCategoryTabs)
	{
		for (i = 0; i < g_iNumOfPowerUICategories; i++)
		{
			s_eaCategoryTabs[i]->iListSize = 0;
		}

		// Place each tree in a Category.
		for (i = 0 ; i < eaSize(&s_pPowerTreeHolder->eaPTRefs); i++)
		{
			PowerTreeDef *pDef = GET_REF(s_pPowerTreeHolder->eaPTRefs[i]->hRef);
			if (pDef)
			{
				PTUICategoryListNode *pCategory = s_eaCategoryTabs[pDef->eUICategory];
				if (pCategory)
				{
					PowerTreeHolder* pHolder = eaGetStruct(&pCategory->eaTreeHolder, parse_PowerTreeHolder, pCategory->iListSize++);
					pHolder->pchTreeName = pDef->pchName;
					pHolder->pTreeDefRef = s_pPowerTreeHolder->eaPTRefs[i];
				}
			}
		}

		// Remove excess trees
		for (i = 0; i < g_iNumOfPowerUICategories; i++)
		{
			while (eaSize(&s_eaCategoryTabs[i]->eaTreeHolder) > s_eaCategoryTabs[i]->iListSize)
			{
				StructDestroy(parse_PowerTreeHolder, eaPop(&s_eaCategoryTabs[i]->eaTreeHolder));
			}
		}

		// Build the list of categories with trees in them
		for (i = 0 ; i < eaSize(&s_eaCategoryTabs); i++)
		{
			if (eaSize(&s_eaCategoryTabs[i]->eaTreeHolder))
			{
				if (count < eaSize(peaUICategories))
				{
					(*peaUICategories)[count] = s_eaCategoryTabs[i];
				}
				else
				{
					eaPush(peaUICategories, s_eaCategoryTabs[i]);
				}
				count++;
			}
		}
	}

	// Remove excess categories.
	while (eaSize(peaUICategories) > count)
		StructDestroy(parse_PTUICategoryListNode, eaPop(peaUICategories));

	ui_GenSetListSafe(pGen, peaUICategories, PTUICategoryListNode);
}

// ********************************** POWER CART **********************************

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartCheckRespec");
bool gclGenExprGenPowerCartCheckRespec(void)
{
	return g_bCartRespecRequest;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenClearPowerCart");
void gclGenExprClearPowerCart(void)
{
	eaClearStruct( &s_eaCartPowerNodes, parse_PowerListNode );
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenLoadPowerCartList");
void gclGenExprLoadPowerCartList(SA_PARAM_OP_VALID Entity *pEnt)
{
	ServerCmd_gslLoadPowerCartList();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenSavePowerCartList");
void gclGenExprSavePowerCartList(SA_PARAM_OP_VALID Entity *pEnt)
{
	if ( pEnt && pEnt->pPlayer )
	{
		int i;

		SavedCartPowerList* pList = StructCreate( parse_SavedCartPowerList );

		for( i = 0; i < eaSize( &s_eaCartPowerNodes ); i++ )
		{
			SavedCartPower *pSavedPower = StructCreate( parse_SavedCartPower );

			COPY_HANDLE( pSavedPower->hNodeDef, s_eaCartPowerNodes[i]->hNodeDef );

			pSavedPower->iRank = s_eaCartPowerNodes[i]->iRank - 1; //PowerListNode's rank is +1 from internal

			eaPush( &pList->ppNodes, pSavedPower );
		}

		ServerCmd_gslSavePowerCartList( pList );

		StructDestroy( parse_SavedCartPowerList, pList );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclLoadPowerCartList( Entity *pEnt, SavedCartPowerList* pList )
{
	int i;

	if ( pList == NULL || pList->ppNodes == NULL )
		return;

	//make sure the list is empty
	gclGenExprClearPowerCart(); //eaClearStruct( &s_eaCartPowerNodes, parse_PowerListNode );

	//fill in the data
	for ( i = 0; i < eaSize( &pList->ppNodes ); i++ )
	{
		PowerListNode* pListNode = StructCreate( parse_PowerListNode );

		PTNodeDef* pNodeDef = GET_REF( pList->ppNodes[i]->hNodeDef );

		FillPowerListNode( pEnt, pListNode, NULL, powertree_TreeDefFromNodeDef( pNodeDef ),
			NULL, powertree_GroupDefFromNodeDef( pNodeDef ),
			NULL, pNodeDef );

		eaPush( &s_eaCartPowerNodes, pListNode );
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CLIENTCMD ACMD_PRIVATE;
void gclNotifyPowerCartPurchaseFailure( Entity* pEnt )
{
	if ( pEnt==NULL )
		return;

	s_iPowersStatus = PowerCartStatus_Failed;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowersStatusReady");
bool gclGenExprPowersStatusReady(void)
{
	return s_iPowersStatus == PowerCartStatus_Ready;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowersStatusFailed");
bool gclGenExprPowersStatusFailed(void)
{
	return s_iPowersStatus == PowerCartStatus_Failed;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowersChangedOnFakeEnt");
bool gclGenExprPowersChangedOnFakeEnt( SA_PARAM_OP_VALID Entity* pRealEnt, SA_PARAM_OP_VALID Entity* pFakeEnt, SA_PARAM_NN_STR const char* pchPoints )
{
	if ( pRealEnt && pFakeEnt && pRealEnt->pChar && pFakeEnt->pChar )
	{
		S32 iRealSpent = entity_PointsSpent( CONTAINER_NOCONST(Entity, pRealEnt), pchPoints );

		S32 iFakeSpent = entity_PointsSpent( CONTAINER_NOCONST(Entity, pFakeEnt), pchPoints );

		if(iRealSpent == iFakeSpent && g_bCartRespecRequest)
		{
			//If there is a respec request, checking the points spent wont be accurate
			int iTree;

			for(iTree=0;iTree<eaSize(&pRealEnt->pChar->ppPowerTrees);iTree++)
			{
				PowerTree *pTree = pRealEnt->pChar->ppPowerTrees[iTree];
				int iNode;

				for(iNode=0;iNode<eaSize(&pTree->ppNodes);iNode++)
				{
					PTNodeDef *pNodeDef = GET_REF(pTree->ppNodes[iNode]->hDef);

					if(pNodeDef && pNodeDef->ppRanks && !stricmp(pNodeDef->ppRanks[0]->pchCostTable,pchPoints))
					{
						PTNode *PTFakeNode = character_FindPowerTreeNode(pFakeEnt->pChar,pNodeDef,NULL);

						if(!PTFakeNode || PTFakeNode->iRank != pTree->ppNodes[iNode]->iRank)
							return true;
					}
				}
			}
		}

		return iRealSpent != iFakeSpent;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenUpdatePowerCartStatus");
void gclGenExprUpdatePowerCartStatus(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt, SA_PARAM_NN_STR const char* pchPoints )
{
	static U32 s_iTimer = 0;

	if ( pEnt==NULL || pFakeEnt==NULL || s_iPowersStatus == PowerCartStatus_Ready )
		return;

	if ( s_iPowersStatus == PowerCartStatus_PendingSet )
	{
		s_iTimer = timeSecondsSince2000() + 5;
		s_iPowersStatus = PowerCartStatus_Pending;
		return;
	}
	if ( s_iPowersStatus == PowerCartStatus_ChangeComingSet )
	{
		s_iTimer = timeSecondsSince2000() + 2;
		s_iPowersStatus = PowerCartStatus_ChangeComing;
		return;
	}

	if ( s_iPowersStatus == PowerCartStatus_Failed )
	{
		s_iPowersStatus = PowerCartStatus_Ready;
	}
	else if ( s_iPowersStatus == PowerCartStatus_Pending && !gclGenExprPowersChangedOnFakeEnt( pEnt, pFakeEnt, pchPoints ) )
	{
		s_iPowersStatus = PowerCartStatus_Ready;
	}
	else if ( timeSecondsSince2000() > s_iTimer )
	{
		s_iPowersStatus = PowerCartStatus_Ready;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenBuyPowerCartItems");
void gclGenExprBuyPowerCartItems(SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity* pPlayer = entActivePlayerPtr();

	if ( pEnt==NULL )
		return;

	s_iPowersStatus = PowerCartStatus_PendingSet;

	if ( pEnt == pPlayer )
	{
		if(g_bCartRespecRequest)
			ServerCmd_gslBuyPowerCartItemsWithRespec();
		else
			ServerCmd_gslBuyPowerCartItems();
	}
	else
		ServerCmd_gslBuyPowerCartItemsForPet( entGetContainerID( pEnt ) );
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenBuyPowerCartItemsWithRespec");
void gclGenExprBuyPowerCartItemsWithRespec(SA_PARAM_OP_VALID Entity *pEnt)
{
	Entity *pPlayer = entActivePlayerPtr();

	if(pEnt==NULL)
		return;

	s_iPowersStatus = PowerCartStatus_PendingSet;

	if( pEnt == pPlayer)
		ServerCmd_gslBuyPowerCartItemsWithRespec();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerSetPending");
void gclGenPowerSetPending(void)
{
	s_iPowersStatus = PowerCartStatus_ChangeComingSet;
}

//power cart purchase for a puppet of type: pchType
AUTO_COMMAND ACMD_NAME("BuyPowerCartItemsForPuppet") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gclBuyPowerCartItemsForPuppet( const ACMD_SENTENCE pchType )
{
	Entity* pEntity = entActivePlayerPtr();
	Entity* pPupEnt = pEntity ? entity_GetPuppetEntityByType( pEntity, pchType, NULL, false, true ) : NULL;

	if ( pPupEnt )
	{
		if (	entGetType(pPupEnt) == pEntity->pSaved->pPuppetMaster->curType
			&&	entGetContainerID(pPupEnt) == pEntity->pSaved->pPuppetMaster->curID )
		{
			ServerCmd_gslBuyPowerCartItems();
		}
		else
		{
			ServerCmd_gslBuyPowerCartItemsForPet( entGetContainerID( pPupEnt ) );
		}
		gclGenExprClearPowerCart();
	}
}

AUTO_COMMAND ACMD_NAME("BuyPowerCartItemsForPuppetPet") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gclBuyPowerCartItemsForPuppetPet( const char* pchPuppetType, const ACMD_SENTENCE pchPetName )
{
	Entity* pEntity = entActivePlayerPtr();
	Entity* pPupEnt = pEntity ? entity_GetPuppetEntityByType( pEntity, pchPuppetType, NULL, true, true ) : NULL;

	if ( pPupEnt )
	{
		S32 i;

		for ( i = 0; i < eaSize(&pPupEnt->pSaved->ppOwnedContainers); i++ )
		{
			Entity* pPetEnt = GET_REF( pPupEnt->pSaved->ppOwnedContainers[i]->hPetRef );

			if ( pPetEnt && stricmp(entGetLocalName(pPetEnt),pchPetName)==0 )
			{
				ServerCmd_gslBuyPowerCartItemsForPet( entGetContainerID( pPetEnt ) );
				gclGenExprClearPowerCart();
				break;
			}
		}
	}
}

static bool gclGetPowerCartList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCostTable, const char *pchPowerCostTable, bool bWantEnhancements)
{
	PowerCartListNode ***peaCartList =  ui_GenGetManagedListSafe(pGen, PowerCartListNode);
	int i, j, iListIndex = 0, size;
	Entity* pEnt = entActivePlayerPtr();

	size = eaSize(&s_eaCartPowerNodes);

	for (i = 0; i < size; i++)
	{
		PowerCartListNode *pPowerNode;
		PowerListNode *pCartNode;
		PTNodeDef *pPTNode;

		pCartNode = s_eaCartPowerNodes[i];
		pPTNode = pCartNode ? GET_REF(pCartNode->hNodeDef) : NULL;

		if (pchPowerCostTable && *pchPowerCostTable)
		{
			if (pPTNode)
			{
				if (stricmp(pPTNode->ppRanks[pCartNode->iRank - 1]->pchCostTable, pchPowerCostTable) != 0)
				{
					continue;
				}
			}
		}

		pPowerNode = eaGetStruct(peaCartList, parse_PowerCartListNode, iListIndex++);
		pPowerNode->bIsEnhancement = false;
		if (pPowerNode->pPowerListNode)
			StructCopyAll(parse_PowerListNode, pCartNode, pPowerNode->pPowerListNode);
		else
			pPowerNode->pPowerListNode = StructClone(parse_PowerListNode, s_eaCartPowerNodes[i]);

		// If the player wants, we populate the power with it's enhancements
		if (pPTNode && s_eaCartPowerNodes[i]->bShowEnhancements && bWantEnhancements)
		{
			// Populate the rank info
			for (j = 0; j < eaSize(&pPTNode->ppRanks); j++)
			{
				if (!stricmp(pPTNode->ppRanks[j]->pchCostTable, pchCostTable))
				{
					PowerCartListNode *pRankNode = eaGetStruct(peaCartList, parse_PowerCartListNode, iListIndex++);
					PTNodeUpgrade *pUpgrade;
					// For convenience, fill in the power this enhancement is for
					if (pRankNode->pPowerListNode)
						StructCopyAll(parse_PowerListNode, s_eaCartPowerNodes[i], pRankNode->pPowerListNode);
					else
						pRankNode->pPowerListNode = StructClone(parse_PowerListNode, s_eaCartPowerNodes[i]);
					if (!pRankNode->pUpgrade)
						pRankNode->pUpgrade = StructCreate(parse_PTNodeUpgrade);
					pUpgrade = pRankNode->pUpgrade;
					pRankNode->bIsEnhancement = true;
					COPY_HANDLE(pUpgrade->hPowerDef, pPTNode->ppRanks[j]->hPowerDef);
					SET_HANDLE_FROM_REFERENT("PTEnhTypeDef", NULL, pUpgrade->hEnhType);
					SET_HANDLE_FROM_REFERENT("PowerTreeNodeDef", pPTNode, pUpgrade->hNode);
					if (pEnt && pEnt->pChar) {
						pUpgrade->iCost = entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character,pEnt->pChar), pPTNode, j);
					} else {
						pUpgrade->iCost = pPTNode->ppRanks[j]->iCostScaled;
					}
					pUpgrade->pchCostTable = allocAddString(pPTNode->ppRanks[j]->pchCostTable);
					pUpgrade->pRequires = pPTNode->ppRanks[j]->pRequires;
					pUpgrade->bIsRank = true;
					pUpgrade->iRank = j;
				}
			}
			// Populate the enhancement info
			for (j = 0; j < eaSize(&pPTNode->ppEnhancements); j++)
			{
				if (!stricmp(pPTNode->ppEnhancements[j]->pchCostTable, pchCostTable))
				{
					PowerCartListNode *pEnhNode = eaGetStruct(peaCartList, parse_PowerCartListNode, iListIndex++);
					PTNodeUpgrade *pUpgrade;
					// For convenience, fill in the power this enhancement is for
					if (pEnhNode->pPowerListNode)
						StructCopyAll(parse_PowerListNode, s_eaCartPowerNodes[i], pEnhNode->pPowerListNode);
					else
						pEnhNode->pPowerListNode = StructClone(parse_PowerListNode, s_eaCartPowerNodes[i]);
					if (!pEnhNode->pUpgrade)
						pEnhNode->pUpgrade = StructCreate(parse_PTNodeUpgrade) ;
					pUpgrade = pEnhNode->pUpgrade;
					pEnhNode->bIsEnhancement = true;
					COPY_HANDLE(pUpgrade->hPowerDef, pPTNode->ppEnhancements[j]->hPowerDef);
					COPY_HANDLE(pUpgrade->hEnhType, pPTNode->ppEnhancements[j]->hEnhType);
					SET_HANDLE_FROM_REFERENT("PowerTreeNodeDef", pPTNode, pUpgrade->hNode);
					pUpgrade->iCost = pPTNode->ppEnhancements[j]->iCost;
					pUpgrade->pchCostTable = allocAddString(pPTNode->ppEnhancements[j]->pchCostTable);
					pUpgrade->pRequires = NULL;
					pUpgrade->bIsRank = false;
					pUpgrade->iRank = 0;
				}
			}
		}
	}

	while (eaSize(peaCartList) > iListIndex)
		StructDestroy(parse_PowerCartListNode, eaPop(peaCartList));

	ui_GenSetListSafe(pGen, peaCartList, PowerCartListNode);

	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPowerCartList);
bool gclGenExprGetPowerCartList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCostTable)
{
	return gclGetPowerCartList(pGen, pchCostTable, NULL, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetPowerCartCostTableList);
bool gclGenExprGetPowerCartCostTableList(SA_PARAM_NN_VALID UIGen *pGen, const char *pchCostTable)
{
	return gclGetPowerCartList(pGen, NULL, pchCostTable, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsPowerInCart");
bool gclGenExprIsPowerInCart(SA_PARAM_OP_VALID PowerDef* pDef)
{
	if ( pDef )
	{
		int i, iSize = eaSize(&s_eaCartPowerNodes);

		for(i = 0; i < iSize; i++)
		{
			PowerDef *pCurPowerDef = GET_REF(s_eaCartPowerNodes[i]->hPowerDef);

			if (pDef == pCurPowerDef)
				return true;
		}
	}

	return false;
}

bool gclGenExprIsPowerListNodeInCartEx(SA_PARAM_OP_VALID PowerListNode* pNode, S32 iRank)
{
	PowerDef* pNodePowerDef = pNode ? GET_REF( pNode->hPowerDef ) : NULL;

	if ( pNodePowerDef )
	{
		int i, iSize = eaSize( &s_eaCartPowerNodes );

		//make sure the power isn't already in the list
		for( i = 0; i < iSize; i++ )
		{
			PowerDef* pCurPowerDef = GET_REF( s_eaCartPowerNodes[i]->hPowerDef );

			//check that both the defs and the ranks match
			if ( pNodePowerDef == pCurPowerDef && s_eaCartPowerNodes[i]->iRank == iRank )
				return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenIsPowerListNodeInCart");
bool gclGenExprIsPowerListNodeInCart(SA_PARAM_OP_VALID PowerListNode* pNode)
{
	return pNode && gclGenExprIsPowerListNodeInCartEx( pNode, pNode->iRank );
}

int gclPowerCartSortByLevel(const PowerListNode **ppNode1,const PowerListNode **ppNode2)
{
	const PowerListNode *pNode1 = (*ppNode1);
	const PowerListNode *pNode2 = (*ppNode2);

	if(GET_REF(pNode1->hNodeDef) == GET_REF(pNode2->hNodeDef))
		return pNode1->iRank - pNode2->iRank;

	return(pNode1->iLevel - pNode2->iLevel);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddPowerToCartAtRank");
bool gclGenExprAddPowerToCartAtRank(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode* pNode, S32 iRank)
{
	PowerListNode* pNewNode;
	int i;

	if ( gclGenExprIsPowerListNodeInCartEx( pNode, iRank ) )
		return false;

	//make a "clone" of the power node
	pNewNode = StructClone( parse_PowerListNode, pNode );

	if ( pNewNode && iRank > 0 )
		pNewNode->iRank = iRank;

	if (pNewNode && pEnt)
	{
		PowerDef* pDef = GET_REF(pNewNode->hPowerDef);
		if (pDef)
			pNewNode->bShowEnhancements = isPowerOwned(pEnt, pDef);
	}

	if(pNewNode)
	{
		for(i=0;i<=eaSize(&s_eaCartPowerNodes);i++)
		{
			if(i==eaSize(&s_eaCartPowerNodes))
			{
				eaPush( &s_eaCartPowerNodes, pNewNode );
				break;
			}
			else if(GET_REF(s_eaCartPowerNodes[i]->hNodeDef) == GET_REF(pNewNode->hNodeDef)
				&& s_eaCartPowerNodes[i]->iRank > pNewNode->iRank)
			{
				eaInsert(&s_eaCartPowerNodes,pNewNode,i);
				break;
			}
			else if(s_eaCartPowerNodes[i]->iLevel > pNewNode->iLevel)
			{
				eaInsert(&s_eaCartPowerNodes,pNewNode,i);
				break;
			}
		}
	}

	return true;
}

static void gclAddPowerToCartForEnt( Entity* pPlayerEnt, Entity* pEnt, const char* pchNodeFull, S32 iRank )
{
	PTNodeDef* pNodeDef = powertreenodedef_Find(pchNodeFull);

	if ( pNodeDef )
	{
		PowerListNode* pNewNode = StructCreate( parse_PowerListNode );

		FillPowerListNode( pEnt, pNewNode, NULL, powertree_TreeDefFromNodeDef( pNodeDef ),
			NULL, powertree_GroupDefFromNodeDef( pNodeDef ),
			NULL, pNodeDef );

		if ( iRank > 0 )
			pNewNode->iRank = iRank;

		eaPush( &s_eaCartPowerNodes, pNewNode );

		gclGenExprSavePowerCartList( pPlayerEnt );
	}
}

AUTO_COMMAND ACMD_NAME("AddPowerToCart") ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void gclAddPowerToCart( S32 iRank, const ACMD_SENTENCE pchNodeFull )
{
	Entity* pEnt = entActivePlayerPtr();

	if ( pEnt )
	{
		gclAddPowerToCartForEnt( pEnt, pEnt, pchNodeFull, iRank );
	}
}

AUTO_COMMAND ACMD_NAME("AddPowerToCartForPuppet") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gclAddPowerToCartForPuppet( S32 iRank, const char* pchNodeFull, const ACMD_SENTENCE pchPuppetType )
{
	Entity* pEnt = entActivePlayerPtr();
	Entity* pPupEnt = pEnt ? entity_GetPuppetEntityByType( pEnt, pchPuppetType, NULL, true, true ) : NULL;

	if ( pPupEnt )
	{
		gclAddPowerToCartForEnt( pEnt, pPupEnt, pchNodeFull, iRank );
	}
}

AUTO_COMMAND ACMD_NAME("AddPowerToCartForPuppetPet") ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void gclAddPowerToCartForPuppetPet( S32 iRank, const char* pchNodeFull, const char* pchPuppetType,
									const ACMD_SENTENCE pchPetName )
{
	Entity* pEnt = entActivePlayerPtr();
	Entity* pPupEnt = pEnt ? entity_GetPuppetEntityByType( pEnt, pchPuppetType, NULL, true, true ) : NULL;

	if ( pEnt && pPupEnt && pPupEnt->pSaved )
	{
		S32 i;

		for ( i = 0; i < eaSize(&pPupEnt->pSaved->ppOwnedContainers); i++ )
		{
			Entity* pPetEnt = GET_REF( pPupEnt->pSaved->ppOwnedContainers[i]->hPetRef );

			if ( pPetEnt && stricmp(entGetLocalName(pPetEnt),pchPetName)==0 )
			{
				gclAddPowerToCartForEnt( pEnt, pPetEnt, pchNodeFull, iRank );
				break;
			}
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerName");
const char* gclGenExprGenGetPowerName(SA_PARAM_OP_VALID Power* pPower)
{
	const char *result = NULL;

	if(pPower)
	{
		PowerDef *powerDef = GET_REF(pPower->hDef);
		if(powerDef)
		{
			result = powerDef->pchName;
		}
	}

	return result;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerDisplayNameFromID");
const char* gclGenExprGenGetPowerDisplayNameFromID(Entity* pEnt, int id)
{
	Power *power = character_FindPowerByID(pEnt->pChar, id);
	PowerDef *pPowerDef;
	if(power)
	{
		pPowerDef = GET_REF(power->hDef);
		return (pPowerDef ? TranslateDisplayMessage(pPowerDef->msgDisplayName) : " ");
	}
	return NULL;

}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerCartName");
const char* gclGenExprGetPowerCartName(SA_PARAM_OP_VALID PowerCartListNode* pNode)
{
	if (pNode && pNode->pUpgrade)
	{
		PowerDef *pDef = GET_REF(pNode->pUpgrade->hPowerDef);
		return (pDef ? TranslateDisplayMessage(pDef->msgDisplayName) : " ");
	}
	return " ";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenAddPowerToCart");
bool gclGenExprAddPowerToCart(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode* pNode)
{
	return gclGenExprAddPowerToCartAtRank( pEnt, pNode, -1 );
}

S32 gclPowerCartRemoveAllNodesWithDefAtHigherRank(SA_PARAM_OP_VALID PTNodeDef *pNodeDef, S32 iRank)
{
	S32 i, iCount = 0;

	if ( pNodeDef==NULL )
		return 0;

	for (i = eaSize(&s_eaCartPowerNodes) - 1; i >= 0; i--)
	{
		//remove any node that shares the def passed in
		if (GET_REF(s_eaCartPowerNodes[i]->hNodeDef) == pNodeDef && iRank < s_eaCartPowerNodes[i]->iRank )
		{
			StructDestroy( parse_PowerListNode, eaRemove( &s_eaCartPowerNodes, i ) );
			iCount++;
		}
	}

	return iCount;
}

//returns the number of nodes removed
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartRemoveAllNodesWithDef");
S32 gclGenExprPowerCartRemoveAllNodesWithDef(SA_PARAM_OP_VALID PTNodeDef *pNodeDef)
{
	S32 i, iCount = 0;

	if ( pNodeDef==NULL )
		return 0;

	for (i = eaSize(&s_eaCartPowerNodes) - 1; i >= 0; i--)
	{
		//remove any node that shares the def passed in
		if (GET_REF(s_eaCartPowerNodes[i]->hNodeDef) == pNodeDef)
		{
			StructDestroy( parse_PowerListNode, eaRemove( &s_eaCartPowerNodes, i ) );
			iCount++;
		}
	}

	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenRemovePowerFromCart");
bool gclGenExprRemovePowerFromCart(SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	PTNodeDef *pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;

	if ( pNodeDef )
	{
		S32 i, iSize = eaSize(&s_eaCartPowerNodes);

		for (i = 0; i < iSize; i++)
		{
			//only remove a list node with the same def and rank
			if (GET_REF(s_eaCartPowerNodes[i]->hNodeDef) == pNodeDef && pListNode->iRank == s_eaCartPowerNodes[i]->iRank)
			{
				StructDestroy( parse_PowerListNode, eaRemove( &s_eaCartPowerNodes, i ) );
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartIsEmpty");
bool gclGenExprPowerCartIsEmpty(void)
{
	return eaSize(&s_eaCartPowerNodes) <= 0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerCartRank");
S32 gclGenExprGetPowerCartRank(SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	PTNodeDef *pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;

	if ( pNodeDef )
	{
		S32 i, iRank = 0, iSize = eaSize(&s_eaCartPowerNodes);

		//search back to front
		for (i = iSize - 1; i >= 0; --i)
		{
			//the first node we find is going to have the most recent rank value
			if (GET_REF(s_eaCartPowerNodes[i]->hNodeDef) == pNodeDef)
			{
				return s_eaCartPowerNodes[i]->iRank;
			}
		}
	}

	return 0;
}

bool isPowerUnfiltered(SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const char* pchFilterTokens)
{
	char* pchFilterCopy;
	char* pchContext;
	char* pchStart;

	// If no string is passed in, just default to true.
	if (pchFilterTokens
		&& pchFilterTokens[0]
		&& pNodeDef)
	{
		char* pchPowerName;
		int i = 0;

		// Convert power name to lowercase
		strdup_alloca(pchPowerName, TranslateDisplayMessage(pNodeDef->pDisplayMessage));
		while (pchPowerName[i])
		{
			pchPowerName[i] = (char)tolower(pchPowerName[i]);
			i++;
		}

		// For all strings in the filter, check if that string is a substring of the name
		strdup_alloca(pchFilterCopy, pchFilterTokens);

		pchStart = strtok_r(pchFilterCopy, " ,\t\r\n", &pchContext);
		do
		{
			if (pchStart)
			{
				// Convert filter to lowercase
				i = 0;
				while (pchStart[i])
				{
					pchStart[i] = (char)tolower(pchStart[i]);
					i++;
				}

				if (!strstr(pchPowerName, pchStart))
				{
					return false;
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	return true;
}

bool isPowerAttribModUnfiltered(SA_PARAM_NN_VALID PTNodeDef *pNodeDef, const char* pchFilterModList)
{
	char* pchFilterCopy;
	char* pchContext;
	char* pchStart;

	// If no string is passed in, just default to true.
	if (pchFilterModList
		&& pchFilterModList[0]
		&& pNodeDef
		&& eaSize(&pNodeDef->ppRanks))
	{
		int i = 0;
		PowerDef *pDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);

		strdup_alloca(pchFilterCopy, pchFilterModList);

		pchStart = strtok_r(pchFilterCopy, " ,\t\r\n", &pchContext);
		do
		{
			if (pchStart)
			{
				bool bFound = false;
				AttribType eAttribType = StaticDefineIntGetInt(AttribTypeEnum, pchStart);

				for (i = 0; i < eaSize(&pDef->ppOrderedMods); i++)
				{
					AttribType eType = pDef->ppOrderedMods[i]->offAttrib;

					if (eType == eAttribType)
					{
						bFound = true;
					}
				}

				if (!bFound)
				{
					return false;
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
	return true;
}

static bool nodeOrCloneExistsInList(PowerListNode **pPowerListNodes, PTNodeDef *pNodeDef, S32 iListSize)
{
	int i;
	if (pPowerListNodes && pNodeDef)
	{
		for (i = 0; i < iListSize; i++)
		{
			PTNodeDef *pCloneNode = GET_REF(pNodeDef->hNodeClone);
			PTNodeDef *pNodeFromList = GET_REF(pPowerListNodes[i]->hNodeDef);
			PTNodeDef *pCloneNodeFromList = GET_REF(pNodeFromList->hNodeClone);

			if ((pNodeDef && (pNodeDef == pNodeFromList))
				|| (pNodeDef && (pNodeDef == pCloneNodeFromList))
				|| (pCloneNode && (pCloneNode == pNodeFromList))
				|| (pCloneNode && (pCloneNode == pCloneNodeFromList)))
			{
				return true;
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetFilterMask");
int gclGenGetFilterMask(bool flag0, bool flag1, bool flag2, bool flag3, bool flag4, bool flag5, bool flag6, bool flag7, bool flag8, bool flag9)
{
	int bit0 = flag0 ? 1 << 0 : 0;
	int bit1 = flag1 ? 1 << 1 : 0;
	int bit2 = flag2 ? 1 << 2 : 0;
	int bit3 = flag3 ? 1 << 3 : 0;
	int bit4 = flag4 ? 1 << 4 : 0;
	int bit5 = flag5 ? 1 << 5 : 0;
	int bit6 = flag6 ? 1 << 6 : 0;
	int bit7 = flag7 ? 1 << 7 : 0;
	int bit8 = flag8 ? 1 << 8 : 0;
	int bit9 = flag9 ? 1 << 9 : 0;
	return bit0 | bit1 | bit2 | bit3 | bit4 | bit5 | bit6 | bit7 | bit8 | bit9;
}

// Build a list of power purposes
static void buildPowerPurposeList(PowerPurposeListNode ***peaPurposes)
{
	int i;
	for (i = 0; i < g_iNumOfPurposes; i++)
	{
		PowerPurposeListNode *pPurpose = eaGetStruct(peaPurposes, parse_PowerPurposeListNode, i);
		if (pPurpose)
		{
			pPurpose->pchPurposeName = StaticDefineIntRevLookup(PowerPurposeEnum, i);
			pPurpose->iListSize = 0;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetFirstPopulatedListIndex");
S32 GenGetFirstPopulatedListIndex(SA_PARAM_NN_VALID UIGen *pGen)
{
	PowerPurposeListNode ***peaPurposeList = ui_GenGetManagedListSafe(pGen, PowerPurposeListNode);
	S32 i;
	for (i = 0; i < eaSize(peaPurposeList); i++)
	{
		if (eaSize(&(*peaPurposeList)[i]->eaPowerList))
		{
			return i;
		}
	}
	return -1;
}


// Builds a list of all the PTGroups in the trees specific in the string.
// The string should be of the format "Treename1 Treename2 Treename2"
static void buildPTGroupListFromString(PTGroupDef ***peaGroupList, const char* pchTreeGroups)
{
	int i;
	char *pchTreeGroupCopy;
	char *pchContext;
	char *pchStart;

	eaClear(peaGroupList);
	strdup_alloca(pchTreeGroupCopy, pchTreeGroups);
	pchStart = strtok_r(pchTreeGroupCopy, " ,\t\r\n", &pchContext);
	do
	{
		char* c;
		if (!pchStart)
			continue;

		c = strpbrk(pchStart, ".");
		if (c)
		{
			PowerTreeDef *pTreeDef;
			*c = '\0';
			pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchStart);
			// look for the group with the right name and add it.
			if (pTreeDef)
			{
				for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
				{
					if (pTreeDef->ppGroups[i]
					&& !stricmp(pTreeDef->ppGroups[i]->pchGroup, c+1))
					{
						eaPush(peaGroupList, pTreeDef->ppGroups[i]);
						break;
					}
				}
			}
		}
		// otherwise, get the whole tree
		else
		{
			PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchStart);
			if (pTreeDef)
			{
				// Push on the first tree...
				for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
				{
					if (pTreeDef->ppGroups[i])
						eaPush(peaGroupList, pTreeDef->ppGroups[i]);
				}

				// ...as well as any trees it links to.
				for (i = 0; i < eaSize(&pTreeDef->ppLinks); i++)
				{
					PowerTreeLink *pLink = pTreeDef->ppLinks[i];
					PowerTreeDef *pLinkTreeDef = pLink ? GET_REF(pLink->hTree) : NULL;
					if (pLink && (pLink->eType == kPowerTreeRelationship_DependsOn))
					{
						if (pLinkTreeDef)
						{
							for (i = 0; i < eaSize(&pLinkTreeDef->ppGroups); i++)
							{
								if (pLinkTreeDef->ppGroups[i])
									eaPush(peaGroupList, pLinkTreeDef->ppGroups[i]);
							}
						}
					}
				}
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
}

bool gclNodeMatchesAttributeAffectingPower(SA_PARAM_OP_VALID PTNodeDef* pNodeDef,
										   SA_PARAM_OP_VALID PowerDef* pDef)
{
	Entity* pEnt = entActivePlayerPtr();
	if (pEnt && pDef && pNodeDef)
	{
		int i;
		U32* eaAttribs = NULL;
		if (!GetAttributesAffectingPower(pDef, entGetLanguage(pEnt), &eaAttribs))
		{
			for (i = eaSize(&pDef->ppCombos)-1; i >= 0; i--)
			{
				PowerCombo* pCombo = pDef->ppCombos[i];
				PowerDef* pComboDef = GET_REF(pCombo->hPower);
				if (!pComboDef)
				{
					continue;
				}
				GetAttributesAffectingPower(pComboDef, entGetLanguage(pEnt), &eaAttribs);
			}
		}
		for (i = ea32Size(&eaAttribs)-1; i >= 0; i--)
		{
			if ((AttribType)eaAttribs[i] == pNodeDef->eAttrib)
			{
				break;
			}
		}
		ea32Destroy(&eaAttribs);
		if (i >= 0)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("NodeMatchesAttributeAffectingPower");
bool gclGenExprNodeMatchesAttributeAffectingPower(SA_PARAM_OP_VALID PowerListNode* pListNode, const char* pchPowerDef)
{
	PTNodeDef* pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;
	PowerDef* pDef = RefSystem_ReferentFromString("PowerDef", pchPowerDef);
	return gclNodeMatchesAttributeAffectingPower(pNodeDef, pDef);
}

bool gclPowerNodePassesFilter(	Entity* pRealEnt, Entity *pFakeEnt,
								PTGroupDef *pGroupDef, PTNodeDef *pNodeDef,
								PowerDef* pAttribsAffectingPowerDef,
								int iFilterMask, const char* pchTextFilter,
								PowerNodeFilterCallback pCallback, void* pCallbackData )
{
	bool bShowOwned = (iFilterMask & kPowerNodeFilter_ShowOwned) != 0;
	bool bShowAvailable = (iFilterMask & kPowerNodeFilter_ShowAvailable) != 0;
	bool bShowUnavailable = (iFilterMask & kPowerNodeFilter_ShowUnavailable) != 0;
	bool bShowStarred = (iFilterMask & kPowerNodeFilter_ShowStarred) != 0;
	bool bGetPowerNodeRanks = (iFilterMask & kPowerNodeFilter_GetPowerNodeRanks) != 0;
	bool bIgnoreUncategorized = (iFilterMask & kPowerNodeFilter_IgnoreUncategorized) != 0;
	bool bShowAvailableOwned = (iFilterMask & kPowerNodeFilter_ShowAvailableOwned) != 0; //only show available nodes that are ALSO owned
	bool bShowOwnedNonEscrow = (iFilterMask & kPowerNodeFilter_ShowOwnedNonEscrow) != 0; //only show owned nodes that are not in escrow
	bool bShowPotential = (iFilterMask & kPowerNodeFilter_ShowPotential) != 0;

	if ( pNodeDef )
	{
		char* c;
		char* treename;
		PowerTreeDef *pTreeDef;
		PowerTree *pTree = NULL;
		PTNode* pNode = (bGetPowerNodeRanks || bShowOwnedNonEscrow) ? powertree_FindNode(pFakeEnt->pChar, &pTree, pNodeDef->pchNameFull) : NULL;
		S32 iRank = (pNode && !pNode->bEscrow) ? pNode->iRank + 1 : 0;
		bool bEscrow = (pNode) ? pNode->bEscrow : false;
		PTNodeRankDef *pRankDef = pNodeDef ? pNodeDef->ppRanks[iRank?iRank-1:0] : NULL;
		PowerDef *pPowerDef = pRankDef ? GET_REF(pRankDef->hPowerDef) : NULL;
		PowerPurpose ePurpose = kPowerPurpose_Uncategorized;

		if ( pPowerDef==NULL && pNodeDef && iRank > 1 )
			pPowerDef = GET_REF(pNodeDef->ppRanks[0]->hPowerDef);

		strdup_alloca(treename, pGroupDef->pchNameFull);
		c = strpbrk(treename, ".");
		if (c && *c == '.') *c = '\0';

		pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, treename);
		if (!pTree)
			pTree = (PowerTree*)entity_FindPowerTreeHelper(CONTAINER_NOCONST(Entity, pFakeEnt), pTreeDef);

#ifdef PURGE_POWER_TREE_POWER_DEFS
		if(pNodeDef)
		{
			// Make sure the Node has requested PowerDefs
			powertreenodedef_RequestPowerDefs(pNodeDef);
		}
#endif

		if(pPowerDef)
		{
			ePurpose = pPowerDef->ePurpose;
		}
		else if(pNodeDef)
		{
			ePurpose = pNodeDef->ePurpose;
		}

		if (!pTreeDef
			|| !pNodeDef
			|| (pNodeDef->eFlag & kNodeFlag_HideNode)
			|| (!pPowerDef && pRankDef && !pRankDef->bEmpty)
			|| ePurpose < 0
			|| ( bIgnoreUncategorized && ePurpose == kPowerPurpose_Uncategorized )
			|| ePurpose >= g_iNumOfPurposes)
		{
			return false;
		}

		// Add the power to the categories that it's a part of
		if (isPowerUnfiltered(pNodeDef, pchTextFilter))
		{
			// The isOwned test being this complicated baffles me, seems like it should just be pNode...
			bool isOwned = (pPowerDef && isPowerOwned(pFakeEnt, pPowerDef)) || (pNode && pRankDef && pRankDef->bEmpty);
			bool isAvailable = false;
			bool isPotential = false;

			if ( pRealEnt && iRank > 0 )
			{
				NOCONST(PTNode)* pRealNode = !g_bCartRespecRequest ? entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pRealEnt), pNodeDef ) : NULL;
				isAvailable = ( pRealNode==NULL || pRealNode->iRank < iRank-1 || entity_CanBuyPowerTreeNodeHelper( ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pFakeEnt), pGroupDef, pNodeDef, iRank, true, true, false, false) );
			}
			else
			{
				isAvailable = (bEscrow||!isOwned||iRank>0)? entity_CanBuyPowerTreeNodeHelper( ATR_EMPTY_ARGS, PARTITION_CLIENT, NULL, CONTAINER_NOCONST(Entity, pFakeEnt), pGroupDef, pNodeDef, iRank, true, true, false, false) : 0;
			}

			isPotential = character_CanBuyPowerTreeNodeIgnorePointsRank(pFakeEnt->pChar, pTree, pGroupDef, pNodeDef, iRank);

			if (	(bShowOwned && isOwned)
				||	(bShowOwnedNonEscrow && isOwned && !bEscrow)
				||	(bShowAvailable && isAvailable)
				||	(bShowUnavailable && !isAvailable && !isOwned)
				||	(bShowStarred && pPowerDef && gclGenExprIsPowerInCart(pPowerDef))
				||	(bShowAvailableOwned && isAvailable && isOwned)
				||	(bShowPotential && isPotential)
				||	(pAttribsAffectingPowerDef && gclNodeMatchesAttributeAffectingPower(pNodeDef, pAttribsAffectingPowerDef)))
			{
				if ( pCallback )
				{
					PowerNodeFilterCallbackData cbData;
					cbData.pRealEnt = pRealEnt;
					cbData.pFakeEnt = pFakeEnt;
					cbData.pTreeDef = pTreeDef;
					cbData.pGroupDef = pGroupDef;
					cbData.pNodeDef = pNodeDef;
					cbData.pPowerDef = pPowerDef;
					cbData.bIsOwned = isOwned;
					cbData.bIsAvailable = isAvailable;
					cbData.bIsAvailableForFakeEnt = isPotential;
					return pCallback(&cbData,pCallbackData);
				}
				return true;
			}
		}
	}
	return false;
}
static bool gclAddNodeToPurposeList(PowerNodeFilterCallbackData* pData, PowerPurposeListNode** eaPurposes)
{
	PowerPurposeListNode* pPurpose = NULL;

	if(pData->pPowerDef)
	{
		pPurpose = eaGet(&eaPurposes, pData->pPowerDef->ePurpose);
	}
	else if(pData->pNodeDef)
	{
		pPurpose = eaGet(&eaPurposes, pData->pNodeDef->ePurpose);
	}

	if ( pPurpose && !nodeOrCloneExistsInList(pPurpose->eaPowerList, pData->pNodeDef, pPurpose->iListSize) )
	{
		PowerListNode *pListNode = eaGetStruct(&pPurpose->eaPowerList, parse_PowerListNode, pPurpose->iListSize++);
		FillPowerListNodeFromFilterData(pData,pListNode);
		return true;
	}

	return false;
}

static void gclGetPTNodeUICategoriesFromString(ExprContext *pContext,
											   const char* pchCategories,
											   PTNodeUICategory** peaCategories)
{
	static PTNodeUICategory* s_eaCategories = NULL;
	char* pchContext;
	char* pchStart;
	char* pchCategoriesCopy;
	ea32Clear((U32**)&s_eaCategories);
	if (!pchCategories || !pchCategories[0])
	{
		(*peaCategories) = NULL;
		return;
	}
	strdup_alloca(pchCategoriesCopy, pchCategories);
	pchStart = strtok_r(pchCategoriesCopy, " ,\t\r\n", &pchContext);
	do
	{
		if (pchStart)
		{
			PTNodeUICategory eCategory = StaticDefineIntGetInt(PTNodeUICategoryEnum,pchStart);
			if (eCategory != -1)
			{
				ea32Push((U32**)&s_eaCategories, eCategory);
			}
			else
			{
				const char* pchBlameFile = exprContextGetBlameFile(pContext);
				ErrorFilenamef(pchBlameFile, "PTNodeUICategory %s not recognized", pchStart);
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	(*peaCategories) = s_eaCategories;
}

typedef int (*PowerListNodeSortFunc)(const PowerListNode **ppNodeA, const PowerListNode **ppNodeB);

static void gclFillCategorizedPowerListsForEnt_Internal(ExprContext *pContext,
														SA_PARAM_NN_VALID UIGen *pGen,
														SA_PARAM_OP_VALID Entity *pRealEnt,
														SA_PARAM_OP_VALID Entity *pFakeEnt,
														const char* pchTreeGroups,
														int iFilterMask,
														const char* pchTextFilter,
														const char* pchUICategories,
														PowerListNodeSortFunc pSortFunc,
														bool bForceReload)
{
	PowerPurposeListNode ***peaPurposes = ui_GenGetManagedListSafe(pGen, PowerPurposeListNode);
	if (bForceReload || s_bPowerDefsUpdated || eaSize(peaPurposes) == 0)
	{
		static PTGroupDef **s_eaGroupList = NULL;
		PTNodeUICategory* eaUICategories = NULL;
		S32 i, j;
		s_bPowerDefsUpdated = false;

		if (pRealEnt==NULL && pFakeEnt==NULL)
		{
			return;
		}
		if (pchUICategories && pchUICategories[0])
		{
			gclGetPTNodeUICategoriesFromString(pContext, pchUICategories, &eaUICategories);
		}
		buildPowerPurposeList(peaPurposes);
		buildPTGroupListFromString(&s_eaGroupList, pchTreeGroups);

		// if only a real ent is specified, treat it as the fake ent and NULL out the real ent
		if (!pFakeEnt && pRealEnt)
		{
			pFakeEnt = pRealEnt;
			pRealEnt = NULL;
		}

		for (i = 0; i < eaSize(&s_eaGroupList); i++)
		{
			PTGroupDef *pGroupDef = s_eaGroupList[i];
			if (pGroupDef)
			{
				for (j = 0; j < eaSize(&pGroupDef->ppNodes); j++)
				{
					PTNodeDef* pNodeDef = pGroupDef->ppNodes[j];

					if (pchUICategories
						&& ea32Find((U32**)&eaUICategories, pNodeDef->eUICategory)<0)
					{
						continue;
					}
					gclPowerNodePassesFilter(pRealEnt, pFakeEnt, pGroupDef, pNodeDef,
											 NULL, iFilterMask, pchTextFilter,
											 gclAddNodeToPurposeList, (*peaPurposes));
				}
			}
		}

		// Clean up
		for (i = 0; i < eaSize(peaPurposes); i++)
		{
			PowerPurposeListNode *pPurpose = (*peaPurposes)[i];

			// Removes excess nodes from this category's list
			while (eaSize(&pPurpose->eaPowerList) > pPurpose->iListSize)
			{
				StructDestroy(parse_PowerListNode, eaPop(&pPurpose->eaPowerList));
			}

			if (pSortFunc)
			{
				eaQSort(pPurpose->eaPowerList, pSortFunc);
			}
		}
	}
	ui_GenSetListSafe(pGen, peaPurposes, PowerPurposeListNode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenFillPowerListForEntByCategory");
void gclGenFillPowerListForEntByCategory(ExprContext *pContext,
										 SA_PARAM_NN_VALID UIGen *pGen,
										 SA_PARAM_OP_VALID Entity *pEnt,
										 const char* pchTreeGroups,
										 int iFilterMask,
										 const char* pchTextFilter,
										 const char* pchUICategories,
										 bool bForceReload)
{
	gclFillCategorizedPowerListsForEnt_Internal(pContext,
		pGen, pEnt, NULL, pchTreeGroups, iFilterMask, pchTextFilter,
		pchUICategories, SortPowerListNodesByUICategory, bForceReload);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenFillCategorizedPowerListsForEnt");
void gclGenFillCategorizedPowerListsForEnt( ExprContext *pContext,
											SA_PARAM_NN_VALID UIGen *pGen,
											SA_PARAM_OP_VALID Entity *pRealEnt,
											SA_PARAM_OP_VALID Entity *pFakeEnt,
											const char* pchTreeGroups,
											int iFilterMask,
											const char* pchTextFilter,
											bool bForceReload)
{
	gclFillCategorizedPowerListsForEnt_Internal(pContext,
		pGen, pRealEnt, pFakeEnt, pchTreeGroups, iFilterMask, pchTextFilter,
		NULL, SortPowerListNodesByLevel, bForceReload);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenFillCategorizedPowerLists");
void gclGenFillCategorizedPowerLists(ExprContext *pContext,
									 SA_PARAM_NN_VALID UIGen *pGen,
									 SA_PARAM_OP_VALID Entity *pEnt,
									 const char* pchTreeGroups,
									 int iFilterMask,
									 const char* pchTextFilter,
									 bool bForceReload)
{
	gclFillCategorizedPowerListsForEnt_Internal(pContext,
		pGen, pEnt, NULL, pchTreeGroups, iFilterMask, pchTextFilter,
		NULL, SortPowerListNodesByLevel, bForceReload);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetCategorizedPowerList");
void gclGenGetCategorizedPowerList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID PowerPurposeListNode *pPurposeListNode, const char* pchPurpose)
{
	// Get category index
	S32 iCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pchPurpose);
	ui_GenSetManagedListSafe(pGen, &pPurposeListNode->eaPowerList, PowerListNode, false);
}

typedef enum GetPowerListNodeFlags {
	GPLN_REQUIRE_CATEGORY = 1,
	GPLN_OWNED_NODES = 2,
	GPLN_UNOWNED_NODES = 4,
	GPLN_ACTIVATABLE = 8,
	GPLN_UNACTIVATABLE = 16,
	GPLN_SHOW_HIDDEN = 32,
	GPLN_COSTTABLE_NULL_NODES = 64,
	GPLN_AUTOBUY = 128,
	GPLN_BUYABLE_TREES = 256,
	GPLN_UNBUYABLE_TREES = 512,
	GPLN_BUYABLE_GROUPS = 1024,
	GPLN_UNBUYABLE_GROUPS = 2048,
	GPLN_SHOW_EMPTY = 4096,
} GetPowerListNodeFlags;

void gclGetPowerNodeList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt,
						 SA_PARAM_OP_VALID Entity *pFakeEnt, const char *pchTrees, PTNodeDef* pExcludeNode,
						 S32 iCategory, GetPowerListNodeFlags eFlags)
{
	static const char **s_eaCostTables = NULL;
	PowerListNode ***peaHolder = ui_GenGetManagedListSafe(pGen, PowerListNode);
	Entity *pCheckEnt = FIRST_IF_SET(pFakeEnt, pEnt);
	S32 i, j, k, iCount = 0;

	eaClearFast(&s_eaCostTables);
	if (pEnt && ((eFlags & GPLN_REQUIRE_CATEGORY) == 0 || iCategory >= 0)

		// At least one of the flag pairs needs to be set
		// for this to generate something.
		&& (eFlags & (GPLN_BUYABLE_TREES | GPLN_UNBUYABLE_TREES)) != 0
		&& (eFlags & (GPLN_BUYABLE_GROUPS | GPLN_UNBUYABLE_GROUPS)) != 0
		&& (eFlags & (GPLN_OWNED_NODES | GPLN_UNOWNED_NODES)) != 0
		&& (eFlags & (GPLN_ACTIVATABLE | GPLN_UNACTIVATABLE)) != 0
		)
	{
		char *pchTreeCopy = NULL, *pchContext = NULL, *pchStart = NULL;
		strdup_alloca(pchTreeCopy, pchTrees);
		pchStart = strtok_r(pchTreeCopy, " ,\t\r\n", &pchContext);
		do
		{
			PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchStart);
			bool bCanBuyTree = true;

			if (!pTreeDef)
				continue;

			if (!(eFlags & GPLN_BUYABLE_TREES) || !(eFlags & GPLN_UNBUYABLE_TREES))
			{
				bCanBuyTree = entity_CanBuyPowerTreeHelper(
						PARTITION_CLIENT,
						CONTAINER_NOCONST(Entity, pCheckEnt),
						pTreeDef, pTreeDef->bTemporary
					);
			}

			if (bCanBuyTree ? !(eFlags & GPLN_BUYABLE_TREES) : !(eFlags & GPLN_UNBUYABLE_TREES))
			{
				continue;
			}

			for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
			{
				PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
				bool bCanBuyGroup = true;

				if (!(eFlags & GPLN_BUYABLE_GROUPS) || !(eFlags & GPLN_UNBUYABLE_GROUPS))
				{
					bCanBuyGroup = entity_CanBuyPowerTreeGroupHelper(
							ATR_EMPTY_ARGS,
							PARTITION_CLIENT,
							NULL,
							CONTAINER_NOCONST(Entity, pCheckEnt),
							pGroupDef
						);
				}

				if (bCanBuyGroup ? !(eFlags & GPLN_BUYABLE_GROUPS) : !(eFlags & GPLN_UNBUYABLE_GROUPS))
				{
					continue;
				}

				for (j = 0; j < eaSize(&pGroupDef->ppNodes); j++)
				{
					PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
					PTNodeRankDef *pRankDef = pNodeDef ? eaGet(&pNodeDef->ppRanks, 0) : NULL;
					PowerDef *pPowerDef = pRankDef ? GET_REF(pRankDef->hPowerDef) : NULL;
					NOCONST(PTNode) *pNode = NULL;
					bool bAutoBuyOwned = false;
					bool bActivatable = false;
					PowerListNode *pListNode = NULL;

					if (pNodeDef == pExcludeNode)
					{
						continue;
					}

					if (!(eFlags & GPLN_SHOW_HIDDEN) && (pNodeDef->eFlag & kNodeFlag_HideNode))
					{
						continue;
					}

					if (!(eFlags & GPLN_SHOW_EMPTY) && (!pRankDef || pRankDef->bEmpty))
					{
						continue;
					}

					if ((eFlags & GPLN_REQUIRE_CATEGORY) && (!pPowerDef || eaiFind(&pPowerDef->piCategories, iCategory) < 0))
					{
						continue;
					}

					if (!(eFlags & GPLN_OWNED_NODES) || !(eFlags & GPLN_UNOWNED_NODES))
					{
						pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pCheckEnt)->pChar, NULL, pNodeDef->pchNameFull);
						if (eFlags & GPLN_AUTOBUY)
							bAutoBuyOwned = (pNodeDef->eFlag & kNodeFlag_AutoBuy) != 0;
					}

					if (pNode || bAutoBuyOwned ? !(eFlags & GPLN_OWNED_NODES) : !(eFlags & GPLN_UNOWNED_NODES))
					{
						continue;
					}

					if (!(eFlags & GPLN_ACTIVATABLE) || !(eFlags & GPLN_UNACTIVATABLE))
					{
						bActivatable = pPowerDef && power_DefDoesActivate(pPowerDef);
					}

					if (bActivatable ? !(eFlags & GPLN_ACTIVATABLE) : !(eFlags & GPLN_UNACTIVATABLE))
					{
						continue;
					}

					// Maintain stable pointers for power nodes
					for (k = iCount; k < eaSize(peaHolder); k++)
					{
						if (GET_REF((*peaHolder)[k]->hNodeDef) == pNodeDef)
						{
							eaMove(peaHolder, iCount, k);
							pListNode = (*peaHolder)[iCount++];
							break;
						}
					}
					if (!pListNode)
					{
						pListNode = StructCreate(parse_PowerListNode);
						eaInsert(peaHolder, pListNode, iCount++);
					}

					if (pFakeEnt)
						FillPowerListNodeForEnt(pEnt, pFakeEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
					else
						FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, CONTAINER_RECONST(PTNode, pNode), pNodeDef);

					if (pListNode->iRank < pListNode->iMaxRank)
					{
						if (pEnt)
						{
							pListNode->bIsAvailable = entity_CanBuyPowerTreeNodeHelper(
									ATR_EMPTY_ARGS,
									PARTITION_CLIENT,
									NULL,
									CONTAINER_NOCONST(Entity, pEnt),
									pGroupDef,
									pNodeDef,
									pListNode->iRank,
									true,
									true,
									false,
									pListNode->bIsTraining
								);
						}

						if (pFakeEnt)
						{
							pListNode->bIsAvailableForFakeEnt = entity_CanBuyPowerTreeNodeHelper(
									ATR_EMPTY_ARGS,
									PARTITION_CLIENT,
									NULL,
									CONTAINER_NOCONST(Entity, pFakeEnt),
									pGroupDef,
									pNodeDef,
									pListNode->iRank,
									true,
									true,
									false,
									pListNode->bIsTraining
								);
						}
					}
					else
					{
						pListNode->bIsAvailable = false;
						pListNode->bIsAvailableForFakeEnt = false;
					}

					if (pRankDef && pRankDef->pchCostTable && eaFindString(&s_eaCostTables, pRankDef->pchCostTable) < 0)
					{
						eaPush(&s_eaCostTables, pRankDef->pchCostTable);
					}
				}
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}

	if (eFlags & GPLN_COSTTABLE_NULL_NODES)
	{
		S32 iEmpty = 0;

		// Count maximum possible unused points
		for (i = 0; i < eaSize(&s_eaCostTables); i++)
		{
			S32 iSpentPoints = entity_PointsSpent(CONTAINER_NOCONST(Entity, pEnt), s_eaCostTables[i]);
			S32 iMaxPoints;
			if (powertable_Find(s_eaCostTables[i]))
				// Get the maximum possible points a player can earn (assume player can't go beyond level 100k)
				iMaxPoints = entity_PowerTableLookupAtHelper(CONTAINER_NOCONST(Entity, pEnt), s_eaCostTables[i], 100000);
			else
				iMaxPoints = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), s_eaCostTables[i]);
			iEmpty += MAX(iMaxPoints - iSpentPoints, 0);
		}

		// Add empty nodes for empty slots
		while (iEmpty-- > 0)
		{
			PowerListNode *pListNode = eaGetStruct(peaHolder, parse_PowerListNode, iCount++);
			FillPowerListNode(pEnt, pListNode, NULL, NULL, NULL, NULL, NULL, NULL);
			pListNode->bIsAvailable = false;
			pListNode->bIsAvailableForFakeEnt = false;
		}
	}

	eaSetSizeStruct(peaHolder, parse_PowerListNode, iCount);
	if (iCount > 1)
		eaQSort(*peaHolder, SortPowerListNodesByLevel);
	ui_GenSetManagedListSafe(pGen, peaHolder, PowerListNode, true);
}

// Given a category and a list of power trees, return a list of all the nodes in that
// category across all trees.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerNodeListFromCategory");
void gclGenExprGetPowerNodeListFromCategory(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTrees, const char *pchCategory)
{
	S32 iCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pchCategory);
	gclGetPowerNodeList(pContext, pGen, pEnt, NULL, pchTrees, NULL, iCategory,
		GPLN_REQUIRE_CATEGORY
		| GPLN_OWNED_NODES | GPLN_UNOWNED_NODES
		| GPLN_ACTIVATABLE | GPLN_UNACTIVATABLE
		| GPLN_BUYABLE_TREES | GPLN_UNBUYABLE_TREES
		| GPLN_BUYABLE_GROUPS | GPLN_UNBUYABLE_GROUPS
	);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerNodeList");
void gclGenExprGetPowerNodeList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTrees, const char *pchExcludeNode, bool bOwnedNodesOnly)
{
	PTNodeDef* pExcludeNode = powertreenodedef_Find(pchExcludeNode);
	gclGetPowerNodeList(pContext, pGen, pEnt, NULL, pchTrees, pExcludeNode, -1,
		GPLN_OWNED_NODES
		| (bOwnedNodesOnly ? 0 : GPLN_UNOWNED_NODES)
		| GPLN_ACTIVATABLE | GPLN_UNACTIVATABLE
		| GPLN_BUYABLE_TREES | GPLN_UNBUYABLE_TREES
		| GPLN_BUYABLE_GROUPS | GPLN_UNBUYABLE_GROUPS
	);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerNodeListWithFlagsEx");
void gclGenExprGetPowerNodeListWithFlagsEx(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, const char *pchTrees, const char *pchCategory, const char *pchFlags)
{
	S32 iCategory = StaticDefineIntGetInt(PowerCategoriesEnum, pchCategory);
	S32 iFlags = pchCategory && *pchCategory ? GPLN_REQUIRE_CATEGORY : 0;
	char *pcBuffer, *pcContext, *pcToken;

	iFlags |= GPLN_OWNED_NODES | GPLN_UNOWNED_NODES
		| GPLN_ACTIVATABLE | GPLN_UNACTIVATABLE
		| GPLN_BUYABLE_TREES | GPLN_UNBUYABLE_TREES
		| GPLN_BUYABLE_GROUPS | GPLN_UNBUYABLE_GROUPS;

	strdup_alloca(pcBuffer, pchFlags);
	if ((pcToken = strtok_r(pcBuffer, " \r\n\t,%", &pcContext)) != NULL)
	{
		do
		{
			U32 uAddFlag = 0;
			U32 uRemoveFlag = 0;

			if (stricmp(pcToken, "+Owned") == 0 || stricmp(pcToken, "Owned") == 0)
				uAddFlag = GPLN_OWNED_NODES;
			if (stricmp(pcToken, "-Owned") == 0 || stricmp(pcToken, "Unowned") == 0)
				uRemoveFlag = GPLN_OWNED_NODES;
			if (stricmp(pcToken, "+Unowned") == 0 || stricmp(pcToken, "Unowned") == 0)
				uAddFlag = GPLN_UNOWNED_NODES;
			if (stricmp(pcToken, "-Unowned") == 0 || stricmp(pcToken, "Owned") == 0)
				uRemoveFlag = GPLN_UNOWNED_NODES;

			if (stricmp(pcToken, "+Activatable") == 0 || stricmp(pcToken, "Activatable") == 0)
				uAddFlag = GPLN_ACTIVATABLE;
			if (stricmp(pcToken, "-Activatable") == 0 || stricmp(pcToken, "Unactivatable") == 0)
				uRemoveFlag = GPLN_ACTIVATABLE;
			if (stricmp(pcToken, "+Unactivatable") == 0 || stricmp(pcToken, "Unactivatable") == 0)
				uAddFlag = GPLN_UNACTIVATABLE;
			if (stricmp(pcToken, "-Unactivatable") == 0 || stricmp(pcToken, "Activatable") == 0)
				uRemoveFlag = GPLN_UNACTIVATABLE;

			if (stricmp(pcToken, "+BuyableGroups") == 0 || stricmp(pcToken, "BuyableGroups") == 0)
				uAddFlag = GPLN_BUYABLE_GROUPS;
			if (stricmp(pcToken, "-BuyableGroups") == 0 || stricmp(pcToken, "UnbuyableGroups") == 0)
				uRemoveFlag = GPLN_BUYABLE_GROUPS;
			if (stricmp(pcToken, "+UnbuyableGroups") == 0 || stricmp(pcToken, "UnbuyableGroups") == 0)
				uAddFlag = GPLN_UNBUYABLE_GROUPS;
			if (stricmp(pcToken, "-UnbuyableGroups") == 0 || stricmp(pcToken, "BuyableGroups") == 0)
				uRemoveFlag = GPLN_UNBUYABLE_GROUPS;

			if (stricmp(pcToken, "+BuyableTrees") == 0 || stricmp(pcToken, "BuyableTrees") == 0)
				uAddFlag = GPLN_BUYABLE_TREES;
			if (stricmp(pcToken, "-BuyableTrees") == 0 || stricmp(pcToken, "UnbuyableTrees") == 0)
				uRemoveFlag = GPLN_BUYABLE_TREES;
			if (stricmp(pcToken, "+UnbuyableTrees") == 0 || stricmp(pcToken, "UnbuyableTrees") == 0)
				uAddFlag = GPLN_UNBUYABLE_TREES;
			if (stricmp(pcToken, "-UnbuyableTrees") == 0 || stricmp(pcToken, "BuyableTrees") == 0)
				uRemoveFlag = GPLN_UNBUYABLE_TREES;

			if (stricmp(pcToken, "+Hidden") == 0 || stricmp(pcToken, "Hidden") == 0)
				uAddFlag = GPLN_SHOW_HIDDEN;
			if (stricmp(pcToken, "-Hidden") == 0)
				uRemoveFlag = GPLN_SHOW_HIDDEN;

			if (stricmp(pcToken, "+Empty") == 0 || stricmp(pcToken, "Empty") == 0)
				uAddFlag = GPLN_SHOW_EMPTY;
			if (stricmp(pcToken, "-Empty") == 0)
				uRemoveFlag = GPLN_SHOW_EMPTY;

			if (stricmp(pcToken, "+Null") == 0 || stricmp(pcToken, "Null") == 0)
				uAddFlag = GPLN_COSTTABLE_NULL_NODES;
			if (stricmp(pcToken, "-Null") == 0)
				uRemoveFlag = GPLN_COSTTABLE_NULL_NODES;

			if (stricmp(pcToken, "+AutoBuy") == 0 || stricmp(pcToken, "AutoBuy") == 0)
				uAddFlag = GPLN_AUTOBUY;
			if (stricmp(pcToken, "-AutoBuy") == 0)
				uRemoveFlag = GPLN_AUTOBUY;

			if (stricmp(pcToken, "+Category") == 0 || stricmp(pcToken, "Category") == 0)
				uAddFlag = GPLN_REQUIRE_CATEGORY;
			if (stricmp(pcToken, "-Category") == 0)
				uRemoveFlag = GPLN_REQUIRE_CATEGORY;

			iFlags = (iFlags & ~uRemoveFlag) | uAddFlag;
		} while ((pcToken = strtok_r(NULL, " \r\n\t,%", &pcContext)) != NULL);
	}

	gclGetPowerNodeList(pContext, pGen, pEnt, pFakeEnt, pchTrees, NULL, iCategory, iFlags);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerNodeListWithFlags");
void gclGenExprGetPowerNodeListWithFlags(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchTrees, const char *pchCategory, const char *pchFlags)
{
	gclGenExprGetPowerNodeListWithFlagsEx(pContext, pGen, pEnt, NULL, pchTrees, pchCategory, pchFlags);
}

static void gclGetArrayOfCategories(ExprContext *pContext, const char* pchCategories, S32 **eaiOutCategories)
{
	char *pchCopy;
	char *pchContext;
	char *pchStart;
	strdup_alloca(pchCopy, pchCategories);
	pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
	if (pchStart)
	{
		do
		{
			S32 catIndex = StaticDefineIntGetInt(PowerCategoriesEnum, pchStart);
			if (catIndex != -1)
			{
				eaiPush(eaiOutCategories, catIndex);
			}
			else
			{
				ErrorFilenamef(exprContextGetBlameFile(pContext), "Power Category %s not recognized", pchStart);
			}
		} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	}
}

// Return a list of powers, given a set of trees, a group within those trees,
// and categories to include or exclude
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowersFromGroupWithCategories");
void gclGenExprGetPowersFromGroupWithCategories(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, const char *pchTrees, const char* pchGroup, const char* pchIncludeCats, const char* pchExcludeCats)
{
	PowerListNode ***peaHolder = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iCount = 0;
	S32 i, j, k;
	char *pchCopy;
	char *pchContext;
	char *pchStart;
	static S32 *s_eaiIncludeCats = NULL;
	static S32 *s_eaiExcludeCats = NULL;

	eaiClear(&s_eaiIncludeCats);
	eaiClear(&s_eaiExcludeCats);

	gclGetArrayOfCategories(pContext, pchIncludeCats, &s_eaiIncludeCats);
	gclGetArrayOfCategories(pContext, pchExcludeCats, &s_eaiExcludeCats);

	strdup_alloca(pchCopy, pchTrees);
	pchStart = strtok_r(pchCopy, " ,\t\r\n", &pchContext);
	do
	{
		PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchStart);
		if (pTreeDef)
		{
			for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
			{
				if (stricmp(pTreeDef->ppGroups[i]->pchGroup, pchGroup) == 0)
				{
					PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
					for (j = 0; j < eaSize(&pGroupDef->ppNodes); j++)
					{
						PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
						PowerDef *pPowerDef = (pNodeDef && eaSize(&pNodeDef->ppRanks)) ? GET_REF(pNodeDef->ppRanks[0]->hPowerDef) : NULL;

						bool bMeetsReqs = true;
						if (pPowerDef)
						{
							for (k = 0; k < eaiSize(&s_eaiIncludeCats); k++)
							{
								if (eaiFind(&pPowerDef->piCategories, s_eaiIncludeCats[k]) == -1)
								{
									bMeetsReqs = false;
									break;
								}
							}
							for (k = 0; k < eaiSize(&s_eaiExcludeCats); k++)
							{
								if (eaiFind(&pPowerDef->piCategories, s_eaiExcludeCats[k]) != -1)
								{
									bMeetsReqs = false;
									break;
								}
							}

							if (bMeetsReqs && !(pNodeDef->eFlag & kNodeFlag_HideNode))
							{
								PowerListNode *pListNode = eaGetStruct(peaHolder, parse_PowerListNode, iCount++);
								FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
							}
						}
					}
				}
			}
		}
	} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
	while (eaSize(peaHolder) > iCount)
	{
		StructDestroy(parse_PowerListNode, eaPop(peaHolder));
	}

	eaQSort(*peaHolder, SortPowerListNodesByLevel);
	ui_GenSetManagedListSafe(pGen, peaHolder, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListOwned");
void gclGenExprGetPowerListOwned(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt)
{
	PowerListNode ***peaNodes;
	gclGenGetMyPowerList(pEnt, pGen, false, false);
	peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	eaQSort(*peaNodes, SortPowerListNodesByLevel);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListAuto");
void gclGenExprGetPowerListAuto(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt)
{
	PowerListNode ***peaNodes;
	gclGenGetMyPowerList(pEnt, pGen, false, true);
	peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	eaQSort(*peaNodes, SortPowerListNodesByLevel);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListForCC");
void gclGenExprGetPowerListForCC(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt)
{
	PowerListNode ***peaNodes;
	gclGenGetMyPowerList(pEnt, pGen, false, false);
	peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	eaQSort(*peaNodes, SortPowerListNodesForCC);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetNumOwnedPowers");
S32 gclGenExprGetNumOwnedPowers(SA_PARAM_NN_VALID Entity *pEnt)
{
	S32 i, j;
	S32 iSum = 0;
	for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
	{
		PowerTree *pTree = pEnt->pChar->ppPowerTrees[i];
		for (j = 0; j < eaSize(&pTree->ppNodes); j++)
		{
			PTNode *pNode = pTree->ppNodes[j];
			PTNodeDef *pNodeDef = GET_REF(pNode->hDef);
			if (pNodeDef && !(pNodeDef->eFlag & kNodeFlag_HideNode))
			{
				iSum++;
			}
		}
	}
	return iSum;
}

// Get all nodes in the tree that are not owned.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerListUnowned");
void gclGenExprGetPowerListUnowned(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID Entity *pEnt, const char *pchTree)
{
	PowerTreeDef *pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchTree);
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iLength = 0;
	S32 i;
	S32 j;
	if (pTreeDef)
	{
		PowerListNode *pListNode;
		for (i = 0; i < eaSize(&pTreeDef->ppGroups); i++)
		{
			PTGroupDef *pGroupDef = pTreeDef->ppGroups[i];
			for (j = 0; j < eaSize(&pGroupDef->ppNodes); j++)
			{
				PTNodeDef *pNodeDef = pGroupDef->ppNodes[j];
				if (pNodeDef && !(pNodeDef->eFlag & kNodeFlag_HideNode))
				{
					NOCONST(PTNode) *pNode = entity_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Entity, pEnt),pNodeDef);
					if(!pNode)
					{
						pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iLength++);
						FillPowerListNode(pEnt, pListNode, NULL, pTreeDef, NULL, pGroupDef, NULL, pNodeDef);
					}
				}
			}
		}
	}
	while (eaSize(peaNodes) > iLength)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	eaQSort(*peaNodes, SortPowerListNodesByLevel);
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerListNodeCanBuy");
bool gclGenExprPowerListNodeCanBuy(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	bool bResult = false;

	if (pEnt && pEnt->pChar && pListNode)
	{
		PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
		PTGroupDef *pGroup = GET_REF(pListNode->hGroupDef);
		PowerTreeDef *pTree = GET_REF(pListNode->hTreeDef);
		if (pNode && pGroup && pTree)
		{
			bResult = ((pListNode->pTree || character_CanBuyPowerTree(PARTITION_CLIENT, pEnt->pChar, pTree))
				&& character_CanBuyPowerTreeNode(PARTITION_CLIENT, pEnt->pChar, pGroup, pNode, pListNode->iRank));
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntFindPowerNodeByName");
bool gclGenExprEntFindPowerListNodeByName(SA_PARAM_OP_VALID Entity *pEnt, const char* pchNode)
{
	if ( pEnt && pchNode && pchNode[0] )
	{
		return powertree_FindNode( pEnt->pChar, NULL, pchNode ) != NULL;
	}
	return false;
}

static S32 entity_PowerTreeNodeCanDecreaseRank(Entity *pEnt, const char *pchTree, const char *pchNode)
{
	PowerTreeDef *pTreeDef = powertreedef_Find(pchTree);
	PTNodeDef *pNodeDef = powertreenodedef_Find(pchNode);
	NOCONST(PowerTree) *pTree = NULL;
	NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(CONTAINER_NOCONST(Character, pEnt->pChar), &pTree, pchNode);

	// Only work if we passed in the proper tree name?
	return (pNode && !pNode->bEscrow && pTree && GET_REF(pTree->hDef) && !stricmp(GET_REF(pTree->hDef)->pchName, pchTree));
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerListNodeCanDecreaseRank");
bool gclGenExprPowerListNodeCanDecreaseRank(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	if (pEnt && pEnt->pChar && pListNode)
	{
		PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
		PowerTreeDef *pTree = GET_REF(pListNode->hTreeDef);
		if (pNode && pTree)
		{
			return entity_PowerTreeNodeCanDecreaseRank(pEnt,pTree->pchName,pNode->pchNameFull);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerListNodeCanBuyIgnorePointsRank");
bool gclGenExprPowerListNodeCanBuyIgnorePointsRank(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	bool bResult = false;

	if (pEnt && pEnt->pChar && pListNode)
	{
		PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
		PTGroupDef *pGroup = GET_REF(pListNode->hGroupDef);
		PowerTreeDef *pTree = GET_REF(pListNode->hTreeDef);
		if (pNode && pGroup && pTree)
		{
			bResult = ((pListNode->pTree || character_CanBuyPowerTree(PARTITION_CLIENT, pEnt->pChar, pTree))
				&& (pListNode->pNode || character_CanBuyPowerTreeNodeIgnorePointsRank(pEnt->pChar, pListNode->pTree, pGroup, pNode, pListNode->iRank)));
		}
	}
	return bResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenShowEnhancements");
void gclGenExprShowEnhancements(PowerCartListNode *pNode, bool bShowEnhancements)
{
	if (pNode && pNode->pPowerListNode)
	{
		pNode->pPowerListNode->bShowEnhancements = bShowEnhancements;
	}
}



//this doesn't mean the internal rank.
//"actual" means the UI-ified rank on the entity rather than on the list node, which may be out of date
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetActualPowerNodeRank");
S32 gclGenExprGetActualPowerNodeRank(	SA_PARAM_OP_VALID Entity *pEnt,
										SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	if ( pEnt )
	{
		PTNodeDef *pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;
		NOCONST(PTNode)* pNode = pNodeDef ? entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pEnt), pNodeDef ) : NULL;
		S32 iRank = (pNode && !pNode->bEscrow) ? pNode->iRank+1 : 0;
		return iRank;
	}

	return 0;
}

//this doesn't mean the internal rank.
//"actual" means the UI-ified rank on the entity rather than on the list node, which may be out of date
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetActualPowerNodeAddedRanks");
S32 gclGenExprGetActualPowerNodeAddedRanks(	SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt,
										   SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	if ( pEnt && pFakeEnt && pListNode )
	{
		PTNodeDef *pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;

		NOCONST(PTNode)* pFakeNode = pNodeDef ? entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pFakeEnt), pNodeDef ) : NULL;
		NOCONST(PTNode)* pRealNode = pNodeDef && !g_bCartRespecRequest ? entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pEnt), pNodeDef ) : NULL;

		S32 iFakeRank = (pFakeNode && !pFakeNode->bEscrow) ? pFakeNode->iRank+1 : 0;
		S32 iRealRank = (pRealNode && !pRealNode->bEscrow) ? pRealNode->iRank+1 : 0;

		return iFakeRank - iRealRank;
	}

	return 0;
}

//takes a fake entity, copies the entity, decreases the specified power tree node, and then validates the tree.
//if the tree is now invalid, copy over the fake entity as it was before decreasing the node
static bool gclGenPowerTreeDecreaseNodeAndValidate( UIGen* pGen, Entity* pEnt, Entity* pFakeEnt, PowerTreeDef* pTreeDef, PTNodeDef* pNodeDef, S32 iDesiredRank, const char* pchNoCopyNumeric)
{
	S32 i;
	bool bSuccess = false;
	PowerTreeSteps* pSteps;
	PowerTreeValidateResults Results = {0};

	//prevent stupid things from happening
	if ( pFakeEnt == entActivePlayerPtr() || stricmp(pFakeEnt->debugName,"FakeEntity")!=0 )
		return false;
	
	pSteps = StructCreate(parse_PowerTreeSteps);
	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pFakeEnt->pChar),pSteps, false, kPTRespecGroup_ALL);

	for (i = eaSize(&pSteps->ppSteps)-1; i >= 0; i--)
	{
		PowerTreeStep* pStep = pSteps->ppSteps[i];
		if (pStep->pchNode == pNodeDef->pchNameFull)
		{
			if (pStep->iRank > 0)
			{
				pStep->iRank--;
			}
			else
			{
				StructDestroy(parse_PowerTreeStep, eaRemove(&pSteps->ppSteps, i));
			}
			break;
		}
	}
	if (i >= 0)
	{
		NOCONST(Entity)* pValidateEnt = StructCreateWithComment(parse_Entity, "Validation ent created during gclGenPowerTreeDecreaseNodeAndValidate");
		bSuccess = entity_PowerTreesValidateSteps(PARTITION_CLIENT, pFakeEnt, NULL, pValidateEnt, pSteps, &Results, true);
		if (bSuccess && !entity_PowerTreeNodeDecreaseRankHelper(CONTAINER_NOCONST(Entity, pFakeEnt),pTreeDef->pchName,pNodeDef->pchNameFull,true,false,NULL))
		{
			bSuccess = false;
		}
		if (bSuccess && g_bCartRespecRequest)
		{
			character_PowerTreeStepsCostRespec(PARTITION_CLIENT,pEnt->pChar,pSteps,0);
			if (!entity_PowerTreeStepsRespecSteps(PARTITION_CLIENT,CONTAINER_RECONST(Entity,pValidateEnt),NULL,pSteps,NULL,NULL,NULL,true,false))
			{
				bSuccess = false;
			}
		}
		StructDestroyNoConst(parse_Entity,pValidateEnt);
	}

	// If the power tree is empty, it is not an auto-bought tree, and the node removal was successful, remove the power tree
	if (bSuccess && !pTreeDef->bAutoBuy &&
		(!IS_HANDLE_ACTIVE(pTreeDef->hClass) || !REF_COMPARE_HANDLES(pTreeDef->hClass, pFakeEnt->pChar->hClass)) &&
		(i = eaIndexedFindUsingString(&pFakeEnt->pChar->ppPowerTrees, pTreeDef->pchName)) >= 0)
	{
		if (eaSize(&pFakeEnt->pChar->ppPowerTrees[i]->ppNodes) == 0)
		{
			entity_PowerTreeRemoveHelper(CONTAINER_NOCONST(Entity, pFakeEnt), pTreeDef);
		}
	}

	//add the invalid nodes to the gen list
	//Since the gen will be cleared every frame if it doesn't have a Model call, use GenGetFailedValidateStepsNodes to display the list without clearing it.
	if (!bSuccess && pGen)
	{
		PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
		S32 j, iCount = 0;

		for (i = 0; i < eaSize(&Results.ppFailedSteps); i++)
		{
			PowerTreeStep* pFailedStep = Results.ppFailedSteps[i];
			if (pFailedStep->pchNode && pFailedStep->pchNode[0])
			{
				for (j = eaSize(peaNodes)-1; j >= 0; j--)
				{
					PowerListNode* pListNode = (*peaNodes)[j];
					const char* pchNodeName = REF_STRING_FROM_HANDLE(pListNode->hNodeDef);
					if (pchNodeName == pFailedStep->pchNode)
					{
						if (pFailedStep->iRank+1 > pListNode->iRank)
						{
							pListNode->iRank = pFailedStep->iRank+1;
						}
						break;
					}
				}
				if (j < 0)
				{
					PowerListNode* pListNode = eaGetStruct(peaNodes, parse_PowerListNode, iCount++);
					SET_HANDLE_FROM_STRING(g_hPowerTreeNodeDefDict, pFailedStep->pchNode, pListNode->hNodeDef);
					FillPowerListNode(pEnt, pListNode, 
										NULL, pTreeDef, 
										NULL, NULL, 
										NULL, GET_REF(pListNode->hNodeDef));
					pListNode->iRank = pFailedStep->iRank+1;
					pListNode->bIsHeader = false;
				}
			}
		}
		eaSetSizeStruct(peaNodes, parse_PowerListNode, iCount);
		eaQSort(*peaNodes, SortPowerListNodeByPurpose);
		gclPowerListNodesInsertHeaders(peaNodes);
		ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
	}

	StructDeInit(parse_PowerTreeValidateResults, &Results);
	StructDestroySafe(parse_PowerTreeSteps, &pSteps);

	return bSuccess;
}

S32 gclGetPowerTreeNodeRank(NOCONST(Entity) *pEnt, PowerListNode *pListNode)
{
	if(pEnt && pListNode)
	{
		PTNodeDef* pNodeDef = GET_REF(pListNode->hNodeDef);
		PowerTreeDef* pTreeDef = GET_REF(pListNode->hTreeDef);

		NOCONST(PowerTree) *pTree = entity_FindPowerTreeHelper(pEnt, pTreeDef);
		if (pTree)
		{
			NOCONST(PTNode) *pNode = character_FindPowerTreeNodeHelper(pEnt->pChar, &pTree, pNodeDef->pchNameFull);
			if (pNode)
			{
				return pNode->iRank + 1;
			}
		}
	}

	return 0;
}

static S32 gclPowerCartModifyPowerTreeNodeRank(SA_PARAM_OP_VALID UIGen *pGen,
											   SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt,
											   SA_PARAM_OP_VALID PowerListNode* pListNode, S32 iRankDifference, const char* pchNoCopyNumeric)
{
	Entity* pPlayer = entActivePlayerPtr();

	if ( pPlayer && pEnt && pFakeEnt && pListNode )
	{
		S32 iModRanks = iRankDifference;
		S32 iFakeEntNodeRanks = gclGetPowerTreeNodeRank(CONTAINER_NOCONST(Entity, pFakeEnt), pListNode);

		PTNodeDef* pNodeDef = GET_REF(pListNode->hNodeDef);
		PowerTreeDef* pTreeDef = GET_REF(pListNode->hTreeDef);

		NOCONST(PTNode)* pNode = pNodeDef && !g_bCartRespecRequest ? entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pEnt), pNodeDef ) : NULL;

		if ( pNodeDef==NULL || pTreeDef==NULL )
			return 0;

		while (	iModRanks > 0
			&&	entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT, CONTAINER_NOCONST(Entity, pFakeEnt), NULL, pTreeDef->pchName, pNodeDef->pchNameFull, pNodeDef->bSlave, false, false, NULL ) )
			iModRanks--;


		while (	(pNode==NULL || pNode->iRank <= iFakeEntNodeRanks-1+iRankDifference-(iModRanks+1))
			&&	iModRanks < 0
			&&	gclGenPowerTreeDecreaseNodeAndValidate(pGen,pEnt,pFakeEnt,pTreeDef,pNodeDef,iFakeEntNodeRanks-1+iRankDifference-(iModRanks+1),pchNoCopyNumeric))
			iModRanks++;

		//note that pListNode's rank is one higher than pNode's
		if ( (pNode && pNode->iRank >= iFakeEntNodeRanks-1+iRankDifference) || (pNode==NULL && iFakeEntNodeRanks+iRankDifference <= 0) )
		{
			gclGenExprPowerCartRemoveAllNodesWithDef( pNodeDef );
			gclGenExprSavePowerCartList(pPlayer);
			FillPowerListNode(pFakeEnt,pListNode,NULL,pTreeDef,NULL,NULL,(PTNode*)entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pFakeEnt), pNodeDef ),pNodeDef);
			return 0;
		}

		if ( iModRanks != iRankDifference )
		{
			S32 iFinalRank = iFakeEntNodeRanks+iRankDifference-iModRanks;

			if ( iRankDifference < 0 )
				gclPowerCartRemoveAllNodesWithDefAtHigherRank( pNodeDef, iFinalRank );

			gclGenExprAddPowerToCartAtRank( pFakeEnt, pListNode, iFinalRank );
			gclGenExprSavePowerCartList(pPlayer);
		}

		FillPowerListNode(pFakeEnt,pListNode,NULL,pTreeDef,NULL,NULL,(PTNode*)entity_FindPowerTreeNodeHelper( CONTAINER_NOCONST(Entity, pFakeEnt), pNodeDef ),pNodeDef);

		return iRankDifference-iModRanks;
	}

	return 0;
}

static S32 gclPowerCartSetRespec(SA_PARAM_NN_VALID UIGen *pGen,
								 SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt, bool bFillPowerCart)
{
	Entity* pPlayer = entActivePlayerPtr();

	if ( pPlayer && pEnt && pFakeEnt )
	{
		PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
		S32 bSuccess = 0;

		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pFakeEnt->pChar),pSteps, false, kPTRespecGroup_ALL);
		character_PowerTreeStepsCostRespec(PARTITION_CLIENT,pEnt->pChar,pSteps,0);

		if(entity_PowerTreeStepsRespecSteps(PARTITION_CLIENT,pFakeEnt,NULL,pSteps,NULL,NULL,NULL,true,false))
		{
			g_bCartRespecRequest = true;
			bSuccess = 1;
		}
		else
		{
			//Reset fake entity?
		}

		if(bSuccess && bFillPowerCart)
		{
			int i;

			for(i=eaSize(&pSteps->ppSteps)-1;i>=0;i--)
			{
				PTNodeDef* pNodeDef = RefSystem_ReferentFromString("PTNodeDef",pSteps->ppSteps[i]->pchNode);
				PowerTreeDef* pTreeDef = RefSystem_ReferentFromString("PowerTreeDef",pSteps->ppSteps[i]->pchTree);
				PowerListNode *pNewNode = StructCreate(parse_PowerListNode);

				if(!pNodeDef || !pTreeDef)
					continue;

				if(pTreeDef->eRespec == kPowerTreeRespec_None)
					continue;

				entity_PowerTreeNodeIncreaseRankHelper(PARTITION_CLIENT, CONTAINER_NOCONST(Entity, pFakeEnt), NULL, pTreeDef->pchName, pNodeDef->pchNameFull, pNodeDef->bSlave, false, false, NULL );
				FillPowerListNodeForEnt(pEnt,pFakeEnt,pNewNode,NULL,pTreeDef,NULL,NULL,NULL,pNodeDef);

				gclGenExprAddPowerToCartAtRank( pFakeEnt, pNewNode, pSteps->ppSteps[i]->iRank + 1 );
			}

			gclGenExprSavePowerCartList(pPlayer);
		}
		else if(bSuccess)
		{
			gclGenExprClearPowerCart();
			gclGenExprSavePowerCartList(pPlayer);
		}

		StructDestroy(parse_PowerTreeSteps,pSteps);

		return bSuccess;
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartRespec");
S32 gclGenExprPowerCartRespec(SA_PARAM_NN_VALID UIGen *pGen,
							  SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity * pFakeEnt, U32 bFillPowerCart)
{
	 return gclPowerCartSetRespec(pGen,pEnt,pFakeEnt,bFillPowerCart);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntPowerCartIsRespecRequired");
S32 gclExprEntPowerCartIsRespecRequired(SA_PARAM_OP_VALID Entity* pEnt)
{
	if (pEnt && pEnt->pPlayer)
	{
		return entity_PowerCartIsRespecRequired(pEnt);
	}
	return false;
}

//returns the amount of ranks added or removed, or 0 if there is an error
//the passed in pGen is a UIGenList which is filled with invalid nodes if modifying ranks causes the power tree to become invalid
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerTreeNodeRank");
S32 gclGenExprGetPowerTreeNodeRank(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID PowerListNode* pListNode)
{
	return gclGetPowerTreeNodeRank(CONTAINER_NOCONST(Entity, pEnt), pListNode);
}

//returns the amount of ranks added or removed, or 0 if there is an error
//the passed in pGen is a UIGenList which is filled with invalid nodes if modifying ranks causes the power tree to become invalid
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartModifyPowerTreeNodeRank");
S32 gclGenExprPowerCartModifyPowerTreeNodeRank(SA_PARAM_OP_VALID UIGen *pGen,
											   SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt,
											   SA_PARAM_OP_VALID PowerListNode* pListNode, S32 iRankDifference)
{
	return gclPowerCartModifyPowerTreeNodeRank(pGen, pEnt, pFakeEnt, pListNode, iRankDifference, NULL);
}

//returns the amount of ranks added or removed, or 0 if there is an error
//the passed in pGen is a UIGenList which is filled with invalid nodes if modifying ranks causes the power tree to become invalid
//the NoCopyNumeric is the name of a numeric that will not be copied from the owning player to the fake entity for power validation when decreasing a power's rank
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenPowerCartModifyPowerTreeNodeRankNoCopyNumeric");
S32 gclGenExprPowerCartModifyPowerTreeNodeRankNoCopyNumeric(	SA_PARAM_NN_VALID UIGen *pGen,
															   SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt,
															   SA_PARAM_OP_VALID PowerListNode* pListNode, S32 iRankDifference, const char* pchNoCopyNumeric)
{
	return gclPowerCartModifyPowerTreeNodeRank(pGen, pEnt, pFakeEnt, pListNode, iRankDifference, pchNoCopyNumeric);
}

// returns true if the old node and the new node are both supposed to propagate or both not propagating
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CheckNodesMatchPropagation");
bool gclGenExprCheckNodesMatchPropagation(	SA_PARAM_OP_VALID Entity *pBuyer, SA_PARAM_OP_VALID Entity* pEnt,
											const char* pchOldNode, const char* pchNewNode )
{
	PTNodeDef* pOldNodeDef = powertreenodedef_Find(pchOldNode);
	PTNodeDef* pNewNodeDef = powertreenodedef_Find(pchNewNode);
	return powertree_NodeHasPropagationPowers(pOldNodeDef) == powertree_NodeHasPropagationPowers(pNewNodeDef);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsEarned");
S32 gclGenExprGetPointsEarned(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints )
{
	S32 iResult = 0;

	if ( pEnt )
	{
		iResult = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchPoints);
	}

	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsSpent");
S32 gclGenExprGetPointsSpent(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints )
{
	if ( pEnt==NULL )
		return 0;

	return entity_PointsSpentTryNumeric(NULL, CONTAINER_NOCONST(Entity, pEnt), pchPoints);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsEarnedScaled");
S32 gclGenExprGetPointsEarnedScaled(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints)
{
	S32 iResult = 0;
	if (pEnt)
	{
		ItemDef* pItemDef = item_DefFromName(pchPoints);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		iResult = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchPoints) * fScale;
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsSpentScaled");
S32 gclGenExprGetPointsSpentScaled(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints)
{
	S32 iResult = 0;
	if (pEnt)
	{
		ItemDef* pItemDef = item_DefFromName(pchPoints);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		iResult = entity_PointsSpentTryNumeric(NULL, CONTAINER_NOCONST(Entity, pEnt), pchPoints) * fScale;
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsSpentForEnt");
S32 gclGenExprGetPointsSpentForEnt(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints )
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	if ( pPlayerEnt==NULL || pEnt==NULL )
		return 0;

	return entity_PointsSpentTryNumeric(CONTAINER_NOCONST(Entity, pPlayerEnt), CONTAINER_NOCONST(Entity, pEnt), pchPoints);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetPointsSpentScaledInTree");
int gclGenExprEntGetPointsSpentScaledInTree(SA_PARAM_OP_VALID Entity *pEnt, const char* pchTree, const char* pchPoints)
{
	int iSpent = 0;
	PowerTree* pTree = pEnt ? character_FindTreeByDefName(pEnt->pChar, pchTree) : NULL;
	if (pTree)
	{
		ItemDef* pItemDef = item_DefFromName(pchPoints);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		iSpent = entity_PointsSpentInTree(CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(PowerTree, pTree), pchPoints) * fScale;
	}
	return iSpent;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPointsSpentScaledForEnt");
S32 gclGenExprGetPointsSpentScaledForEnt(SA_PARAM_OP_VALID Entity *pEnt, const char* pchPoints)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	S32 iResult = 0;

	if (pEnt)
	{
		ItemDef* pItemDef = item_DefFromName(pchPoints);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		iResult = entity_PointsSpentTryNumeric(CONTAINER_NOCONST(Entity, pPlayerEnt), CONTAINER_NOCONST(Entity, pEnt), pchPoints) * fScale;
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMaxSpendablePointsInTree");
S32 gclExprEntGetMaxSpendablePointsInTree(SA_PARAM_OP_VALID Entity* pEnt, const char* pchTreeDef, const char* pchPoints)
{
	PowerTreeDef* pTreeDef = RefSystem_ReferentFromString(g_hPowerTreeDefDict, pchTreeDef);
	S32 iResult = 0;
	if (pEnt && pTreeDef)
	{
		ItemDef* pItemDef = item_DefFromName(pchPoints);
		F32 fScale = 1.0f;
		if (pItemDef)
		{
			fScale = pItemDef->fScaleUI;
		}
		iResult = entity_GetMaxSpendablePointsInTree(pEnt, pTreeDef, pchPoints) * fScale;
	}
	return iResult;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntGetMaxRankCostInTreesScaled");
int gclExprEntGetMaxRankCostInTreesScaled(SA_PARAM_OP_VALID Entity* pEnt, const char* pchPoints, S32 ePurpose)
{
	ItemDef* pItemDef = item_DefFromName(pchPoints);
	int iMaxCost = 0;
	F32 fScale = 1.0f;

	if (pItemDef)
	{
		fScale = pItemDef->fScaleUI;
	}
	if (pEnt && pEnt->pChar)
	{
		int p;
		for (p = eaSize(&pEnt->pChar->ppPowerTrees)-1; p >= 0; p--)
		{
			PowerTree* pTree = pEnt->pChar->ppPowerTrees[p];
			PowerTreeDef* pTreeDef = GET_REF(pTree->hDef);
			if (pTreeDef)
			{
				int i, j, k;
				for (i = eaSize(&pTreeDef->ppGroups)-1; i >= 0; i--)
				{
					PTGroupDef* pGroupDef = pTreeDef->ppGroups[i];
					for (j = eaSize(&pGroupDef->ppNodes)-1; j >= 0; j--)
					{
						PTNodeDef* pNodeDef = pGroupDef->ppNodes[j];
						
						if (ePurpose != kPowerPurpose_None && pNodeDef->ePurpose > ePurpose)
							continue;

						if (pNodeDef->bHasCosts)
						{
							for (k = eaSize(&pNodeDef->ppRanks)-1; k >= 0; k--)
							{
								PTNodeRankDef* pRankDef = pNodeDef->ppRanks[k];
								if (pRankDef->pchCostTable &&
									stricmp(pRankDef->pchCostTable, pchPoints)==0)
								{
									if (pRankDef->iCostScaled > iMaxCost)
									{
										iMaxCost = pRankDef->iCostScaled;
									}
								}
							}
						}
						for (k = eaSize(&pNodeDef->ppEnhancements)-1; k >= 0; k--)
						{
							PTNodeEnhancementDef* pEnhDef = pNodeDef->ppEnhancements[k];
							if (pEnhDef->pchCostTable &&
								stricmp(pEnhDef->pchCostTable, pchPoints)==0)
							{
								if (pEnhDef->iCost > iMaxCost)
								{
									iMaxCost = pEnhDef->iCost;
								}
							}
						}
					}
				}
			}
		}
	}
	return iMaxCost * fScale;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerPointEconomy");
const char* gclGenExprGetPowerPointEconomy(SA_PARAM_NN_VALID PowerListNode *pListNode)
{
	PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);

	if (pNode)
	{
		S32 i = 0;
		for (i=0; i < eaSize(&pNode->ppRanks); i++)
		{
			if (i==pListNode->iRank-1 || GET_REF(pNode->ppRanks[i]->hPowerDef) == GET_REF(pListNode->hPowerDef))
			{
				return pNode->ppRanks[i]->pchCostTable;
			}
		}
	}

	return "(null)";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerPointCostOfRank");
S32 gclGenExprGetPowerPointCostOfRank(SA_PARAM_NN_VALID PowerListNode *pListNode, S32 iRank)
{
	Entity* pEnt = pListNode->pEnt;
	PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
	PTNodeRankDef* pRankDef = pNode ? eaGet(&pNode->ppRanks, iRank-1) : NULL;
	if (pEnt && pEnt->pChar && pRankDef)
	{
		return entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pNode, iRank-1);
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerPointCostOfRankScaled");
S32 gclGenExprGetPowerPointCostOfRankScaled(SA_PARAM_NN_VALID PowerListNode *pListNode, S32 iRank)
{
	PTNodeDef* pNodeDef = GET_REF(pListNode->hNodeDef);
	S32 iCost = gclGenExprGetPowerPointCostOfRank(pListNode, iRank);
	F32 fScale = 1.0f;

	if (pNodeDef)
	{
		PTNodeRankDef* pRankDef = eaGet(&pNodeDef->ppRanks, iRank-1);
		if (pRankDef)
		{
			ItemDef* pItemDef = item_DefFromName(pRankDef->pchCostTable);
			if (pItemDef)
			{
				fScale = pItemDef->fScaleUI;
			}
		}
	}
	return iCost * fScale;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerPointCost");
S32 gclGenExprGetPowerPointCost(SA_PARAM_NN_VALID PowerListNode *pListNode)
{
	Entity* pEnt = pListNode->pEnt;
	PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
	if (pEnt && pEnt->pChar && pNode)
	{
		S32 i;
		for (i=0; i < eaSize(&pNode->ppRanks); i++)
		{
			if (i==pListNode->iRank-1 || GET_REF(pNode->ppRanks[i]->hPowerDef) == GET_REF(pListNode->hPowerDef))
			{
				return entity_PowerTreeNodeRankCostHelper(CONTAINER_NOCONST(Character, pEnt->pChar), pNode, i);
			}
		}
	}
	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerEarnedPointsForNode");
S32 gclGenExprGetPowerEarnedPointsForNode(SA_PARAM_NN_VALID PowerListNode *pListNode)
{
	Entity *pEnt = pListNode->pEnt;
	PowerTreeDef *pTree = GET_REF(pListNode->hTreeDef);
	PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
	if (pEnt && pEnt->pChar && pTree && pNode)
	{
		S32 i;
		for (i=0; i < eaSize(&pNode->ppRanks); i++)
		{
			if (i==pListNode->iRank-1 || GET_REF(pNode->ppRanks[i]->hPowerDef) == GET_REF(pListNode->hPowerDef))
			{
				const char *pchPoints = pNode->ppRanks[i]->pchCostTable;
				S32 iMaxPointsEarned = entity_GetMaxSpendablePointsInTree(pEnt, pTree, pchPoints);
				S32 iPointsEarned = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchPoints);
				if (iMaxPointsEarned && iPointsEarned > iMaxPointsEarned)
					iPointsEarned = iMaxPointsEarned;
				return iPointsEarned;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetPowerAvailablePointsForNode");
S32 gclGenExprGetPowerAvailablePointsForNode(SA_PARAM_NN_VALID PowerListNode *pListNode)
{
	Entity *pEnt = pListNode->pEnt;
	PowerTreeDef *pTree = GET_REF(pListNode->hTreeDef);
	PTNodeDef *pNode = GET_REF(pListNode->hNodeDef);
	if (pEnt && pEnt->pChar && pTree && pNode)
	{
		S32 i;
		for (i=0; i < eaSize(&pNode->ppRanks); i++)
		{
			if (i==pListNode->iRank-1 || GET_REF(pNode->ppRanks[i]->hPowerDef) == GET_REF(pListNode->hPowerDef))
			{
				const char *pchPoints = pNode->ppRanks[i]->pchCostTable;
				S32 iMaxPointsEarned = entity_GetMaxSpendablePointsInTree(pEnt, pTree, pchPoints);
				S32 iPointsEarned = entity_PointsEarned(CONTAINER_NOCONST(Entity, pEnt), pchPoints);
				S32 iPointsSpent = entity_PointsSpentTryNumeric(CONTAINER_NOCONST(Entity, pEnt), CONTAINER_NOCONST(Entity, pEnt), pchPoints);
				if (iMaxPointsEarned && iPointsEarned > iMaxPointsEarned)
					iPointsEarned = iMaxPointsEarned;
				return iPointsEarned - iPointsSpent;
			}
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTrainerList");
void gclGenExprGetTrainerList(ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char* pchPowerTables)
{
	TrainerOption ***peaOptions = ui_GenGetManagedListSafe(pGen, TrainerOption);
	S32 count = 0;
	char *pchTableCopy;
	char *pchContext;
	char *pchCurrentTable;

	strdup_alloca(pchTableCopy, pchPowerTables);
	pchCurrentTable = strtok_r(pchTableCopy, " ,\t\r\n", &pchContext);

	do
	{
		int i;
		char** eapchPowerTables = NULL;
		eaPush(&eapchPowerTables, pchCurrentTable);
		while(pchCurrentTable = strchr(pchCurrentTable, '|'))
		{
			*pchCurrentTable++ = '\0';
			eaPush(&eapchPowerTables, pchCurrentTable);
		}
		for (i = 0; i < eaSize(&eapchPowerTables); i++)
		{
			if (entity_PointsRemaining(NULL, CONTAINER_NOCONST(Entity, pEnt), NULL, eapchPowerTables[i]))
			{
				TrainerOption *pOption = eaGetStruct(peaOptions, parse_TrainerOption, count++);
				if (stricmp_safe(pOption->pchOption, eapchPowerTables[i]) != 0)
				{
					if (!pOption->pchOption)
					{
						estrCreate(&pOption->pchOption);
					}
					else
					{
						estrDestroy(&pOption->pchOption);
					}
					estrClear(&pOption->pchOption);
					estrAppend2(&pOption->pchOption, eapchPowerTables[0]);
				}
				break;
			}
		}
		eaDestroy(&eapchPowerTables);
	} while (pchCurrentTable = strtok_r(NULL, " ,\t\r\n", &pchContext));

	while (eaSize(peaOptions) > count)
		StructDestroy(parse_TrainerOption, eaPop(peaOptions));

	ui_GenSetListSafe(pGen, peaOptions, TrainerOption);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerDefHasCategory");
bool exprPowerDefHasCategory(SA_PARAM_OP_VALID PowerDef *pPowerDef, const char* pchCategory)
{
	S32 iCatIndex = StaticDefineIntGetInt(PowerCategoriesEnum, pchCategory);
	if (pPowerDef)
	{
		if (eaiFind(&pPowerDef->piCategories, iCatIndex) != -1)
		{
			return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PowerListNodeHasCategory");
bool exprPowerHasCategory(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_VALID PowerListNode *pListNode, const char* pchCategory)
{
	PTNodeDef *pNodeDef = pListNode ? GET_REF(pListNode->hNodeDef) : NULL;
	if (pEnt && pEnt->pChar && pNodeDef)
	{
		PowerDef *pDef = GET_REF(pListNode->hPowerDef);
		S32 iCatIndex = StaticDefineIntGetInt(PowerCategoriesEnum, pchCategory);
		if (pDef)
		{
			if (eaiFind(&pDef->piCategories, iCatIndex) != -1)
			{
				return true;
			}
			else
			{
				PTGroupDef *pGroupDef = GET_REF(pListNode->hGroupDef);
				S32 i;
				if (!pGroupDef)
					return false;

				for (i = 0; i < eaSize(&pGroupDef->ppNodes); i++)
				{
					PTNodeDef *pSlaveNodeDef = pGroupDef->ppNodes[i];
					PTNode *pSlaveNode = (pSlaveNodeDef && pSlaveNodeDef->bSlave) ? powertree_FindNode(pEnt->pChar, NULL, pSlaveNodeDef->pchNameFull) : NULL;
					Power *pSlavePower = (pSlaveNode && pSlaveNode->ppPowers[pSlaveNode->iRank]) ? pSlaveNode->ppPowers[pSlaveNode->iRank] : NULL;
					PowerDef *pSlaveDef = pSlavePower ? GET_REF(pSlavePower->hDef): NULL;

					if (pSlaveDef && eaiFind(&pSlaveDef->piCategories, iCatIndex) != -1)
					{
						return true;
					}
				}
			}
		}
	}
	return false;
};

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PTNodeDefFromPowerCartNode");
SA_RET_OP_VALID PTNodeDef *PTNodeDefFromPowerCartNode(SA_PARAM_OP_VALID PowerCartListNode *pCartNode)
{
	if (pCartNode && pCartNode->pPowerListNode)
	{
		return GET_REF(pCartNode->pPowerListNode->hNodeDef);
	}
	return NULL;
}



AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EnhPointsSpentOnNode");
S32 exprEnhPointsSpentOnNode(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_OP_VALID PTNodeDef *pNodeDef)
{
	return (pEnt && pNodeDef) ? entity_PowerTreeNodeEnhPointsSpentHelper(CONTAINER_NOCONST(Entity, pEnt), pNodeDef) : -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("MaxEnhPoints");
S32 exprMaxEnhPoints(SA_PARAM_OP_VALID PTNodeDef *pNodeDef)
{
	return pNodeDef ? pNodeDef->iCostMaxEnhancement : -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("HueToRGB");
S32 exprHueToRGB(F32 hue)
{
	Vec3 hsv = {hue, 1, 1};
	Vec3 rgb;
	S32 rgba = 0xFF;
	hsvToRgb(hsv, rgb);
	rgba |= ((S32)(rgb[0]*255)) << 24;
	rgba |= ((S32)(rgb[1]*255)) << 16;
	rgba |= ((S32)(rgb[2]*255)) << 8;
	return rgba;
}

// Returns true if the Entity's PowerTrees are currently valid
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("EntPowerTreesValid");
bool gclGenExprEntPowerTreesValid(SA_PARAM_OP_VALID Entity *pEnt)
{
	bool bResult = false;
	if(pEnt)
	{
		bResult = entity_PowerTreesValidate(PARTITION_CLIENT,pEnt,NULL,NULL);
	}
	return bResult;
}

// Updates the Gen's managed list of PowerTreeSteps for a respec
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetMyRespecSteps");
bool gclGenExprGetMyRespecSteps(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt)
{
	PowerTreeSteps *pSteps = NULL;
	PowerTreeStep ***peaSteps =  ui_GenGetManagedListSafe(pGen, PowerTreeStep);
	S32 iLength = 0;
	S32 i,j;

	if(pEnt && pEnt->pChar)
	{
		// Get the steps to recreate the PowerTrees.  This does a bunch of allocation (rebuilds from scratch)
		//  which is really bad for the UI, but that's the way the helper is written at this time.  There is
		//  a function written to request this data from the server, which would probably be safer than
		//  calculating it on the client, but then you have to deal with waiting for a server request to
		//  come back, and so on.  That's probably the right long term solution though.
		pSteps = StructCreate(parse_PowerTreeSteps);
		character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pEnt->pChar),pSteps, true, kPTRespecGroup_ALL);
		character_PowerTreeStepsCostRespec(PARTITION_CLIENT,pEnt->pChar,pSteps,0);
		iLength = eaSize(&pSteps->ppSteps);

		for(i=j=0; i<iLength; i++)
		{
			PowerTreeStep *pStep = pSteps->ppSteps[i];

			// If this step's cost is negative, it's disallowed, which also implies everything after it
			//  is disallowed, so we break here.
			if(pStep->iCostRespec < 0)
				break;

			if(pStep->iCostRespec==0 && !pStep->pchNode)
			{
				// This is a free step that represents an entire tree.  For now, we're going to bake
				//  these steps into the prior step (if there is one).
				if(j>0)
				{
					PowerTreeStep *pStepPrior = (*peaSteps)[j-1];
					pStepPrior->iStepsImplied++;
					continue;
				}
			}

			// Make sure there's a step in the managed list to copy into
			if(j>=eaSize(peaSteps))
			{
				eaPush(peaSteps,StructCreate(parse_PowerTreeStep));
			}

			// Copy everything into the step in the managed list and increment how many steps we've copied
			StructCopyAll(parse_PowerTreeStep,pStep,(*peaSteps)[j]);
			j++;
		}

		iLength = j;
	}

	// Clean up the unneeded portion of the managed list
	while(eaSize(peaSteps) > iLength)
		StructDestroy(parse_PowerTreeStep, eaPop(peaSteps));

	ui_GenSetManagedListSafe(pGen, peaSteps, PowerTreeStep, true);

	StructDestroy(parse_PowerTreeSteps,pSteps);

	return true;
}


// Gets the translated name of a PowerTreeStep
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStepName");
const char *gclGenExprGetStepName(SA_PARAM_OP_VALID PowerTreeStep *pstep)
{
	if(pstep)
	{
		if(pstep->pchEnhancement)
		{
			PowerDef *pdefEnh = powerdef_Find(pstep->pchEnhancement);
			if(pdefEnh)
			{
				return TranslateDisplayMessage(pdefEnh->msgDisplayName);
			}
		}
		else if(pstep->pchNode)
		{
			PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
			if(pdefNode)
			{
				return TranslateDisplayMessage(pdefNode->pDisplayMessage);
			}
		}
		else if(pstep->pchTree)
		{
			PowerTreeDef *pdefTree = powertreedef_Find(pstep->pchTree);
			if(pdefTree)
			{
				return TranslateDisplayMessage(pdefTree->pDisplayMessage);
			}
		}
	}
	return "(null)";
}

// Gets the translated name of a PowerTreeStep's power node specifically
// This is used for enhancements' parent power's names.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStepPowerName");
const char *gclGenExprGetStepPowerName(SA_PARAM_OP_VALID PowerTreeStep *pstep)
{
	if(pstep && pstep->pchEnhancement && pstep->pchNode)
	{
		PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
		if(pdefNode)
		{
			return TranslateDisplayMessage(pdefNode->pDisplayMessage);
		}
	}

	return "(null)";
}

// Gets the translated description of a PowerTreeStep
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStepDesc");
const char *gclGenExprGetStepDesc(SA_PARAM_OP_VALID PowerTreeStep *pstep)
{
	if(pstep)
	{
		if(pstep->pchEnhancement)
		{
			PowerDef *pdefEnh = powerdef_Find(pstep->pchEnhancement);
			if(pdefEnh)
			{
				return TranslateDisplayMessage(pdefEnh->msgDescription);
			}
		}
		else if(pstep->pchNode)
		{
			PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
			if(pdefNode && eaSize(&pdefNode->ppRanks) && pdefNode->ppRanks[0] && GET_REF(pdefNode->ppRanks[0]->hPowerDef))
			{
				PowerDef *pdefNodePower = GET_REF(pdefNode->ppRanks[0]->hPowerDef);
				if(pdefNodePower)
				{
					return TranslateDisplayMessage(pdefNodePower->msgDescription);
				}
			}
		}
		else if(pstep->pchTree)
		{
			PowerTreeDef *pdefTree = powertreedef_Find(pstep->pchTree);
			if(pdefTree)
			{
				return TranslateDisplayMessage(pdefTree->pDescriptionMessage);
			}
		}
	}
	return "(null)";
}

// Gets the icon of a PowerTreeStep
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStepIcon");
const char *gclGenExprGetStepIcon(SA_PARAM_OP_VALID PowerTreeStep *pstep)
{
	if(pstep)
	{
		if(pstep->pchEnhancement)
		{
			PowerDef *pdefEnh = powerdef_Find(pstep->pchEnhancement);
			if(pdefEnh && pdefEnh->pchIconName)
			{
				return pdefEnh->pchIconName;
			}
		}
		else if(pstep->pchNode)
		{
			PTNodeDef *pdefNode = powertreenodedef_Find(pstep->pchNode);
			if(pdefNode && eaSize(&pdefNode->ppRanks) && pdefNode->ppRanks[0] && GET_REF(pdefNode->ppRanks[0]->hPowerDef))
			{
				PowerDef *pdefNodePower = GET_REF(pdefNode->ppRanks[0]->hPowerDef);
				if(pdefNodePower && pdefNodePower->pchIconName)
				{
					return pdefNodePower->pchIconName;
				}
			}
		}
		else if(pstep->pchTree)
		{
			PowerTreeDef *pdefTree = powertreedef_Find(pstep->pchTree);
			if(pdefTree)
			{
				// TODO: Where do tree icons come from?!
				return "";
			}
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetStepType");
const char *gclGenExprGetStepType(SA_PARAM_OP_VALID PowerTreeStep *pstep)
{
	if(pstep)
	{
		if(pstep->pchEnhancement)
		{
			return "Enhancement";
		}
		else if(pstep->pchNode)
		{
			return "PTNode";
		}
		else if(pstep->pchTree)
		{
			return "Tree";
		}
	}
	return "(null)";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayerGetFullRespecCost");
S32 gclPlayerGetFullRespecCost(SA_PARAM_NN_VALID Entity *pPlayer)
{
	PowerTreeSteps *pSteps = StructCreate(parse_PowerTreeSteps);
	int iTotalCost = 0;

	character_GetPowerTreeSteps(CONTAINER_NOCONST(Character, pPlayer->pChar),pSteps, true, kPTRespecGroup_ALL);
	character_PowerTreeStepsCostRespec(PARTITION_CLIENT,pPlayer->pChar,pSteps,0);

	iTotalCost = GetPowerTreeSteps_TotalCost(pSteps);

	StructDestroy(parse_PowerTreeSteps,pSteps);

	return iTotalCost;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetRespecTotalCost");
S32 gclGenExprRespecTotalCost(SA_PARAM_NN_VALID UIGen *pGen)
{
	static PowerTreeSteps *pSteps = NULL;
	PowerTreeStep ***peaSteps = ui_GenGetManagedListSafe(pGen, PowerTreeStep);
	S32 iTotalCost = 0;

	if(!pSteps)
		pSteps = StructCreate(parse_PowerTreeSteps);

	if(peaSteps)
	{
		eaCopy(&pSteps->ppSteps, peaSteps);
		iTotalCost = GetPowerTreeSteps_TotalCost(pSteps);
	}

	eaClearFast(&pSteps->ppSteps);

	ui_GenSetListSafe(pGen, peaSteps, PowerTreeStep);
	return iTotalCost;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetRespecNumericCost");
S32 gclGenExprGetRespecNumericCost(S32 eRespecType)
{
	Entity* pEnt = entActivePlayerPtr();
	
	return PowerTree_GetNumericRespecCost(CONTAINER_NOCONST(Entity, pEnt), eRespecType);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetRespecTotalSteps");
S32 gclGenExprGetRespecTotalSteps(SA_PARAM_NN_VALID UIGen *pGen)
{
	PowerTreeStep ***peaSteps =  ui_GenGetManagedListSafe(pGen, PowerTreeStep);
	S32 iTotalSteps = 0;

	if(peaSteps)
	{
		S32 iStepIdx;
		for(iStepIdx = eaSize(peaSteps)-1; iStepIdx >= 0; iStepIdx--)
		{
			iTotalSteps += 1 + (*peaSteps)[iStepIdx]->iStepsImplied;
		}
	}

	ui_GenSetListSafe(pGen, peaSteps, PowerTreeStep);
	return iTotalSteps;
}

//Returns true/false if the character has a free respec available
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FreeRespecAvailable");
S32 gclGenExprFreeRespecAvailable(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(pEnt &&
		pEnt->pChar)
	{
		return (timeServerSecondsSince2000() >= pEnt->pChar->iFreeRespecAvailable);
	}
	return(false);
}

//Returns the number of account wide respecs the character has available.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetGameAccountRespecs");
S32 gclGenExprGetGameAccountRespecs(SA_PARAM_OP_VALID Entity *pEnt)
{
	if(pEnt && pEnt->pPlayer)
	{
		GameAccountData *pData = GET_REF(pEnt->pPlayer->pPlayerAccountData->hData);
		if(pData)
		{
			if (gConf.bDontAllowGADModification)
				return gad_GetAccountValueInt(pData, MicroTrans_GetRespecTokensASKey());
			else
				return gad_GetAttribInt(pData, MicroTrans_GetRespecTokensGADKey());
		}
	}

	return(0);
}

//Returns the number of character specific, item-based respecs the character has available.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetItemRespecs");
S32 gclGenExprGetItemRespecs(SA_PARAM_OP_VALID Entity *pEnt)
{
	return inv_GetNumericItemValue(pEnt, MicroTrans_GetRespecTokensKeyID());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTrainableNodesList");
void gclGenExprGenGetTrainableNodesList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	PowerListNode ***peaNodes  = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 i, j, k, iSize = 0;

	if ( pEnt==NULL || pEnt->pChar==NULL )
		return;

	for ( i = 0; i < eaSize( &pEnt->pChar->ppPowerTrees ); i++ )
	{
		for ( j = 0; j < eaSize( &pEnt->pChar->ppPowerTrees[i]->ppNodes ); j++ )
		{
			PTNode* pPTNode = pEnt->pChar->ppPowerTrees[i]->ppNodes[j];
			PTNodeDef* pNodeDef = GET_REF(pPTNode->hDef);
			S32 iRankSearchSize = pNodeDef ? MIN(pPTNode->iRank+1, eaSize(&pNodeDef->ppRanks)) : 0;
			for ( k = 0; k < iRankSearchSize; k++ )
			{
				PTNodeDef* pTrainerNodeDef = GET_REF(pNodeDef->ppRanks[k]->hTrainerUnlockNode);
				if ( pTrainerNodeDef )
				{
					char* pchGroup;
					PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pTrainerNodeDef);
					PowerListNode* pNode = eaGetStruct(peaNodes, parse_PowerListNode, iSize++);
					pNode->bIsHeader = false;
					COPY_HANDLE(pNode->hNodeDef,pNodeDef->ppRanks[k]->hTrainerUnlockNode);
					SET_HANDLE_FROM_REFERENT(g_hPowerTreeDefDict,pTreeDef,pNode->hTreeDef);
					if ( eaSize(&pTrainerNodeDef->ppRanks)>0 )
						COPY_HANDLE(pNode->hPowerDef,pTrainerNodeDef->ppRanks[0]->hPowerDef);
					estrStackCreate(&pchGroup);
					if (powertree_GroupNameFromNodeDef(pTrainerNodeDef, &pchGroup) && pchGroup)
						SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pchGroup,pNode->hGroupDef);
					estrDestroy(&pchGroup);
				}
			}
		}
	}
	while (eaSize(peaNodes) > iSize)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	eaQSort( *peaNodes, SortPowerListNodeByPurpose );
	gclPowerListNodesInsertHeaders( peaNodes );
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainableNodesCount");
S32 gclGenExprGetTrainableNodesCount(SA_PARAM_OP_VALID Entity* pEnt)
{
	S32 i, j, k, iCount = 0;

	if ( pEnt==NULL || pEnt->pChar==NULL )
		return 0;

	for ( i = 0; i < eaSize( &pEnt->pChar->ppPowerTrees ); i++ )
	{
		for ( j = 0; j < eaSize( &pEnt->pChar->ppPowerTrees[i]->ppNodes ); j++ )
		{
			PTNode* pPTNode = pEnt->pChar->ppPowerTrees[i]->ppNodes[j];
			PTNodeDef* pNodeDef = GET_REF(pPTNode->hDef);
			S32 iRankSearchSize = pNodeDef ? MIN(pPTNode->iRank+1, eaSize(&pNodeDef->ppRanks)) : 0;
			for ( k = 0; k < iRankSearchSize; k++ )
			{
				PTNodeDef* pTrainerNodeDef = GET_REF(pNodeDef->ppRanks[k]->hTrainerUnlockNode);
				if ( pTrainerNodeDef )
					iCount++;
			}
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGetTrainingProgressList");
void gclGenExprGetTrainingProgressList(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID Entity* pEnt)
{
	PowerListNode ***peaNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 i, iSize = 0;

	if (SAFE_MEMBER(pEnt, pChar))
	{
		for ( i = 0; i < eaSize(&pEnt->pChar->ppTraining); i++ )
		{
			char* pchGroup;
			PowerTreeDef* pTreeDef;
			PTNodeDef* pTrainerNodeDef;
			PowerListNode* pNode = eaGetStruct(peaNodes, parse_PowerListNode, iSize++);
			COPY_HANDLE(pNode->hNodeDef,pEnt->pChar->ppTraining[i]->hNewNodeDef);
			pNode->pTrainingInfo = pEnt->pChar->ppTraining[i];
			pNode->bIsTraining = true;
			pTrainerNodeDef = GET_REF(pNode->hNodeDef);
			pTreeDef = powertree_TreeDefFromNodeDef(pTrainerNodeDef);
			if ( pTrainerNodeDef && eaSize(&pTrainerNodeDef->ppRanks)>0 )
				COPY_HANDLE(pNode->hPowerDef, pTrainerNodeDef->ppRanks[0]->hPowerDef);
			if ( pTreeDef )
				SET_HANDLE_FROM_REFERENT(g_hPowerTreeDefDict, pTreeDef, pNode->hTreeDef);
			estrStackCreate(&pchGroup);
			if (powertree_GroupNameFromNodeDef(pTrainerNodeDef, &pchGroup) && pchGroup)
				SET_HANDLE_FROM_STRING(g_hPowerTreeGroupDefDict,pchGroup,pNode->hGroupDef);
			estrDestroy(&pchGroup);
		}
	}
	while (eaSize(peaNodes) > iSize)
		StructDestroy(parse_PowerListNode, eaPop(peaNodes));
	eaQSort( *peaNodes, SortPowerListNodeByPurpose );
	gclPowerListNodesInsertHeaders( peaNodes );
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsEntTraining");
bool gclGenExprIsEntTraining(SA_PARAM_OP_VALID Entity* pEnt)
{
	return pEnt && pEnt->pChar && eaSize(&pEnt->pChar->ppTraining)>0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingProgress");
F32 gclGenExprGetTrainingProgress(SA_PARAM_OP_VALID CharacterTraining* pInfo)
{
	return pInfo ? MIN((timeServerSecondsSince2000() - pInfo->uiStartTime)/(F32)(pInfo->uiCompleteTime - pInfo->uiStartTime),1) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingTimeRemaining");
S32 gclGenExprGetTrainingTimeRemaining(SA_PARAM_OP_VALID CharacterTraining* pInfo)
{
	return pInfo ? MAX(pInfo->uiCompleteTime - timeServerSecondsSince2000(),0) : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingRefundAmount");
S32 gclGenExprGetTrainingRefundAmount( SA_PARAM_OP_VALID CharacterTraining* pInfo )
{
	return pInfo ? pInfo->iRefundAmount : 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingTimeFromPower");
U32 gclGenExprGetTrainingTimeFromPowerDef( const char* pchPowerDef, const char *pcAllegiance )
{
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pcAllegiance);
	if ( Officer_GetRankCount(pAllegiance) > 0 )
	{
		PowerDef *pPowDef = RefSystem_ReferentFromString("PowerDef",pchPowerDef);
		OfficerRankDef* pOfficerRankDef = pPowDef ? Officer_GetRankDef( pPowDef->ePurpose - 1,pAllegiance,NULL ) : NULL;

		if ( pOfficerRankDef )
			return pOfficerRankDef->uiTrainingTime;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingCostFromPower");
S32 gclGenExprGetTrainingCostFromPowerDef( const char* pchPowerDef, const char *pcAllegiance )
{
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pcAllegiance);
	if ( Officer_GetRankCount(pAllegiance) > 0 )
	{
		PowerDef *pPowDef = RefSystem_ReferentFromString("PowerDef",pchPowerDef);
		OfficerRankDef* pOfficerRankDef = pPowDef ? Officer_GetRankDef( pPowDef->ePurpose - 1,pAllegiance,NULL ) : NULL;

		if ( pOfficerRankDef )
			return pOfficerRankDef->iTrainingCost;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GetTrainingCostNumericFromPower");
const char* gclGenExprGetTrainingCostNumericFromPowerDef( const char* pchPowerDef, const char *pcAllegiance )
{
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pcAllegiance);
	if ( Officer_GetRankCount(pAllegiance) > 0 )
	{
		PowerDef *pPowDef = RefSystem_ReferentFromString("PowerDef",pchPowerDef);
		OfficerRankDef* pOfficerRankDef = pPowDef ? Officer_GetRankDef( pPowDef->ePurpose - 1,pAllegiance,NULL ) : NULL;

		if ( pOfficerRankDef )
			return pOfficerRankDef->pchTrainingNumeric;
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AutoDescAttribModGetAttribType");
U32 gclExprAutoDescAttribModGetAttribType(AutoDescAttribMod* pMod)
{
	return SAFE_MEMBER(pMod, offAttrib);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(PowerListNodeGetName);
const char *gclPowerListNodeExprGetName(SA_PARAM_OP_VALID PowerListNode *pListNode)
{
	return pListNode && GET_REF(pListNode->hPowerDef) ? TranslateDisplayMessage(GET_REF(pListNode->hPowerDef)->msgDisplayName) : "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectLevelTreeNodeGetPowersWithFakeEnt");
void gclGenExprGroupProjectLevelTreeNodeGetPowersWithFakeEnt(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity *pFakeEnt, const char *pchLevelNodeKey, const char *pchFilter, S32 iFlags)
{
	static PowerTreeDef **s_eaOwnedPowerTrees;
	static PowerTreeDef **s_eaUnownedPowerTrees;
	static PTGroupDef **s_eaPowerGroups;
	static PowerTreeDefRef **s_eaPowerTreeRefs;
	char *pchGroupProject = NULL, *pchUnlockNumeric, *pchNumericUnlock, *pchManualUnlock;
	PowerListNode ***peaPowerNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	S32 iCount = 0;

	if (pchLevelNodeKey && *pchLevelNodeKey)
		strdup_alloca(pchGroupProject, pchLevelNodeKey);
	pchUnlockNumeric = pchGroupProject ? strchr(pchGroupProject, ',') : NULL;
	pchNumericUnlock = pchUnlockNumeric ? strchr(pchUnlockNumeric + 1, ',') : NULL;
	pchManualUnlock = pchNumericUnlock ? strchr(pchNumericUnlock + 1, ',') : NULL;

	if (pchManualUnlock)
	{
		GroupProjectDef *pGroupProject = NULL;
		S32 i, j;

		*pchUnlockNumeric++ = '\0';
		*pchNumericUnlock++ = '\0';
		*pchManualUnlock++ = '\0';

		pGroupProject = RefSystem_ReferentFromString("GroupProjectDef", pchGroupProject);

		// Determine power trees to look at
		if (pGroupProject && pEnt && pEnt->pChar)
		{
			// Add owned power trees
			for (i = 0; i < eaSize(&pEnt->pChar->ppPowerTrees); i++)
			{
				PowerTreeDef *pTreeDef = GET_REF(pEnt->pChar->ppPowerTrees[i]->hDef);
				if (pTreeDef && eaFind(&pGroupProject->powerTrees, pTreeDef->pchName) >= 0)
				{
					eaPush(&s_eaOwnedPowerTrees, pTreeDef);
					gclPowersUIRequestRefsTree(pTreeDef);
				}
			}

			// Determine unowned power trees
			for (i = 0; i < eaSize(&pGroupProject->powerTrees); i++)
			{
				PowerTreeDef *pTreeDef = RefSystem_ReferentFromString("PowerTreeDef", pGroupProject->powerTrees[i]);
				if (pTreeDef)
				{
					if (eaFind(&s_eaOwnedPowerTrees, pTreeDef) < 0)
					{
						eaPush(&s_eaUnownedPowerTrees, pTreeDef);
						gclPowersUIRequestRefsTree(pTreeDef);
					}
				}
				else
				{
					// Make references for power trees not available
					for (j = eaSize(&s_eaPowerTreeRefs) - 1; j >= 0; j--)
					{
						if (!GET_REF(s_eaPowerTreeRefs[j]->hRef) && REF_STRING_FROM_HANDLE(s_eaPowerTreeRefs[j]->hRef) == pGroupProject->powerTrees[i])
							break;
					}
					if (j < 0)
					{
						PowerTreeDefRef *pRef = StructCreate(parse_PowerTreeDefRef);
						SET_HANDLE_FROM_STRING("PowerTreeDef", pGroupProject->powerTrees[i], pRef->hRef);
						eaPush(&s_eaPowerTreeRefs, pRef);
					}
				}
			}

			// Add potential power trees
			for (i = 0; i < eaSize(&s_eaUnownedPowerTrees); i++)
			{
				if (entity_CanBuyPowerTreeHelper(PARTITION_CLIENT, CONTAINER_NOCONST(Entity, pEnt), s_eaUnownedPowerTrees[i], s_eaUnownedPowerTrees[i]->bTemporary))
					eaPush(&s_eaOwnedPowerTrees, s_eaUnownedPowerTrees[i]);
			}
		}

		// Determine power tree groups to look at
		for (i = 0; i < eaSize(&g_GroupProjectLevelTreeDef.eaLevelNodes); i++)
		{
			GroupProjectLevelTreeNodeDef *pLevelNodeDef = g_GroupProjectLevelTreeDef.eaLevelNodes[i];

			if (stricmp(pLevelNodeDef->pchManualUnlock, pchManualUnlock))
				continue;
			if (stricmp(pLevelNodeDef->pchNumericUnlock, pchNumericUnlock))
				continue;

			for (j = 0; j < eaSize(&s_eaOwnedPowerTrees); j++)
			{
				S32 iGroup, iGroupAllowed;
				for (iGroup = 0; iGroup < eaSize(&s_eaOwnedPowerTrees[j]->ppGroups); iGroup++)
				{
					PTGroupDef *pGroupDef = s_eaOwnedPowerTrees[j]->ppGroups[iGroup];
					for (iGroupAllowed = eaSize(&pLevelNodeDef->eaPowerTreeGroups) - 1; iGroupAllowed >= 0; iGroupAllowed--)
					{
						if (!stricmp(pLevelNodeDef->eaPowerTreeGroups[iGroupAllowed], pGroupDef->pchGroup))
						{
							eaPush(&s_eaPowerGroups, pGroupDef);
							break;
						}
					}
				}
			}
		}

		// Add power tree nodes
		for (i = 0; i < eaSize(&s_eaPowerGroups); i++)
		{
			PTGroupDef *pPowerGroup = s_eaPowerGroups[i];
			PowerTreeDef *pPowerTree = powertree_TreeDefFromGroupDef(pPowerGroup);

			for (j = 0; j < eaSize(&pPowerGroup->ppNodes); j++)
			{
				PTNodeDef *pPowerNode = pPowerGroup->ppNodes[j];
				PowerListNode *pListNode = NULL;
				S32 iNodes, iRanks;

				// Node is hidden
				if (pPowerNode->eFlag & kNodeFlag_HideNode)
					continue;

				// A PowerDef is not loaded yet
				for (iRanks = eaSize(&pPowerNode->ppRanks) - 1; iRanks >= 0; iRanks--)
				{
					if (!GET_REF(pPowerNode->ppRanks[iRanks]->hPowerDef))
						break;
				}
				if (iRanks >= 0)
					continue;

				// TODO: insert more node filtering here

				// Find or create power list node
				for (iNodes = iCount; iNodes < eaSize(peaPowerNodes); iNodes++)
				{
					if (GET_REF((*peaPowerNodes)[iNodes]->hNodeDef) == pPowerNode)
					{
						if (iNodes != iCount)
							eaMove(peaPowerNodes, iCount, iNodes);
						pListNode = (*peaPowerNodes)[iCount];
						iCount++;
						break;
					}
				}
				if (!pListNode)
				{
					pListNode = StructCreate(parse_PowerListNode);
					eaInsert(peaPowerNodes, pListNode, iCount++);
				}

				FillPowerListNodeForEnt(pEnt, FIRST_IF_SET(pFakeEnt, pEnt), pListNode, NULL, pPowerTree, NULL, pPowerGroup, NULL, pPowerNode);

				if (pEnt && pListNode->iRank < pListNode->iMaxRank)
				{
					pListNode->bIsAvailable = entity_CanBuyPowerTreeNodeHelper(
						ATR_EMPTY_ARGS,
						PARTITION_CLIENT,
						NULL,
						CONTAINER_NOCONST(Entity, pEnt),
						pPowerGroup,
						pPowerNode,
						pListNode->iRank,
						true,
						true,
						false,
						false
					);
				}
				else
				{
					pListNode->bIsAvailable = false;
				}

				if (pFakeEnt && pListNode->iRank < pListNode->iMaxRank)
				{
					pListNode->bIsAvailableForFakeEnt = entity_CanBuyPowerTreeNodeHelper(
						ATR_EMPTY_ARGS,
						PARTITION_CLIENT,
						NULL,
						CONTAINER_NOCONST(Entity, pEnt),
						pPowerGroup,
						pPowerNode,
						pListNode->iRank,
						true,
						true,
						false,
						false
					);
				}
				else
				{
					pListNode->bIsAvailableForFakeEnt = false;
				}
			}
		}
	}

	eaClearFast(&s_eaPowerGroups);
	eaClearFast(&s_eaUnownedPowerTrees);
	eaClearFast(&s_eaOwnedPowerTrees);

	eaSetSizeStruct(peaPowerNodes, parse_PowerListNode, iCount);
	ui_GenSetManagedListSafe(pGen, peaPowerNodes, PowerListNode, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectLevelTreeNodeGetPowers");
void gclGenExprGroupProjectLevelTreeNodeGetPowers(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, const char *pchLevelNodeKey, const char *pchFilter, S32 iFlags)
{
	gclGenExprGroupProjectLevelTreeNodeGetPowersWithFakeEnt(pGen, pEnt, NULL, pchLevelNodeKey, pchFilter, iFlags);
}

AUTO_FIXUPFUNC;
TextParserResult PTUICategoryListNodeParserFixup(PTUICategoryListNode *pCatListNode, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pCatListNode->eaTreeHolder);
	}
	return PARSERESULT_SUCCESS;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetFailedValidateStepsNodes);
void gclGenGetFailedValidateStepsNodes(SA_PARAM_NN_VALID UIGen *pGen)
{
	PowerListNode ***peaNodes  = ui_GenGetManagedListSafe(pGen, PowerListNode);

	// This function does nothing, just doesn't clear the list that was set by one of the error functions above.
	
	ui_GenSetManagedListSafe(pGen, peaNodes, PowerListNode, true);
}

bool gclGenExprInitFakePlayerWithPowers(SA_PARAM_OP_VALID Entity *pOwner, SA_PARAM_OP_VALID Entity *pEnt);
void gclGenExprDeInitFakePlayer(void);

// Warning: I can totally imagine a power tree that has dependencies that aren't based on level. If you call this function on such a tree,
// it could run every frame. Please be careful!
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenPowerCartRemovePowersInErrorList);
void gclGenPowerCartRemovePowersInErrorList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID Entity* pFakeEnt )
{
	PowerListNode ***peaGenNodes = ui_GenGetManagedListSafe(pGen, PowerListNode);
	if( eaSize(peaGenNodes) > 0 )
	{
		int i;
		PowerListNode **peaNodes = NULL;

		// Each time you call gclGenPowerTreeDecreaseNodeAndValidate(), the list gen's list might be changed,
		// so copy all the existing nodes completely, so they don't get destroyed out from under us.
		eaCopyStructs(peaGenNodes, &peaNodes, parse_PowerListNode);

		// sort the PowerListNodes by lowest to highest level required
		eaQSort(peaNodes, SortPowerListNodesByLevel);

		for( i = eaSize(&peaNodes)-1; i >= 0; --i )
		{
			PowerListNode *pListNode = peaNodes[i];
			// Remove PowerListNodes from the cart from highest to lowest level required
			gclGenExprPowerCartModifyPowerTreeNodeRank(pGen, pEnt, pFakeEnt, pListNode, -1);
		}

		eaClearStruct(&peaNodes, parse_PowerListNode);

		// Reset the FakePlayer
		gclGenExprDeInitFakePlayer();
		gclGenExprInitFakePlayerWithPowers(pEnt, pEnt);

		// Apply the powers in the cart to the fake player
		for( i = 0; i < eaSize(&s_eaCartPowerNodes); ++i )
		{
			PowerListNode *pListNode = s_eaCartPowerNodes[i];
			gclGenExprPowerCartModifyPowerTreeNodeRank(pGen, pEnt, pFakeEnt, pListNode, 1);
		}
		gclGenExprSavePowerCartList(pEnt);
	}

	ui_GenSetManagedListSafe(pGen, NULL, PowerListNode, true);
}

#include "PowerGrid_h_ast.c"
#include "PowerGrid_c_ast.c"
