#include "gclGroupProjectUI.h"
#include "Entity.h"
#include "gclEntity.h"
#include "Player.h"
#include "Guild.h"
#include "UIGen.h"
#include "UICore.h"
#include "ItemCommon.h"
#include "itemCommon_h_ast.h"
#include "ItemEnums.h"
#include "itemEnums_h_ast.h"
#include "inventoryCommon.h"
#include "GameClientLib.h"
#include "FCInventoryUI.h"
#include "StringCache.h"
#include "Expression.h"
#include "gclGroupProject.h"
#include "gclUIGen.h"
#include "StringUtil.h"
#include "fileutil.h"
#include "FolderCache.h"

#include "AutoGen/GroupProjectCommon_h_ast.h"
#include "AutoGen/gclGroupProjectUI_h_ast.h"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static NOCONST(GroupProjectContainer) *s_pDummyGuild;
static NOCONST(GroupProjectContainer) *s_pDummyPlayer;

typedef struct DonationTaskFilter
{
	const char *pchNameFilter;
	const char *pchDescriptionFilter;
	GroupProjectTaskSlotType eSlotType;
	U32 uCategoryMask;
	bool bSortByCategories : 1;
	bool bAddCategoryHeaders : 1;
} DonationTaskFilter;

typedef struct DonationSlotFilter
{
	U32 iFirstSlot;
	U32 iLastSlot;
	GroupProjectTaskSlotType eSlotType;
	bool bFilled : 1;
} DonationSlotFilter;

typedef struct GroupProjectDonorFilter
{
	const char *pchNameFilter;
	bool bSortName : 1;
} GroupProjectDonorFilter;

typedef struct GroupProjectFilter
{
	const char *pchNameFilter;
	GroupProjectType eProjectType;
} GroupProjectFilter;

typedef struct GroupProjectUnlockFilter
{
	const char *pchNameFilter;
	bool bSortInternalGrouped : 1;
} GroupProjectUnlockFilter;

typedef struct GroupProjectNumericFilter
{
	const char **eapchNameFilters;
} GroupProjectNumericFilter;

typedef struct DonationTaskBucketDonationResultFilter
{
	bool bIncludeSuccessfulDonations : 1;
	bool bIncludePartialDonations : 1;
	bool bIncludeFailedDonations : 1;
} DonationTaskBucketDonationResultFilter;

GroupProjectContainer *gclGroupProject_ResolveContainer(U32 eProjectType)
{
	Entity *pEnt = entActivePlayerPtr();
	GroupProjectContainer *pContainer;

    if ( pEnt == NULL )
    {
        return NULL;
    }

	pContainer = GroupProject_ResolveContainer(pEnt, eProjectType);
	if (eProjectType == GroupProjectType_Guild && pContainer && pContainer->ownerID != guild_GetGuildID(pEnt))
		return NULL;
	if (eProjectType == GroupProjectType_Player && pContainer && pContainer->ownerID != entGetContainerID(pEnt))
		return NULL;

    return pContainer;
}

GroupProjectState *gclGroupProject_ResolveState(GroupProjectType eProjectType, const char *pchGroupName)
{
	GroupProjectDef *pGroupProjectDef = NULL;
	GroupProjectState *pState = NULL;
	GroupProjectContainer *pContainer;
	S32 i;

	pGroupProjectDef = RefSystem_ReferentFromString("GroupProjectDef", pchGroupName);
	if (!pGroupProjectDef || pGroupProjectDef->type != eProjectType)
		return NULL;

	pContainer = gclGroupProject_ResolveContainer(eProjectType);

	if (pContainer)
	{
		for (i = eaSize(&pContainer->projectList) - 1; i >= 0; i--)
		{
			if (GET_REF(pContainer->projectList[i]->projectDef) == pGroupProjectDef)
			{
				pState = pContainer->projectList[i];
				break;
			}
		}
	}

	// The guild project state may not exist yet, so create a dummy state for
	// the group project.
	if (!pState && eProjectType == GroupProjectType_Guild)
	{
		Guild *pGuild = guild_GetGuild(entActivePlayerPtr());

		if (pGuild)
		{
			if (s_pDummyGuild && s_pDummyGuild->ownerID != pGuild->iContainerID)
				StructDestroyNoConstSafe(parse_GroupProjectContainer, &s_pDummyGuild);

			if (!s_pDummyGuild)
			{
				s_pDummyGuild = StructCreateNoConst(parse_GroupProjectContainer);
				s_pDummyGuild->ownerID = pGuild->iContainerID;
			}

			for (i = eaSize(&s_pDummyGuild->projectList) - 1; i >= 0; i--)
			{
				if (GET_REF(s_pDummyGuild->projectList[i]->projectDef) == pGroupProjectDef)
				{
					pState = CONTAINER_RECONST(GroupProjectState, s_pDummyGuild->projectList[i]);
					break;
				}
			}

			if (!pState)
			{
				NOCONST(GroupProjectState) *pNewState = StructCreateNoConst(parse_GroupProjectState);
				SET_HANDLE_FROM_REFERENT("GroupProjectDef", pGroupProjectDef, pNewState->projectDef);
				eaPush(&s_pDummyGuild->projectList, pNewState);
				pState = CONTAINER_RECONST(GroupProjectState, pNewState);
			}
		}
	}
	else if (!pState && eProjectType == GroupProjectType_Player)
	{
		Entity *pPlayer = entActivePlayerPtr();

		if (pPlayer)
		{
			if (s_pDummyPlayer && s_pDummyPlayer->ownerID != entGetContainerID(pPlayer))
				StructDestroyNoConstSafe(parse_GroupProjectContainer, &s_pDummyPlayer);

			if (!s_pDummyPlayer)
			{
				s_pDummyPlayer = StructCreateNoConst(parse_GroupProjectContainer);
				s_pDummyPlayer->ownerID = entGetContainerID(pPlayer);
			}

			for (i = eaSize(&s_pDummyPlayer->projectList) - 1; i >= 0; i--)
			{
				if (GET_REF(s_pDummyPlayer->projectList[i]->projectDef) == pGroupProjectDef)
				{
					pState = CONTAINER_RECONST(GroupProjectState, s_pDummyPlayer->projectList[i]);
					break;
				}
			}

			if (!pState)
			{
				NOCONST(GroupProjectState) *pNewState = StructCreateNoConst(parse_GroupProjectState);
				SET_HANDLE_FROM_REFERENT("GroupProjectDef", pGroupProjectDef, pNewState->projectDef);
				eaPush(&s_pDummyPlayer->projectList, pNewState);
				pState = CONTAINER_RECONST(GroupProjectState, pNewState);
			}
		}
	}

	return pState;
}

int gclGroupProject_GroupProjectDonor_SortName(const GroupProjectDonorUI **ppDonorL, const GroupProjectDonorUI **ppDonorR, const void *pContext)
{
	return stricmp_safe((*ppDonorL)->pchDisplayName, (*ppDonorR)->pchDisplayName);
}

int gclGroupProject_GroupProject_SortName(const GroupProjectUI **ppGroupProjectL, const GroupProjectUI **ppGroupProjectR, const void *pContext)
{
	return stricmp_safe((*ppGroupProjectL)->pchDisplayName, (*ppGroupProjectR)->pchDisplayName);
}

int gclGroupProject_GroupProjectDonor_SortContribution(const GroupProjectDonorUI **ppDonorL, const GroupProjectDonorUI **ppDonorR, const void *pContext)
{
	return (*ppDonorR)->uContribution - (*ppDonorL)->uContribution;
}

int gclGroupProject_GroupProjectUnlock_SortInternalName(const GroupProjectUnlockUI **ppUnlockL, const GroupProjectUnlockUI **ppUnlockR, const void *pContext)
{
	GroupProjectUnlockDef *pUnlockL = GET_REF((*ppUnlockL)->hGroupProjectUnlock);
	GroupProjectUnlockDef *pUnlockR = GET_REF((*ppUnlockR)->hGroupProjectUnlock);
	const char *pchUnlockL = pUnlockL ? pUnlockL->name : REF_STRING_FROM_HANDLE((*ppUnlockL)->hGroupProjectUnlock);
	const char *pchUnlockR = pUnlockR ? pUnlockR->name : REF_STRING_FROM_HANDLE((*ppUnlockR)->hGroupProjectUnlock);
	return stricmp_safe(pchUnlockL, pchUnlockR);
}

int gclGroupProject_DonationTask_SortCategoryName(const DonationTaskUI **ppTaskL, const DonationTaskUI **ppTaskR, const DonationTaskFilter *pContext)
{
	DonationTaskDef *pTaskL = GET_REF((*ppTaskL)->hDonationTask);
	DonationTaskDef *pTaskR = GET_REF((*ppTaskR)->hDonationTask);

	if (pContext && pContext->bSortByCategories)
	{
		if ((*ppTaskL)->eCategory != (*ppTaskR)->eCategory)
			return (*ppTaskL)->eCategory - (*ppTaskR)->eCategory;
	}

	if (pContext && pContext->bAddCategoryHeaders)
	{
		int bHeaderL = (pTaskL == NULL);
		int bHeaderR = (pTaskR == NULL);
		if (bHeaderL != bHeaderR)
			return bHeaderR - bHeaderL;
	}

	return stricmp_safe((*ppTaskL)->pchDisplayName, (*ppTaskR)->pchDisplayName);
}

int gclGroupProject_GroupProjectNumeric_SortName(const GroupProjectNumericUI **ppNumericL, const GroupProjectNumericUI  **ppNumericR, const void *pContext)
{
	return stricmp_safe((*ppNumericL)->pchDisplayName, (*ppNumericR)->pchDisplayName);
}

void gclGroupProjectUI_UpdateTask(GroupProjectDef *pGroupProject, DonationTaskUI *pDonationUI, DonationTaskDef *pDef, DonationTaskSlot *pSlot)
{
	if (pSlot && GET_REF(pSlot->taskDef) != pDef)
		pSlot = NULL;

	pDonationUI->pchDisplayName = TranslateDisplayMessage(pDef->displayNameMsg);
	pDonationUI->pchDescription = TranslateDisplayMessage(pDef->descriptionMsg);
	pDonationUI->pchTooltip = TranslateDisplayMessage(pDef->tooltipMsg);
	if (GET_REF(pDonationUI->hDonationTask) != pDef)
		SET_HANDLE_FROM_REFERENT("DonationTaskDef", pDef, pDonationUI->hDonationTask);
	pDonationUI->iBucketCount = eaSize(&pDef->buckets);
	pDonationUI->pchImage = pDef->iconName;
	pDonationUI->eCategory = pDef->category;

	pDonationUI->iCurrentBucketQuantity = 0;
	pDonationUI->iTotalBucketQuantity = 0;
	pDonationUI->iTotalCompletionTime = pDef->secondsToComplete;
	pDonationUI->iRemainingCompletionTime = pDef->secondsToComplete;
	pDonationUI->bRepeatable = pDef->repeatable;

	if (pSlot)
	{
		GroupProject_UpdateBucketQuantities(pGroupProject, pDef, pSlot, &pDonationUI->iCurrentBucketQuantity, &pDonationUI->iTotalBucketQuantity);

		switch (pSlot->state)
		{
		xcase DonationTaskState_AcceptingDonations:
			{
				pDonationUI->bCanFillBucket = true;
				pDonationUI->bBucketsFilled = false;
				pDonationUI->bCanSlot = false;
				pDonationUI->bAlreadyCompleted = false; //?
				pDonationUI->bRewardPending = false;
			}
		xcase DonationTaskState_Finalized:
			{
				pDonationUI->iRemainingCompletionTime = MAX(0, pSlot->completionTime - timeServerSecondsSince2000());
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = true;
				pDonationUI->bCanSlot = false;
				pDonationUI->bAlreadyCompleted = false; //?
				pDonationUI->bRewardPending = false;
			}
		xcase DonationTaskState_Canceled:
			{
				pDonationUI->iRemainingCompletionTime = MAX(0, pSlot->completionTime - timeServerSecondsSince2000());
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = true;
				pDonationUI->bCanSlot = false;
				pDonationUI->bAlreadyCompleted = false;
				pDonationUI->bRewardPending = false;
			}
		xcase DonationTaskState_Completed:
			{
				pDonationUI->iRemainingCompletionTime = 0;
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = true;
				pDonationUI->bAlreadyCompleted = true;
				pDonationUI->bRewardPending = false;
			}
		xcase DonationTaskState_RewardPending:
			{
				pDonationUI->iRemainingCompletionTime = 0;
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = true;
				pDonationUI->bAlreadyCompleted = true;
				pDonationUI->bRewardPending = true;
			}
		xcase DonationTaskState_RewardClaimed:
			{
				pDonationUI->iRemainingCompletionTime = 0;
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = true;
				pDonationUI->bAlreadyCompleted = true;
				pDonationUI->bRewardPending = false;
			}
		xcase DonationTaskState_None:
			{
				pDonationUI->bCanFillBucket = false;
				pDonationUI->bBucketsFilled = false;
				pDonationUI->bCanSlot = false;
				pDonationUI->bAlreadyCompleted = false;
				pDonationUI->bRewardPending = false;
			}
		}
	}
	else
	{
		pDonationUI->bCanFillBucket = false;
		pDonationUI->bBucketsFilled = false;

		// TODO: determine if this can be slotted
		pDonationUI->bCanSlot = true;
		pDonationUI->bAlreadyCompleted = false;
		pDonationUI->bRewardPending = false;
	}
}

void gclGroupProjectUI_UpdateTaskCategoryHeader(GroupProjectDef *pGroupProject, DonationTaskUI *pDonationUI, DonationTaskCategoryType eCategory)
{
	pDonationUI->pchDisplayName = StaticDefineGetTranslatedMessage(DonationTaskCategoryTypeEnum, eCategory);
	pDonationUI->pchDescription = NULL;
	pDonationUI->pchTooltip = NULL;
	if (IS_HANDLE_ACTIVE(pDonationUI->hDonationTask))
		REMOVE_HANDLE(pDonationUI->hDonationTask);
	pDonationUI->iBucketCount = 0;
	pDonationUI->pchImage = NULL;
	pDonationUI->eCategory = eCategory;

	pDonationUI->iCurrentBucketQuantity = 0;
	pDonationUI->iTotalBucketQuantity = 0;
	pDonationUI->iTotalCompletionTime = 0;
	pDonationUI->iRemainingCompletionTime = 0;
	pDonationUI->bRepeatable = false;

	pDonationUI->bCanFillBucket = false;
	pDonationUI->bBucketsFilled = false;
	pDonationUI->bCanSlot = false;
	pDonationUI->bAlreadyCompleted = false;
}

void gclGroupProject_GetBucketParams(const char *pchBucketKey, GroupProjectState **ppState, DonationTaskSlot **ppSlot, GroupProjectDonationRequirement **ppTaskBucket, DonationTaskBucketData **ppFilledBucket)
{
	static char s_achProjectName[1024];
	static char s_achBucketName[1024];
	GroupProjectState *pState;
	DonationTaskSlot *pSlot;
	GroupProjectDonationRequirement *pTaskBucket;
	DonationTaskBucketData *pFilledBucket;
	DonationTaskDef *pTask;
	U32 uProjectType;
	S32 i;
	U32 iSlot;

	if (!ppState)
		ppState = &pState;
	if (!ppSlot)
		ppSlot = &pSlot;
	if (!ppTaskBucket)
		ppTaskBucket = &pTaskBucket;
	if (!ppFilledBucket)
		ppFilledBucket = &pFilledBucket;

	*ppState = NULL;
	*ppSlot = NULL;
	*ppTaskBucket = NULL;
	*ppFilledBucket = NULL;

	if (!pchBucketKey || !*pchBucketKey)
		return;

	if (sscanf_s(pchBucketKey, "%d %u %s %s", &uProjectType, &iSlot, SAFESTR(s_achProjectName), SAFESTR(s_achBucketName)) != 4)
		return;

	*ppState = gclGroupProject_ResolveState(uProjectType, s_achProjectName);
	if (!*ppState)
		return;

	for (i = eaSize(&(*ppState)->taskSlots) - 1; i >= 0; i--)
	{
		if ((*ppState)->taskSlots[i]->taskSlotNum == iSlot)
		{
			*ppSlot = (*ppState)->taskSlots[i];
			break;
		}
	}

	pTask = *ppSlot ? GET_REF((*ppSlot)->taskDef) : NULL;
	for (i = pTask ? eaSize(&pTask->buckets) - 1 : -1; i >= 0; i--)
	{
		if (!stricmp(pTask->buckets[i]->name, s_achBucketName))
		{
			*ppTaskBucket = pTask->buckets[i];
			break;
		}
	}

	for (i = *ppSlot ? eaSize(&(*ppSlot)->buckets) - 1 : -1; i >= 0; i--)
	{
		if (!stricmp((*ppSlot)->buckets[i]->bucketName, pchBucketKey))
		{
			*ppFilledBucket = (*ppSlot)->buckets[i];
			break;
		}
	}
}

void gclGroupProject_UpdateBucket(Entity *pEnt, DonationTaskBucketUI *pBucketUI, GroupProjectState *pState, DonationTaskSlot *pSlot, DonationTaskDef *pTask, GroupProjectDonationRequirement *pTaskBucket, DonationTaskBucketData *pFilledBucket)
{
	const S32 s_iUpdateInterval = 1000;
	static S32 s_iScatter;
	static char s_achKey[1024];
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	bool bRefreshSlow = !pBucketUI->uAvailableItemsUpdate || (S32)(gGCLState.totalElapsedTimeMs - pBucketUI->uAvailableItemsUpdate) > s_iUpdateInterval;

	if (bRefreshSlow)
		pBucketUI->uAvailableItemsUpdate = gGCLState.totalElapsedTimeMs + (s_iScatter++ * 100) % s_iUpdateInterval;

	SET_HANDLE_FROM_REFERENT("DonationTaskDef", pTask, pBucketUI->hDonationTask);
	pBucketUI->pchBucketName = pTaskBucket->name;

	pBucketUI->pchDisplayName = TranslateDisplayMessage(pTaskBucket->displayNameMsg);
	pBucketUI->pchDescription = TranslateDisplayMessage(pTaskBucket->descriptionMsg);
	pBucketUI->pchTooltip = TranslateDisplayMessage(pTaskBucket->tooltipMsg);
	pBucketUI->pchImage = pTaskBucket->iconName;

	if (pTaskBucket->specType == DonationSpecType_Item && GET_REF(pTaskBucket->requiredItem))
	{
		ItemDef *pItemDef = GET_REF(pTaskBucket->requiredItem);
		if (!pBucketUI->pchDisplayName)
			pBucketUI->pchDisplayName = TranslateDisplayMessage(pItemDef->displayNameMsg);
		if (!pBucketUI->pchDescription)
			pBucketUI->pchDescription = TranslateDisplayMessage(pItemDef->descriptionMsg);
		if (!pBucketUI->pchImage)
		{
			pBucketUI->pchImage = item_GetIconName(NULL, pItemDef);
			if (pItemDef->eType == kItemType_Numeric)
				pBucketUI->pchImage = gclGetBestIconName(pBucketUI->pchImage, "default_item_icon");
		}
	}

	if (GET_REF(pBucketUI->hItemDef) != GET_REF(pTaskBucket->requiredItem))
		COPY_HANDLE(pBucketUI->hItemDef, pTaskBucket->requiredItem);

	pBucketUI->iCurrentQuantity = SAFE_MEMBER(pFilledBucket, donationCount);
	pBucketUI->iRequiredQuantity = pTaskBucket->count;

	if (pTaskBucket->specType == DonationSpecType_Item && pTaskBucket->donationIncrement > 0)
		pBucketUI->iIncrementQuantity = pTaskBucket->donationIncrement;
	else
		pBucketUI->iIncrementQuantity = 1;

	pBucketUI->iOpenDonation = (pBucketUI->iRequiredQuantity - pBucketUI->iCurrentQuantity) / pBucketUI->iIncrementQuantity;

	if (pGroupProject && pSlot)
	{
		sprintf(s_achKey, "%d %d %s %s", pGroupProject->type, pSlot->taskSlotNum, pGroupProject->name, pTaskBucket->name);
		if (!pBucketUI->pchBucketKey || stricmp(pBucketUI->pchBucketKey, s_achKey))
			StructCopyString(&pBucketUI->pchBucketKey, s_achKey);
	}
	else if (pBucketUI->pchBucketKey)
	{
		StructFreeStringSafe(&pBucketUI->pchBucketKey);
	}

	switch (pTaskBucket->specType)
	{
	xcase DonationSpecType_Item:
		{
			ItemDef *pItemDef = GET_REF(pBucketUI->hItemDef);
			if (!pItemDef)
				pBucketUI->iMaximumDonation = 0;
			else if (pItemDef->eType == kItemType_Numeric)
			{
				S32 iItemValue = inv_GetNumericItemValue(pEnt, pItemDef->pchName);
				if (IS_HANDLE_ACTIVE(pItemDef->hSpendingNumeric))
				{
					ItemDef *pSpendItem = GET_REF(pItemDef->hSpendingNumeric);
					iItemValue -= inv_GetNumericItemValue(pEnt, pSpendItem ? pSpendItem->pchName : REF_STRING_FROM_HANDLE(pItemDef->hSpendingNumeric));
				}

				pBucketUI->iMaximumDonation = iItemValue / pBucketUI->iIncrementQuantity;

				if (pItemDef->fScaleUI != 0 && pItemDef->fScaleUI != 1)
				{
					pBucketUI->iCurrentQuantity *= pItemDef->fScaleUI;
					pBucketUI->iRequiredQuantity *= pItemDef->fScaleUI;
				}
			}
			else if (bRefreshSlow)
				pBucketUI->iMaximumDonation = item_CountOwned(pEnt, pItemDef) / pBucketUI->iIncrementQuantity;
		}
	xcase DonationSpecType_Expression:
		{
			if (bRefreshSlow && pEnt && pEnt->pInventoryV2)
			{
				S32 i, j, iCount = 0;
				UIInventoryKey Key = {0};
				bool bStackable = false;

				for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags) - 1; i >= 0; i--)
				{
					InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];

					gclInventoryUpdateBag(pEnt, pBag);

					for (j = eaSize(&pBag->ppIndexedInventorySlots) - 1; j >= 0; j--)
					{
						InventorySlot *pInvSlot = gclInventoryUpdateSlot(pEnt, pBag, pBag->ppIndexedInventorySlots[j]);
						char *pchOldKey;
						const char *pchNewKey;
						ItemDef *pItemDef;

						if (!pInvSlot->pItem)
							continue;

						pItemDef = GET_REF(pInvSlot->pItem->hItem);
						if (!pItemDef)
							continue;

                        if ( !DonationTask_ItemMatchesExpressionRequirement(pEnt, pTaskBucket, pInvSlot->pItem) )
							continue;

						if (!item_CanRemoveItem(pInvSlot->pItem))
							continue;

						if (pItemDef->iStackLimit > 1)
						{
							bStackable = true;
						}

						// Make the inventory key
						gclInventoryMakeSlotKey(pEnt, pBag, pInvSlot, &Key);
						pchOldKey = eaGet(&pBucketUI->eachAvailableItemKeys, iCount);
						pchNewKey = gclInventoryMakeKeyString(NULL, &Key);

						// Add inventory key, this does not take deletions well
						if (!pchOldKey && pchNewKey && *pchNewKey)
							eaSet(&pBucketUI->eachAvailableItemKeys, StructAllocString(pchNewKey), iCount++);
						else if (pchOldKey && pchNewKey && *pchNewKey && stricmp(pchOldKey, pchNewKey))
							eaInsert(&pBucketUI->eachAvailableItemKeys, StructAllocString(pchNewKey), iCount++);
						else
							iCount++;
					}
				}

				// Set the size of the array
				while (eaSize(&pBucketUI->eachAvailableItemKeys) > iCount)
					StructFreeString(eaPop(&pBucketUI->eachAvailableItemKeys));

				// Support the item queue if items are not stackable
				pBucketUI->bCanQueueItems = !bStackable;
				if (pBucketUI->bCanQueueItems)
				{
					// Validate the queued items
					for (i = eaSize(&pBucketUI->eachQueuedItemKeys) - 1; i >= 0; i--)
					{
						// Find the queued key
						for (j = eaSize(&pBucketUI->eachAvailableItemKeys) - 1; j >= 0; j--)
						{
							if (!stricmp(pBucketUI->eachAvailableItemKeys[j], pBucketUI->eachQueuedItemKeys[i]))
								break;
						}

						// The queued item is no longer valid
						if (j < 0)
							StructFreeString(eaRemove(&pBucketUI->eachQueuedItemKeys, i));
					}
					pBucketUI->iQueuedItemCount = eaSize(&pBucketUI->eachQueuedItemKeys);
				}
				else
				{
					// None of the items may be queued
					while (eaSize(&pBucketUI->eachQueuedItemKeys) > 0)
						StructFreeString(eaPop(&pBucketUI->eachQueuedItemKeys));
					pBucketUI->iQueuedItemCount = 0;
				}
			}
			pBucketUI->iMaximumDonation = eaSize(&pBucketUI->eachAvailableItemKeys);
		}
	xdefault:
		{
			pBucketUI->iMaximumDonation = 0;
		}
	}

	pBucketUI->bFilled = pBucketUI->iCurrentQuantity >= pBucketUI->iRequiredQuantity;
	pBucketUI->bCanFillBucket = !pBucketUI->bFilled && pBucketUI->iMaximumDonation > 0;
}

void gclGroupProjectUI_UpdateGroupProject(GroupProjectUI *pGroupProjectUI, GroupProjectDef *pGroupProject, GroupProjectState *pState)
{
	pGroupProjectUI->pchDisplayName = TranslateDisplayMessage(pGroupProject->displayNameMsg);
	pGroupProjectUI->pchDescription = TranslateDisplayMessage(pGroupProject->descriptionMsg);
	pGroupProjectUI->pchGroupName = pGroupProject->name;
	pGroupProjectUI->pchImage = pGroupProject->iconName;
}

void gclGroupProjectUI_UpdateNumeric(GroupProjectNumericUI *pNumericUI, GroupProjectNumericDef *pNumeric, S32 iAdjust, GroupProjectState *pState)
{
	if (GET_REF(pNumericUI->hGroupProjectNumeric) != pNumeric)
		SET_HANDLE_FROM_REFERENT("GroupProjectNumericDef", pNumeric, pNumericUI->hGroupProjectNumeric);
	pNumericUI->pchDisplayName = TranslateDisplayMessage(pNumeric->displayNameMsg);
	pNumericUI->pchDescription = TranslateDisplayMessage(pNumeric->tooltipMsg);
	pNumericUI->pchTooltip = TranslateDisplayMessage(pNumeric->tooltipMsg);
	pNumericUI->pchImage = pNumeric->iconName;
	pNumericUI->iAdjustValue = iAdjust;
	pNumericUI->iMaximumValue = pNumeric->maxValue;

	if (pState)
	{
		GroupProjectDef *pGroupProject = GET_REF(pState->projectDef);
		S32 i;

		pNumericUI->iCurrentValue = 0;
		for (i = eaSize(&pState->numericData) - 1; i >= 0; i--)
		{
			if (GET_REF(pState->numericData[i]->numericDef) == pNumeric)
			{
				pNumericUI->iCurrentValue = pState->numericData[i]->numericVal;
				break;
			}
		}

		if (pGroupProject)
		{
			pNumericUI->eProjectType = pGroupProject->type;
			if (GET_REF(pNumericUI->hGroupName) != pGroupProject)
				SET_HANDLE_FROM_REFERENT("GroupProjectDef", pGroupProject, pNumericUI->hGroupName);
		}
		else
		{
			pNumericUI->eProjectType = GroupProjectType_None;
			if (IS_HANDLE_ACTIVE(pNumericUI->hGroupName))
				REMOVE_HANDLE(pNumericUI->hGroupName);
		}
	}
	else
	{
		pNumericUI->iCurrentValue = 0;
		pNumericUI->eProjectType = GroupProjectType_None;
		if (IS_HANDLE_ACTIVE(pNumericUI->hGroupName))
			REMOVE_HANDLE(pNumericUI->hGroupName);
	}
}

bool gclGroupProjectUI_IsUnlocked(GroupProjectUnlockDef *pUnlock, GroupProjectState *pState)
{
	S32 i, n;

	if (!pUnlock || !pState)
		return false;

	switch (pUnlock->type)
	{
	xcase UnlockType_Manual:
		{
			n = pState ? eaSize(&pState->unlocks) : 0;
			for (i = n - 1; i >= 0; i--)
			{
				if (GET_REF(pState->unlocks[i]->unlockDef) == pUnlock)
					break;
			}
			return (i >= 0);
		}
	xcase UnlockType_NumericValueEqualOrGreater:
		{
			GroupProjectNumericDef *pNumeric = GET_REF(pUnlock->numeric);
			if (!pNumeric)
				break;

			n = pState ? eaSize(&pState->numericData) : 0;
			for (i = n - 1; i >= 0; i--)
			{
				if (GET_REF(pState->numericData[i]->numericDef) == pNumeric)
					break;
			}
			return (i >= 0) ? pState->numericData[i]->numericVal >= pUnlock->triggerValue : false;
		}
	}

	return false;
}

void gclGroupProjectUI_UpdateUnlock(GroupProjectUnlockUI *pUnlockUI, GroupProjectUnlockDef *pUnlock, GroupProjectState *pState)
{
	S32 i, n;

	if (GET_REF(pUnlockUI->hGroupProjectUnlock) != pUnlock)
		SET_HANDLE_FROM_REFERENT("GroupProjectUnlockDef", pUnlock, pUnlockUI->hGroupProjectUnlock);
	pUnlockUI->pchDisplayName = TranslateDisplayMessage(pUnlock->displayNameMsg);
	pUnlockUI->pchDescription = TranslateDisplayMessage(pUnlock->descriptionMsg);
	pUnlockUI->pchTooltip = TranslateDisplayMessage(pUnlock->tooltipMsg);
	pUnlockUI->pchImage = pUnlock->iconName;

	switch (pUnlock->type)
	{
	xcase UnlockType_Manual:
		{
			if (pUnlockUI->pUnlockNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pUnlockUI->pUnlockNumeric);
			pUnlockUI->iUnlockReferenceValue = 0;
			pUnlockUI->bUnlockManual = true;
			pUnlockUI->bUnlockNumericGreaterEqual = false;

			n = pState ? eaSize(&pState->unlocks) : 0;
			for (i = n - 1; i >= 0; i--)
			{
				if (GET_REF(pState->unlocks[i]->unlockDef) == pUnlock)
					break;
			}
			pUnlockUI->bUnlocked = (i >= 0);
		}
	xcase UnlockType_NumericValueEqualOrGreater:
		{
			GroupProjectNumericDef *pNumeric = GET_REF(pUnlock->numeric);
			if (pNumeric && !pUnlockUI->pUnlockNumeric)
				pUnlockUI->pUnlockNumeric = StructCreate(parse_GroupProjectNumericUI);
			if (pUnlockUI->pUnlockNumeric)
				gclGroupProjectUI_UpdateNumeric(pUnlockUI->pUnlockNumeric, pNumeric, 0, pState);
			else if (pUnlockUI->pUnlockNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pUnlockUI->pUnlockNumeric);
			pUnlockUI->iUnlockReferenceValue = pUnlock->triggerValue;
			pUnlockUI->bUnlockManual = false;
			pUnlockUI->bUnlockNumericGreaterEqual = true;

			n = pState ? eaSize(&pState->numericData) : 0;
			for (i = n - 1; i >= 0; i--)
			{
				if (GET_REF(pState->numericData[i]->numericDef) == pNumeric)
					break;
			}
			pUnlockUI->bUnlocked = (i >= 0) ? pState->numericData[i]->numericVal >= pUnlock->triggerValue : false;
		}
	xdefault:
		{
			if (pUnlockUI->pUnlockNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pUnlockUI->pUnlockNumeric);
			pUnlockUI->iUnlockReferenceValue = 0;
			pUnlockUI->bUnlockManual = false;
			pUnlockUI->bUnlockNumericGreaterEqual = false;
			pUnlockUI->bUnlocked = false;
		}
	}
}

void gclGroupProjectUI_UpdateReward(DonationTaskRewardUI *pRewardUI, DonationTaskReward *pReward, GroupProjectState *pState)
{
	pRewardUI->pItem = NULL;

	switch (pReward->rewardType)
	{
	xcase DonationTaskRewardType_NumericAdd:
		{
			GroupProjectNumericDef *pNumeric = GET_REF(pReward->numericDef);
			GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;

			if (pRewardUI->pUnlock)
				StructDestroySafe(parse_GroupProjectUnlockUI, &pRewardUI->pUnlock);
			if (pNumeric && !pRewardUI->pNumeric)
				pRewardUI->pNumeric = StructCreate(parse_GroupProjectNumericUI);
			else if (!pNumeric && pRewardUI->pNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pRewardUI->pNumeric);

			if (pRewardUI->pNumeric)
			{
				GroupProjectConstant *pConstant = pGroupProject ? eaGet(&pGroupProject->constants, GroupProject_FindConstant(pGroupProject, pReward->rewardConstant)) : NULL;
				gclGroupProjectUI_UpdateNumeric(pRewardUI->pNumeric, pNumeric, pConstant ? pConstant->value : 0, pState);
			}

			pRewardUI->bNumericSet = false;
		}
	xcase DonationTaskRewardType_NumericSet:
		{
			GroupProjectNumericDef *pNumeric = GET_REF(pReward->numericDef);
			GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;

			if (pRewardUI->pUnlock)
				StructDestroySafe(parse_GroupProjectUnlockUI, &pRewardUI->pUnlock);
			if (pNumeric && !pRewardUI->pNumeric)
				pRewardUI->pNumeric = StructCreate(parse_GroupProjectNumericUI);
			else if (!pNumeric && pRewardUI->pNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pRewardUI->pNumeric);

			if (pRewardUI->pNumeric)
			{
				GroupProjectConstant *pConstant = pGroupProject ? eaGet(&pGroupProject->constants, GroupProject_FindConstant(pGroupProject, pReward->rewardConstant)) : NULL;
				gclGroupProjectUI_UpdateNumeric(pRewardUI->pNumeric, pNumeric, pConstant ? pConstant->value : 0, pState);
			}

			pRewardUI->bNumericSet = true;
		}
	xcase DonationTaskRewardType_Unlock:
		{
			GroupProjectUnlockDef *pUnlock = GET_REF(pReward->unlockDef);

			if (pRewardUI->pNumeric)
				StructDestroySafe(parse_GroupProjectNumericUI, &pRewardUI->pNumeric);
			if (pUnlock && !pRewardUI->pUnlock)
				pRewardUI->pUnlock = StructCreate(parse_GroupProjectUnlockUI);
			else if (!pUnlock && pRewardUI->pUnlock)
				StructDestroySafe(parse_GroupProjectUnlockUI, &pRewardUI->pUnlock);

			if (pRewardUI->pUnlock)
				gclGroupProjectUI_UpdateUnlock(pRewardUI->pUnlock, pUnlock, pState);
		}
	}
}

void gclGroupProjectUI_UpdateRewardItem(DonationTaskRewardUI *pRewardUI, InventorySlot *pInventorySlot, GroupProjectState *pState)
{
	if (pRewardUI->pNumeric)
		StructDestroySafe(parse_GroupProjectNumericUI, &pRewardUI->pNumeric);
	if (pRewardUI->pUnlock)
		StructDestroySafe(parse_GroupProjectUnlockUI, &pRewardUI->pUnlock);

	pRewardUI->pItem = pInventorySlot ? pInventorySlot->pItem : NULL;
}

void gclGroupProjectUI_GetTaskList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, SA_PARAM_OP_VALID const DonationTaskFilter *pFilter, int slotNum)
{
	GroupProjectDef *pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;
	DonationTaskUI ***peaTaskUI = ui_GenGetManagedListSafe(pGen, DonationTaskUI);
	S32 i, j, iCount = 0;
    Entity *pEnt = entActivePlayerPtr();
	U32 eCategories = 0;

	if (pGroupProjectDef)
	{
		for (i = 0; i < eaSize(&pGroupProjectDef->donationTaskDefs); i++)
		{
			DonationTaskDef *pTask = GET_REF(pGroupProjectDef->donationTaskDefs[i]->taskDef);
			DonationTaskUI *pTaskUI = NULL;

			if (pTask == NULL)
				continue;

			// Evaluate whether the donation task is currently allowed.
			switch (pGroupProjectDef->type)
			{
			xcase GroupProjectType_Guild:
				if (!GuildProject_DonationTaskAllowed(pEnt, pGroupProjectDef->name, pTask, slotNum))
					continue;
			xcase GroupProjectType_Player:
				if (!PlayerProject_DonationTaskAllowed(pEnt, pGroupProjectDef->name, pTask, slotNum))
					continue;
			}

			if (pFilter)
			{
				if (pFilter->eSlotType != TaskSlotType_None && pTask->slotType != pFilter->eSlotType)
					continue;

				if (pFilter->pchNameFilter && *pFilter->pchNameFilter)
				{
					const char *pchName = TranslateDisplayMessage(pTask->displayNameMsg);
					if (pchName && !strstri(pchName, pFilter->pchNameFilter))
						continue;
				}
				if (pFilter->pchDescriptionFilter && *pFilter->pchDescriptionFilter)
				{
					const char *pchDescription = TranslateDisplayMessage(pTask->descriptionMsg);
					if (pchDescription && !strstri(pchDescription, pFilter->pchDescriptionFilter))
						continue;
				}
			}

			if (pTask->category < 32)
				eCategories |= 1 << pTask->category;
			if (pFilter && (pFilter->uCategoryMask & (1 << pTask->category)) != 0)
				continue;

			// Attempt to keep the task list instances stable
			for (j = iCount; j < eaSize(peaTaskUI); j++)
			{
				if (GET_REF((*peaTaskUI)[j]->hDonationTask) == pTask)
				{
					if (j != iCount)
						eaSwap(peaTaskUI, j, iCount);
					pTaskUI = (*peaTaskUI)[iCount++];
					break;
				}
			}
			if (!pTaskUI)
			{
				pTaskUI = StructCreate(parse_DonationTaskUI);
				eaInsert(peaTaskUI, pTaskUI, iCount++);
			}

			gclGroupProjectUI_UpdateTask(pGroupProjectDef, pTaskUI, pTask, NULL);
		}
	}

	for (i = 0; i < 32; i++)
	{
		DonationTaskUI *pTaskUI = NULL;

		if ((eCategories & (1 << i)) == 0)
			continue;

		// Attempt to keep the task list instances stable
		for (j = iCount; j < eaSize(peaTaskUI); j++)
		{
			if (!IS_HANDLE_ACTIVE((*peaTaskUI)[j]->hDonationTask) && (*peaTaskUI)[j]->eCategory == i)
			{
				if (j != iCount)
					eaSwap(peaTaskUI, j, iCount);
				pTaskUI = (*peaTaskUI)[iCount++];
				break;
			}
		}
		if (!pTaskUI)
		{
			pTaskUI = StructCreate(parse_DonationTaskUI);
			eaInsert(peaTaskUI, pTaskUI, iCount++);
		}

		gclGroupProjectUI_UpdateTaskCategoryHeader(pGroupProjectDef, pTaskUI, i);
	}

	eaSetSizeStruct(peaTaskUI, parse_DonationTaskUI, iCount);
	eaStableSort(*peaTaskUI, pFilter, gclGroupProject_DonationTask_SortCategoryName);
	ui_GenSetManagedListSafe(pGen, peaTaskUI, DonationTaskUI, true);
}

void gclGroupProjectUI_GetSlotList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, DonationSlotFilter *pFilter)
{
	GroupProjectDef *pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;
	DonationTaskSlotUI ***peaSlotUI = ui_GenGetManagedListSafe(pGen, DonationTaskSlotUI);
	U32 i;
	S32 j, iCount = 0;

	if (pState)
	{
		Entity *pPlayer = entActivePlayerPtr();
		for (i = 0; i < (U32)eaiSize(&pGroupProjectDef->slotTypes); i++)
		{
			DonationTaskSlot *pSlot = NULL;
			DonationTaskSlotUI *pSlotUI = NULL;

			for (j = eaSize(&pState->taskSlots) - 1; j >= 0; j--)
			{
				if (pState->taskSlots[j]->taskSlotNum == i)
				{
					pSlot = pState->taskSlots[j];
					break;
				}
			}

			if (pFilter)
			{
				if (pFilter->bFilled && (!pSlot || !IS_HANDLE_ACTIVE(pSlot->taskDef)))
					continue;
				if (i < pFilter->iFirstSlot || (pFilter->iLastSlot >= 0 && pFilter->iLastSlot < i))
					continue;
				if (pFilter->eSlotType != TaskSlotType_None && pFilter->eSlotType != pGroupProjectDef->slotTypes[i])
					continue;
			}

			// Attempt to keep the slot list instances stable
			for (j = iCount; j < eaSize(peaSlotUI); j++)
			{
				if ((U32)(*peaSlotUI)[j]->iSlot == i)
				{
					if (j != iCount)
						eaSwap(peaSlotUI, j, iCount);
					pSlotUI = (*peaSlotUI)[iCount++];
					break;
				}
			}
			if (!pSlotUI)
			{
				pSlotUI = StructCreate(parse_DonationTaskSlotUI);
				eaInsert(peaSlotUI, pSlotUI, iCount++);
			}

			pSlotUI->iSlot = i;
			pSlotUI->eSlotType = pGroupProjectDef->slotTypes[i];

			// Update the active task
			if (pSlotUI->pActiveTask && (!pSlot || !IS_HANDLE_ACTIVE(pSlot->taskDef)))
				StructDestroySafe(parse_DonationTaskUI, &pSlotUI->pActiveTask);
			else if (!pSlotUI->pActiveTask && (pSlot && IS_HANDLE_ACTIVE(pSlot->taskDef)))
				pSlotUI->pActiveTask = StructCreate(parse_DonationTaskUI);
			if (pSlot && GET_REF(pSlot->taskDef))
				gclGroupProjectUI_UpdateTask(pGroupProjectDef, pSlotUI->pActiveTask, GET_REF(pSlot->taskDef), pSlot);

			// Update the next task
			if (pSlotUI->pNextTask && (!pSlot || !IS_HANDLE_ACTIVE(pSlot->nextTaskDef)))
				StructDestroySafe(parse_DonationTaskUI, &pSlotUI->pNextTask);
			else if (!pSlotUI->pNextTask && (pSlot && IS_HANDLE_ACTIVE(pSlot->nextTaskDef)))
				pSlotUI->pNextTask = StructCreate(parse_DonationTaskUI);
			if (pSlot && GET_REF(pSlot->nextTaskDef))
				gclGroupProjectUI_UpdateTask(pGroupProjectDef, pSlotUI->pNextTask, GET_REF(pSlot->nextTaskDef), NULL);

			// Count available tasks
			pSlotUI->iAvailableTasks = 0;
			for (j = eaSize(&pGroupProjectDef->donationTaskDefs) - 1; j >= 0; j--)
			{
				DonationTaskDef *pTask = GET_REF(pGroupProjectDef->donationTaskDefs[j]->taskDef);
				if (!pTask || pTask->slotType != pSlotUI->eSlotType)
					continue;

				switch (pGroupProjectDef->type)
				{
				xcase GroupProjectType_Guild:
					if (!GuildProject_DonationTaskAllowed(pPlayer, pGroupProjectDef->name, pTask, i))
						continue;
				xcase GroupProjectType_Player:
					if (!PlayerProject_DonationTaskAllowed(pPlayer, pGroupProjectDef->name, pTask, i))
						continue;
				}

				pSlotUI->iAvailableTasks++;
			}
		}
	}

	eaSetSizeStruct(peaSlotUI, parse_DonationTaskSlotUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaSlotUI, DonationTaskSlotUI, true);
}

void gclGroupProjectUI_GetTaskBucketList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, DonationTaskDef *pTask, U32 iSlot)
{
	DonationTaskBucketUI ***peaBucketUI = ui_GenGetManagedListSafe(pGen, DonationTaskBucketUI);
	S32 i, j, iCount = 0;
	Entity *pEnt = entActivePlayerPtr();
	DonationTaskSlot *pSlot = NULL;

	// Find the active slot information
	if (pState)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (pState->taskSlots[i]->taskSlotNum == iSlot)
			{
				pSlot = pState->taskSlots[i];
				if (!pTask)
					pTask = GET_REF(pSlot->taskDef);
				break;
			}
		}
	}

	if (pTask)
	{
		for (i = 0; i < eaSize(&pTask->buckets); i++)
		{
			GroupProjectDonationRequirement *pTaskBucket = pTask->buckets[i];
			DonationTaskBucketData *pFilledBucket = NULL;
			DonationTaskBucketUI *pBucketUI = NULL;

			// Get slotted bucket information
			if (pSlot)
			{
				for (j = eaSize(&pSlot->buckets) - 1; j >= 0; j--)
				{
					if (pSlot->buckets[j]->bucketName == pTaskBucket->name)
					{
						pFilledBucket = pSlot->buckets[j];
						break;
					}
				}
			}

			// Attempt to keep the ui bucket instances stable
			for (j = iCount; j < eaSize(peaBucketUI); j++)
			{
				if (GET_REF((*peaBucketUI)[j]->hDonationTask) == pTask
					&& (*peaBucketUI)[j]->pchBucketName == pTaskBucket->name
					)
				{
					if (j != iCount)
						eaSwap(peaBucketUI, j, iCount);
					pBucketUI = (*peaBucketUI)[iCount++];
					break;
				}
			}
			if (!pBucketUI)
			{
				pBucketUI = StructCreate(parse_DonationTaskBucketUI);
				eaInsert(peaBucketUI, pBucketUI, iCount++);
			}

			// Determine available items
			gclGroupProject_UpdateBucket(pEnt, pBucketUI, pState, pSlot, pTask, pTaskBucket, pFilledBucket);
		}
	}

	eaSetSizeStruct(peaBucketUI, parse_DonationTaskBucketUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaBucketUI, DonationTaskBucketUI, true);
}

void gclGroupProjectUI_GetDonorList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, GroupProjectDonorFilter *pFilter)
{
	GroupProjectDonorUI ***peaDonorUI = ui_GenGetManagedListSafe(pGen, GroupProjectDonorUI);
	S32 i, j, iCount = 0;

	if (pState)
	{
		for (i = eaSize(&pState->donationStats) - 1; i >= 0; i--)
		{
			GroupProjectDonationStats *pDonor = pState->donationStats[i];
			GroupProjectDonorUI *pDonorUI = NULL;

			if (pFilter)
			{
				if (pFilter->pchNameFilter && *pFilter->pchNameFilter && !strstri(pDonor->displayName, pFilter->pchNameFilter))
					continue;
			}

			// Attempt to keep the ui bucket instances stable
			for (j = iCount; j < eaSize(peaDonorUI); j++)
			{
				if (strcmp((*peaDonorUI)[j]->pchDisplayName, pDonor->displayName) == 0)
				{
					if (j != iCount)
						eaSwap(peaDonorUI, j, iCount);
					pDonorUI = (*peaDonorUI)[iCount++];
					break;
				}
			}
			if (!pDonorUI)
			{
				pDonorUI = StructCreate(parse_GroupProjectDonorUI);
				eaInsert(peaDonorUI, pDonorUI, iCount++);
			}

			StructCopyString(&pDonorUI->pchDisplayName, pDonor->displayName);
			pDonorUI->uContribution = pDonor->contribution;
		}
	}

	eaSetSizeStruct(peaDonorUI, parse_GroupProjectDonorUI, iCount);
	if (pFilter && pFilter->bSortName)
		eaStableSort((*peaDonorUI), NULL, gclGroupProject_GroupProjectDonor_SortName);
	else
		eaStableSort((*peaDonorUI), NULL, gclGroupProject_GroupProjectDonor_SortContribution);
	ui_GenSetManagedListSafe(pGen, peaDonorUI, GroupProjectDonorUI, true);
}

void gclGroupProjectUI_GetGroupProjectList(GroupProjectUI ***peaGroupProjectUI, GroupProjectFilter *pFilter)
{
	S32 j, iCount = 0;

	FOR_EACH_IN_REFDICT("GroupProjectDef", GroupProjectDef, pGroupProject);
	{
		GroupProjectUI *pGroupProjectUI = NULL;

		if (pFilter)
		{
			const char *pchName;
			if (pGroupProject->type != pFilter->eProjectType)
				continue;
			pchName = TranslateDisplayMessage(pGroupProject->displayNameMsg);
			if (pFilter->pchNameFilter && *pFilter->pchNameFilter && (!pchName || strstri(pchName, pFilter->pchNameFilter)))
				continue;
		}

		// Attempt to keep the ui bucket instances stable
		for (j = iCount; j < eaSize(peaGroupProjectUI); j++)
		{
			if ((*peaGroupProjectUI)[j]->pchGroupName == pGroupProject->name)
			{
				if (j != iCount)
					eaSwap(peaGroupProjectUI, j, iCount);
				pGroupProjectUI = (*peaGroupProjectUI)[iCount++];
				break;
			}
		}
		if (!pGroupProjectUI)
		{
			pGroupProjectUI = StructCreate(parse_GroupProjectUI);
			eaInsert(peaGroupProjectUI, pGroupProjectUI, iCount++);
		}

		gclGroupProjectUI_UpdateGroupProject(pGroupProjectUI, pGroupProject, gclGroupProject_ResolveState(pGroupProject->type, pGroupProject->name));
	}
	FOR_EACH_END;

	eaSetSizeStruct(peaGroupProjectUI, parse_GroupProjectUI, iCount);
	eaStableSort((*peaGroupProjectUI), NULL, gclGroupProject_GroupProject_SortName);
}

GroupProjectUI *gclGroupProjectUI_GetGroupProject(SA_PARAM_OP_VALID UIGen *pGen, GroupProjectDef *pGroupProject)
{
	static GroupProjectUI s_GroupProject;
	GroupProjectUI *pGroupProjectUI = &s_GroupProject;

	if (pGen)
		pGroupProjectUI = ui_GenGetManagedPointer(pGen, parse_GroupProjectUI);

	if (pGroupProject)
		gclGroupProjectUI_UpdateGroupProject(pGroupProjectUI, pGroupProject, gclGroupProject_ResolveState(pGroupProject->type, pGroupProject->name));
	else if (pGroupProjectUI->pchGroupName)
		StructReset(parse_GroupProjectUI, pGroupProjectUI);

	if (pGen)
		ui_GenSetManagedPointer(pGen, pGroupProjectUI, parse_GroupProjectUI, true);
	return pGroupProjectUI;
}

DonationTaskBucketUI *gclGroupProjectUI_GetDonationTaskBucket(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, SA_PARAM_OP_VALID DonationTaskSlot *pSlot, SA_PARAM_OP_VALID DonationTaskDef *pTask, const char *pchBucketName)
{
	static DonationTaskBucketUI s_Bucket;
	DonationTaskBucketUI *pBucketUI = &s_Bucket;
	GroupProjectDonationRequirement *pTaskBucket = NULL;
	DonationTaskBucketData *pFilledBucket = NULL;
	S32 i;

	if (pGen)
		pBucketUI = ui_GenGetManagedPointer(pGen, parse_DonationTaskBucketUI);

	if (pSlot && !pTask)
	{
		pTask = GET_REF(pSlot->taskDef);
	}

	if (pTask && pchBucketName && *pchBucketName)
	{
		for (i = eaSize(&pTask->buckets) - 1; i >= 0; i--)
		{
			if (!stricmp(pTask->buckets[i]->name, pchBucketName))
			{
				pTaskBucket = pTask->buckets[i];
				break;
			}
		}
	}
	if (!pSlot && pState && pchBucketName && *pchBucketName)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (GET_REF(pState->taskSlots[i]->taskDef) == pTask)
			{
				pSlot = pState->taskSlots[i];
				break;
			}
		}
	}
	if (pSlot)
	{
		for (i = eaSize(&pSlot->buckets) - 1; i >= 0; i--)
		{
			if (!stricmp(pSlot->buckets[i]->bucketName, pchBucketName))
			{
				pFilledBucket = pSlot->buckets[i];
				break;
			}
		}
	}

	if (pTaskBucket)
		gclGroupProject_UpdateBucket(entActivePlayerPtr(), pBucketUI, pState, pSlot, pTask, pTaskBucket, pFilledBucket);
	else if (pBucketUI->pchBucketName)
		StructReset(parse_DonationTaskBucketUI, pBucketUI);

	if (pGen)
		ui_GenSetManagedPointer(pGen, pBucketUI, parse_DonationTaskBucketUI, true);
	return pBucketUI;
}

void gclGroupProjectUI_GetUnlockList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, GroupProjectUnlockFilter *pFilter)
{
	GroupProjectUnlockUI ***peaUnlockUI = ui_GenGetManagedListSafe(pGen, GroupProjectUnlockUI);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	S32 i, j, iCount = 0;

	if (pGroupProject)
	{
		static const char *s_pchDelims = " \r\n\t,%|";
		char *pchNameFilter = pFilter && pFilter->pchNameFilter && strspn(pFilter->pchNameFilter, s_pchDelims) != strcspn(pFilter->pchNameFilter, "") ? (char *)pFilter->pchNameFilter : NULL;
		char *pchNext = NULL;
		if (pchNameFilter)
			strdup_alloca(pchNameFilter, pchNameFilter);
		do
		{
			S32 iStartCount = iCount;

			if (pchNameFilter)
			{
				pchNameFilter += strspn(pchNameFilter, s_pchDelims);
				pchNext = pchNameFilter + strcspn(pchNameFilter, s_pchDelims);
				if (*pchNext)
					*pchNext++ = '\0';
			}

			for (i = 0; i < eaSize(&pGroupProject->unlockDefs); i++)
			{
				GroupProjectUnlockDef *pUnlock = GET_REF(pGroupProject->unlockDefs[i]->unlockDef);
				GroupProjectUnlockUI *pUnlockUI = NULL;
				if (!pUnlock)
					continue;
				if (pchNameFilter && !strstri(pUnlock->name, pchNameFilter))
					continue;

				// Attempt to keep the unlock instances stable
				for (j = iCount; j < eaSize(peaUnlockUI); j++)
				{
					if (GET_REF((*peaUnlockUI)[j]->hGroupProjectUnlock) == pUnlock)
					{
						if (j != iCount)
							eaSwap(peaUnlockUI, j, iCount);
						pUnlockUI = (*peaUnlockUI)[iCount++];
						break;
					}
				}
				if (!pUnlockUI)
				{
					pUnlockUI = StructCreate(parse_GroupProjectUnlockUI);
					eaInsert(peaUnlockUI, pUnlockUI, iCount++);
				}

				gclGroupProjectUI_UpdateUnlock(pUnlockUI, pUnlock, pState);
			}

			// If the list should be sorted by groups, sort any new
			// unlock instances now.
			if (pFilter && pFilter->bSortInternalGrouped && iCount != iStartCount)
			{
				mergeSort(&(*peaUnlockUI)[iStartCount], iCount - iStartCount, sizeof(void *), NULL, gclGroupProject_GroupProjectUnlock_SortInternalName);
			}

			pchNameFilter = pchNext;
		} while (pchNameFilter && strspn(pchNameFilter, s_pchDelims) != strcspn(pchNameFilter, ""));
	}

	eaSetSizeStruct(peaUnlockUI, parse_GroupProjectUnlockUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaUnlockUI, GroupProjectUnlockUI, true);
}

void gclGroupProjectUI_GetTaskRewardList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, DonationTaskDef *pTask, U32 iSlot)
{
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	DonationTaskRewardUI ***peaRewardUI = ui_GenGetManagedListSafe(pGen, DonationTaskRewardUI);
	S32 i, j, iCount = 0;
	DonationTaskSlot *pSlot = NULL;
	InvRewardRequest *pRewardItems = NULL;

	// Find the active slot information
	if (pState)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (pState->taskSlots[i]->taskSlotNum == iSlot)
			{
				pSlot = pState->taskSlots[i];
				if (!pTask)
					pTask = GET_REF(pSlot->taskDef);
				break;
			}
		}
	}

	if (pTask)
	{
		// Only player projects may have rewards
		if (pGroupProject && pGroupProject->type == GroupProjectType_Player)
		{
			pRewardItems = gclGroupProject_GetTaskRewards(pGroupProject->name, pTask->name);
		}

		for (i = 0; i < eaSize(&pTask->taskRewards); i++)
		{
			DonationTaskReward *pReward = pTask->taskRewards[i];
			DonationTaskRewardUI *pRewardUI = NULL;

			// Attempt to keep the ui reward instances stable
			for (j = iCount; j < eaSize(peaRewardUI); j++)
			{
				if ((pReward->rewardType == DonationTaskRewardType_Unlock
						&& GET_REF(pReward->unlockDef)
						&& (*peaRewardUI)[j]->pUnlock
						&& GET_REF((*peaRewardUI)[j]->pUnlock->hGroupProjectUnlock) == GET_REF(pReward->unlockDef))
					|| (pReward->rewardType == DonationTaskRewardType_NumericAdd
						&& GET_REF(pReward->numericDef)
						&& !(*peaRewardUI)[j]->bNumericSet
						&& (*peaRewardUI)[j]->pNumeric
						&& GET_REF((*peaRewardUI)[j]->pNumeric->hGroupProjectNumeric) == GET_REF(pReward->numericDef))
					|| (pReward->rewardType == DonationTaskRewardType_NumericSet
						&& GET_REF(pReward->numericDef)
						&& (*peaRewardUI)[j]->bNumericSet
						&& (*peaRewardUI)[j]->pNumeric
						&& GET_REF((*peaRewardUI)[j]->pNumeric->hGroupProjectNumeric) == GET_REF(pReward->numericDef))
					)
				{
					if (j != iCount)
						eaSwap(peaRewardUI, j, iCount);
					pRewardUI = (*peaRewardUI)[iCount++];
					break;
				}
			}
			if (!pRewardUI)
			{
				pRewardUI = StructCreate(parse_DonationTaskRewardUI);
				eaInsert(peaRewardUI, pRewardUI, iCount++);
			}

			gclGroupProjectUI_UpdateReward(pRewardUI, pReward, pState);
		}

		if (pRewardItems)
		{
			Entity *pEnt = entActivePlayerPtr();

			// Ensure numeric data is filled
			for (i = eaSize(&pRewardItems->eaNumericRewards) - 1; i >= 0; i--)
			{
				ItemNumericData *pNumericData = pRewardItems->eaNumericRewards[i];
				if (inv_FillNumericRewardRequestClient(pEnt, pNumericData, pRewardItems))
				{
					eaRemove(&pRewardItems->eaNumericRewards, i);
					StructDestroy(parse_ItemNumericData, pNumericData);
				}
			}

			for (i = 0; i < eaSize(&pRewardItems->eaRewards); i++)
			{
				InventorySlot *pItemReward = pRewardItems->eaRewards[i];
				DonationTaskRewardUI *pRewardUI = NULL;

				if (!pItemReward->pItem)
					continue;

				// Attempt to keep the ui reward instances stable
				for (j = iCount; j < eaSize(peaRewardUI); j++)
				{
					if ((*peaRewardUI)[j]->pItem == pItemReward->pItem)
					{
						if (j != iCount)
							eaSwap(peaRewardUI, j, iCount);
						pRewardUI = (*peaRewardUI)[iCount++];
						break;
					}
				}
				if (!pRewardUI)
				{
					pRewardUI = StructCreate(parse_DonationTaskRewardUI);
					eaInsert(peaRewardUI, pRewardUI, iCount++);
				}

				gclGroupProjectUI_UpdateRewardItem(pRewardUI, pItemReward, pState);
			}
		}
	}

	eaSetSizeStruct(peaRewardUI, parse_DonationTaskRewardUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaRewardUI, DonationTaskRewardUI, true);
}

void gclGroupProjectUI_GetNumericList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, GroupProjectNumericFilter *pFilter)
{
	GroupProjectNumericUI ***peaNumericUI = ui_GenGetManagedListSafe(pGen, GroupProjectNumericUI);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	S32 i, j, iCount = 0;

	if (pState && pGroupProject)
	{
		for (i = 0; i < eaSize(&pGroupProject->validNumerics); i++)
		{
			GroupProjectNumericDef *pNumeric = GET_REF(pGroupProject->validNumerics[i]->numericDef);
			GroupProjectNumericUI *pNumericUI = NULL;

			if (!pNumeric)
				continue;

			if (pFilter)
			{
				if (eaSize(&pFilter->eapchNameFilters) > 0)
				{
					for (j = eaSize(&pFilter->eapchNameFilters) - 1; j >= 0; j--)
					{
						if (isWildcardMatch(pFilter->eapchNameFilters[j], pNumeric->name, false, true))
							break;
					}
					if (j < 0)
						continue;
				}
			}

			// Attempt to keep the numeric instances stable
			for (j = iCount; j < eaSize(peaNumericUI); j++)
			{
				if (GET_REF((*peaNumericUI)[j]->hGroupProjectNumeric) == pNumeric)
				{
					if (j != iCount)
						eaSwap(peaNumericUI, j, iCount);
					pNumericUI = (*peaNumericUI)[iCount++];
					break;
				}
			}
			if (!pNumericUI)
			{
				pNumericUI = StructCreate(parse_GroupProjectNumericUI);
				eaInsert(peaNumericUI, pNumericUI, iCount++);
			}

			gclGroupProjectUI_UpdateNumeric(pNumericUI, pNumeric, 0, pState);
		}
	}

	eaSetSizeStruct(peaNumericUI, parse_GroupProjectNumericUI, iCount);
	eaStableSort(*peaNumericUI, NULL, gclGroupProject_GroupProjectNumeric_SortName);
	ui_GenSetManagedListSafe(pGen, peaNumericUI, GroupProjectNumericUI, true);
}

GroupProjectNumericUI *gclGroupProjectUI_GetNumericValue(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, GroupProjectNumericDef *pNumeric)
{
	static GroupProjectNumericUI s_NumericUI;
	GroupProjectNumericUI *pNumericUI = &s_NumericUI;

	if (pGen)
		pNumericUI = ui_GenGetManagedPointer(pGen, parse_GroupProjectNumericUI);

	if (pNumeric)
		gclGroupProjectUI_UpdateNumeric(pNumericUI, pNumeric, 0, pState);
	else if (IS_HANDLE_ACTIVE(pNumericUI->hGroupProjectNumeric))
		StructReset(parse_GroupProjectNumericUI, pNumericUI);

	if (pGen)
		ui_GenSetManagedPointer(pGen, pNumericUI, parse_GroupProjectNumericUI, true);
	return pNumericUI;
}

GroupProjectUnlockUI *gclGroupProjectUI_GetUnlock(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID GroupProjectState *pState, GroupProjectUnlockDef *pUnlock)
{
	static GroupProjectUnlockUI s_UnlockUI;
	GroupProjectUnlockUI *pUnlockUI = &s_UnlockUI;

	if (pGen)
		pUnlockUI = ui_GenGetManagedPointer(pGen, parse_GroupProjectUnlockUI);

	if (pUnlock)
		gclGroupProjectUI_UpdateUnlock(pUnlockUI, pUnlock, pState);
	else if (IS_HANDLE_ACTIVE(pUnlockUI->hGroupProjectUnlock))
		StructReset(parse_GroupProjectUnlockUI, pUnlockUI);

	if (pGen)
		ui_GenSetManagedPointer(pGen, pUnlockUI, parse_GroupProjectUnlockUI, true);
	return pUnlockUI;
}

void gclGroupProjectUI_UpdateResultItem(DonationTaskBucketDonationResultUI *pResultUI, const char *pchItemName, S32 eBagID, S32 iSlot, S32 iRequested, S32 iActual)
{
	const char *pchItemDef = GET_REF(pResultUI->hItemDef) ? GET_REF(pResultUI->hItemDef)->pchName : REF_STRING_FROM_HANDLE(pResultUI->hItemDef);

	if (pchItemDef != pchItemName)
	{
		if (pchItemName)
			SET_HANDLE_FROM_STRING("ItemDef", pchItemName, pResultUI->hItemDef);
		else
			REMOVE_HANDLE(pResultUI->hItemDef);
	}

	if (!pResultUI->pDonatedItem || GET_REF(pResultUI->pDonatedItem->hItem) != GET_REF(pResultUI->hItemDef))
	{
		if (pResultUI->pDonatedItem)
			StructDestroySafe(parse_Item, &pResultUI->pDonatedItem);

		if (GET_REF(pResultUI->hItemDef))
			pResultUI->pDonatedItem = CONTAINER_RECONST(Item, inv_ItemInstanceFromDefName(pchItemName, 1, 0, NULL, NULL, NULL, false, NULL));
	}

	pResultUI->eBagID = eBagID;
	pResultUI->iSlot = iSlot;
	pResultUI->iActualDonation = iActual;
	pResultUI->iRequestedDonation = iRequested;
	pResultUI->bSuccessfulDonation = iActual == iRequested;
	pResultUI->bPartialDonation = iActual > 0 && iActual != iRequested;
	pResultUI->bFailedDonation = iActual == 0;
}

DonationTaskBucketDonationResultUI *gclGroupProjectUI_StableResultItem(DonationTaskBucketDonationResultUI ***peaResultUI, S32 *piCount, const char *pchItemName, S32 eBagID, S32 iSlot)
{
	DonationTaskBucketDonationResultUI *pResult = NULL;
	S32 i;

	for (i = *piCount; i < eaSize(peaResultUI); i++)
	{
		const char *pchItemDef = GET_REF((*peaResultUI)[i]->hItemDef) ? GET_REF((*peaResultUI)[i]->hItemDef)->pchName : REF_STRING_FROM_HANDLE((*peaResultUI)[i]->hItemDef);
		if (pchItemDef == pchItemName
			&& (*peaResultUI)[i]->eBagID == eBagID
			&& (*peaResultUI)[i]->iSlot == iSlot)
		{
			if (*piCount != i)
				eaMove(peaResultUI, *piCount, i);
			pResult = (*peaResultUI)[(*piCount)++];
			break;
		}
	}

	if (!pResult)
	{
		pResult = StructCreate(parse_DonationTaskBucketDonationResultUI);
		eaInsert(peaResultUI, pResult, (*piCount)++);
	}

	return pResult;
}

void gclGroupProjectUI_GetDonationResultItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ContributionNotifyData *pNotify, DonationTaskBucketDonationResultFilter *pFilter)
{
	DonationTaskBucketDonationResultUI ***peaResultUI = ui_GenGetManagedListSafe(pGen, DonationTaskBucketDonationResultUI);
	DonationTaskBucketDonationResultUI *pResultUI;
	bool bSuccessful, bPartial, bFailed;
	int iCount = 0;

	if (pNotify)
	{
		if (pNotify->donatedItemName)
		{
			bSuccessful = pNotify->requestedDonationCount == pNotify->donationCount && pFilter->bIncludeSuccessfulDonations;
			bPartial = pNotify->requestedDonationCount != pNotify->donationCount && pNotify->donationCount > 0 && pFilter->bIncludePartialDonations;
			bFailed = pNotify->requestedDonationCount != pNotify->donationCount && pNotify->donationCount <= 0 && pFilter->bIncludeFailedDonations;

			if (bSuccessful || bPartial || bFailed)
			{
				// Add item
				pResultUI = gclGroupProjectUI_StableResultItem(peaResultUI, &iCount, pNotify->donatedItemName, 0, 0);
				gclGroupProjectUI_UpdateResultItem(pResultUI, pNotify->donatedItemName, 0, 0, pNotify->requestedDonationCount, pNotify->donationCount);
			}
		}
		else
		{
			S32 i, j;
			for (i = 0; i < eaSize(&pNotify->requestedDonations); i++)
			{
				ContributionItemData *pRequested = pNotify->requestedDonations[i];
				ContributionItemData *pActual = NULL;

				for (j = eaSize(&pNotify->actualDonations) - 1; j >= 0; j--)
				{
					if (pNotify->actualDonations[j]->itemName == pRequested->itemName
						&& pNotify->actualDonations[j]->bagID == pRequested->bagID
						&& pNotify->actualDonations[j]->slotIdx == pRequested->slotIdx)
					{
						pActual = pNotify->actualDonations[j];
						break;
					}
				}

				bSuccessful = pRequested && pActual && pRequested->count == pActual->count && pFilter->bIncludeSuccessfulDonations;
				bPartial = pRequested && pActual && pRequested->count != pActual->count && pFilter->bIncludePartialDonations;
				bFailed = pRequested && !pActual && pFilter->bIncludeFailedDonations;

				if (bSuccessful || bPartial || bFailed)
				{
					// Add item
					pResultUI = gclGroupProjectUI_StableResultItem(peaResultUI, &iCount, pRequested->itemName, pRequested->bagID, pRequested->slotIdx);
					gclGroupProjectUI_UpdateResultItem(pResultUI, pRequested->itemName, pRequested->bagID, pRequested->slotIdx, pRequested->count, SAFE_MEMBER(pActual, count));
				}
			}
		}
	}

	eaSetSizeStruct(peaResultUI, parse_DonationTaskBucketDonationResultUI, iCount);
	ui_GenSetManagedListSafe(pGen, peaResultUI, DonationTaskBucketDonationResultUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetSlotList");
void gclGroupProjectExprGetSlotList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	gclGroupProjectUI_GetSlotList(pGen, pState, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetSlotListRange");
void gclGroupProjectExprGetSlotListRange(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 iFirstSlot, S32 iLastSlot)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationSlotFilter Filter = {0};
	Filter.iFirstSlot = iFirstSlot;
	Filter.iLastSlot = iLastSlot;
	gclGroupProjectUI_GetSlotList(pGen, pState, &Filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetSlotListType");
void gclGroupProjectExprGetSlotListType(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, U32 eSlotType)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationSlotFilter Filter = {0};
	Filter.iLastSlot = -1;
	Filter.eSlotType = eSlotType;
	gclGroupProjectUI_GetSlotList(pGen, pState, &Filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetTaskList");
void gclGroupProjectExprGetTaskList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 eSlotType, int slotNum)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskFilter Filter = {0};
	Filter.eSlotType = eSlotType;
	gclGroupProjectUI_GetTaskList(pGen, pState, &Filter, slotNum);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetCategorySortedTaskList");
void gclGroupProjectExprGetCategorySortedTaskList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 eSlotType, int slotNum)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskFilter Filter = {0};
	Filter.eSlotType = eSlotType;
	Filter.bSortByCategories = true;
	Filter.bAddCategoryHeaders = true;
	gclGroupProjectUI_GetTaskList(pGen, pState, &Filter, slotNum);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetMaskedCategorySortedTaskList");
void gclGroupProjectExprGetMaskedCategorySortedTaskList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 eSlotType, int slotNum, U32 uCategoryMask)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskFilter Filter = {0};
	Filter.eSlotType = eSlotType;
	Filter.bSortByCategories = true;
	Filter.bAddCategoryHeaders = true;
	Filter.uCategoryMask = uCategoryMask;
	gclGroupProjectUI_GetTaskList(pGen, pState, &Filter, slotNum);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetBucketList");
void gclGroupProjectExprGetBucketList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 iSlot)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	gclGroupProjectUI_GetTaskBucketList(pGen, pState, NULL, iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetTaskBucketList");
void gclGroupProjectExprGetTaskBucketList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchTaskName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskDef *pTask = RefSystem_ReferentFromString("DonationTaskDef", pchTaskName);
	gclGroupProjectUI_GetTaskBucketList(pGen, pState, pTask, -1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetLeaderboard");
void gclGroupProjectExprGetLeaderboard(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchNameFilter, S32 iSortMode)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDonorFilter Filter = {0};
	Filter.pchNameFilter = pchNameFilter;
	Filter.bSortName = iSortMode == 1;
	gclGroupProjectUI_GetDonorList(pGen, pState, &Filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetList");
void gclGroupProjectExprGetList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchNameFilter)
{
	GroupProjectUI ***peaGroupProjectUI = ui_GenGetManagedListSafe(pGen, GroupProjectUI);
	GroupProjectFilter Filter = {0};
	Filter.pchNameFilter = pchNameFilter;
	Filter.eProjectType = eProjectType;
	gclGroupProjectUI_GetGroupProjectList(peaGroupProjectUI, &Filter);
	ui_GenSetManagedListSafe(pGen, peaGroupProjectUI, GroupProjectUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetListString");
const char *gclGroupProjectExprGetListString(ExprContext *pContext, U32 eProjectType, const char *pchNameFilter)
{
	static GroupProjectUI **s_eaGroupProjectUI;
	static char *s_estrProjectList;
	GroupProjectFilter Filter = {0};
	S32 i;

	Filter.pchNameFilter = pchNameFilter;
	Filter.eProjectType = eProjectType;
	gclGroupProjectUI_GetGroupProjectList(&s_eaGroupProjectUI, &Filter);

	estrClear(&s_estrProjectList);
	for (i = 0; i < eaSize(&s_eaGroupProjectUI); i++)
	{
		if (s_estrProjectList && *s_estrProjectList)
			estrConcatChar(&s_estrProjectList, ' ');
		estrConcatString(&s_estrProjectList, s_eaGroupProjectUI[i]->pchGroupName, strlen(s_eaGroupProjectUI[i]->pchGroupName));
	}

	return exprContextAllocString(pContext, s_estrProjectList);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetProject");
SA_RET_NN_VALID GroupProjectUI *gclGroupProjectExprGetProject(SA_PARAM_NN_VALID UIGen *pGen, GroupProjectType eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : RefSystem_ReferentFromString("GroupProjectDef", pchGroupName);

	if (pGroupProject && pGroupProject->type != eProjectType)
		pGroupProject = NULL;

	return gclGroupProjectUI_GetGroupProject(pGen, pGroupProject);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetFirstActiveSlot");
S32 gclGroupProjectExprGetFirstActiveSlot(U32 eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	U32 iBest = UINT_MAX;
	S32 i;

	if (pState)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (IS_HANDLE_ACTIVE(pState->taskSlots[i]->taskDef) && pState->taskSlots[i]->taskSlotNum < iBest)
				iBest = pState->taskSlots[i]->taskSlotNum;
		}
	}

	return iBest;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetProjects");
bool gclGroupProjectExprGetProjects(U32 eProjectType)
{
	// Max request frequency 30 seconds
	// Max request frequency when missing the container 5 seconds
	// Ignore request if last requested in the last 5 frames
	static const U32 c_RequestFrequency = 30000;
	static const U32 c_RequestFrequencyMissing = 5000;
	static const U32 c_RequestLatch = 5;
	static U32 s_uLastRequestTime_Guild;
	static U32 s_uFrameLatch_Guild;
	static U32 s_uLastRequestTime_Player;
	static U32 s_uFrameLatch_Player;
	GroupProjectContainer *pContainer = gclGroupProject_ResolveContainer(eProjectType);

	switch (eProjectType)
	{
	case GroupProjectType_Guild:
		if ((gGCLState.totalElapsedTimeMs >= s_uLastRequestTime_Guild + c_RequestFrequency || !pContainer && gGCLState.totalElapsedTimeMs >= s_uLastRequestTime_Guild + c_RequestFrequencyMissing)
			&& g_ui_State.uiFrameCount >= s_uFrameLatch_Guild)
		{
			gclGroupProject_GetProjectDefsForType(GroupProjectType_Guild);
			gclGroupProject_SubscribeToGuildProject();
			s_uLastRequestTime_Guild = gGCLState.totalElapsedTimeMs;
		}
		s_uFrameLatch_Guild = g_ui_State.uiFrameCount + c_RequestLatch;
		return true;

	case GroupProjectType_Player:
		if ((gGCLState.totalElapsedTimeMs >= s_uLastRequestTime_Player + c_RequestFrequency || !pContainer && gGCLState.totalElapsedTimeMs >= s_uLastRequestTime_Player + c_RequestFrequencyMissing)
			&& g_ui_State.uiFrameCount >= s_uFrameLatch_Player)
		{
			gclGroupProject_GetProjectDefsForType(GroupProjectType_Player);
			gclGroupProject_SubscribeToPlayerProject();
			s_uLastRequestTime_Player = gGCLState.totalElapsedTimeMs;
		}
		s_uFrameLatch_Player = g_ui_State.uiFrameCount + c_RequestLatch;
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectQueueTask");
bool gclGroupProjectExprStartTask(U32 eProjectType, const char *pchGroupName, S32 iSlot, const char *pchDonationTask)
{
	// start the task if there is no active task, queue the task if there is an active task

	switch (eProjectType)
	{
	case GroupProjectType_Guild:
		ServerCmd_gslGuildProject_SetNextTask(pchGroupName, iSlot, pchDonationTask);
		return true;

	case GroupProjectType_Player:
		ServerCmd_gslPlayerProject_SetNextTask(pchGroupName, iSlot, pchDonationTask);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectClaimReward");
bool gclGroupProjectExprClaimReward(U32 eProjectType, const char *pchGroupName, S32 iSlot)
{
	switch (eProjectType)
	{
	case GroupProjectType_Player:
		ServerCmd_gslPlayerProject_ClaimReward(pchGroupName, iSlot);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanCancelGroupProject");
bool gclGroupProjectExprCanCancelProject(U32 eProjectType, const char *pchGroupName, U32 iSlot)
{
	GroupProjectState *pState = NULL;
	DonationTaskSlot *pSlot = NULL;
	DonationTaskDef *pTaskDef;
	int i;

	pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	if (!pState)
	{
		return false;
	}

	for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
	{
		if (pState->taskSlots[i]->taskSlotNum == iSlot)
		{
			pSlot = pState->taskSlots[i];
			break;
		}
	}

	if (!pSlot || pSlot->state != DonationTaskState_AcceptingDonations)
	{
		return false;
	}

	pTaskDef = GET_REF(pSlot->taskDef);
	if (!pTaskDef || !pTaskDef->cancelable)
	{
		return false;
	}

	if (eProjectType == GroupProjectType_Guild)
	{
		Entity *pEnt = entActivePlayerPtr();
	    Guild *pGuild = guild_GetGuild(pEnt);
	    if (pGuild)
	    {
	        GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
	        if (pMember && pMember->iRank == (eaSize(&pGuild->eaRanks) - 1))
	        {
	        	return true;
	        }
	    }
	}
	else
	{
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CancelGroupProject");
void gclGroupProjectExprCancelProject(U32 eProjectType, const char *pchGroupName, S32 iSlot)
{
	switch (eProjectType)
	{
	case GroupProjectType_Player:
		ServerCmd_gslPlayerProject_CancelProject(pchGroupName, iSlot);
		return;

	case GroupProjectType_Guild:
		ServerCmd_gslGuildProject_CancelProject(pchGroupName, iSlot);
		return;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskBucket");
SA_RET_OP_VALID DonationTaskBucketUI *gclGroupProjectExprGetTaskBucket(U32 eProjectType, const char *pchGroupName, U32 iSlot, const char *pchBucketName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskBucketUI *pBucketUI = NULL;
	S32 i;

	if (pState)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (pState->taskSlots[i]->taskSlotNum == iSlot)
			{
				pBucketUI = gclGroupProjectUI_GetDonationTaskBucket(NULL, pState, pState->taskSlots[i], GET_REF(pState->taskSlots[i]->taskDef), pchBucketName);
				break;
			}
		}
	}

	return pBucketUI;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectTaskBucket");
SA_RET_OP_VALID DonationTaskBucketUI *gclGroupProjectExprGenGetTaskBucket(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, U32 iSlot, const char *pchBucketName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskBucketUI *pBucketUI = NULL;
	S32 i;

	if (pState)
	{
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (pState->taskSlots[i]->taskSlotNum == iSlot)
			{
				pBucketUI = gclGroupProjectUI_GetDonationTaskBucket(pGen, pState, pState->taskSlots[i], GET_REF(pState->taskSlots[i]->taskDef), pchBucketName);
				break;
			}
		}
	}

	if (!pBucketUI)
		ui_GenSetManagedPointer(pGen, pBucketUI, parse_DonationTaskBucketUI, true);
	return pBucketUI;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetGenTaskBucket");
SA_RET_OP_VALID DonationTaskBucketUI *gclGroupProjectExprGetGenTaskBucket(SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	DonationTaskBucketUI *pBucket = ui_GenGetPointer(pGen, NULL, &pTable);
	return pTable == parse_DonationTaskBucketUI ? pBucket : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskBucketByKey");
SA_RET_OP_VALID DonationTaskBucketUI *gclGroupProjectExprGetTaskBucketByKey(U32 eProjectType, const char *pchBucketKey)
{
	GroupProjectState *pState;
	GroupProjectDonationRequirement *pTaskBucket;
	DonationTaskSlot *pSlot;
	DonationTaskBucketUI *pBucketUI = NULL;

	gclGroupProject_GetBucketParams(pchBucketKey, &pState, &pSlot, &pTaskBucket, NULL);

	if (pState && pSlot && pTaskBucket)
		pBucketUI = gclGroupProjectUI_GetDonationTaskBucket(NULL, pState, pSlot, GET_REF(pSlot->taskDef), pTaskBucket->name);

	return pBucketUI;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectTaskBucketByKey");
SA_RET_OP_VALID DonationTaskBucketUI *gclGroupProjectExprGenGetTaskBucketByKey(SA_PARAM_NN_VALID UIGen *pGen, const char *pchBucketKey)
{
	GroupProjectState *pState;
	GroupProjectDonationRequirement *pTaskBucket;
	DonationTaskSlot *pSlot;
	DonationTaskBucketUI *pBucketUI = NULL;

	gclGroupProject_GetBucketParams(pchBucketKey, &pState, &pSlot, &pTaskBucket, NULL);

	if (pState && pSlot && pTaskBucket)
		pBucketUI = gclGroupProjectUI_GetDonationTaskBucket(pGen, pState, pSlot, GET_REF(pSlot->taskDef), pTaskBucket->name);

	if (!pBucketUI)
		ui_GenSetManagedPointer(pGen, pBucketUI, parse_DonationTaskBucketUI, true);
	return pBucketUI;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetRewardNumeric");
const char *gclGroupProjectExprGetRewardNumeric(U32 eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProject;
	ItemDef *pItemDef;

	if (!pState)
		return NULL;

	pGroupProject = GET_REF(pState->projectDef);
	if (!pGroupProject || !IS_HANDLE_ACTIVE(pGroupProject->contributionNumeric))
		return NULL;

	pItemDef = GET_REF(pGroupProject->contributionNumeric);
	if (!pItemDef)
		return REF_STRING_FROM_HANDLE(pGroupProject->contributionNumeric);
	return pItemDef->pchName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectBucketRewardNumeric");
const char *gclGroupProjectExprBucketRewardNumeric(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket)
{
	GroupProjectState *pState;
	GroupProjectDef *pGroupProject;
	ItemDef *pItemDef;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, NULL, NULL, NULL);
	if (!pState)
		return NULL;

	pGroupProject = GET_REF(pState->projectDef);
	if (!pGroupProject || !IS_HANDLE_ACTIVE(pGroupProject->contributionNumeric))
		return NULL;

	pItemDef = GET_REF(pGroupProject->contributionNumeric);
	if (!pItemDef)
		return REF_STRING_FROM_HANDLE(pGroupProject->contributionNumeric);
	return pItemDef->pchName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskAddQuantity");
bool gclGroupProjectExprTaskAddQuantity(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, S32 iQuantity)
{
    GroupProjectState *pState;
    DonationTaskSlot *pSlot;
	GroupProjectDef *pGroupProjectDef;
    ItemDef *pItemDef = pBucket ? GET_REF(pBucket->hItemDef) : NULL;

	// add quantity of item to slot

    gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;
    if ( pState == NULL || pSlot == NULL || pGroupProjectDef == NULL )
    {
        return false;
    }

	switch (pGroupProjectDef->type)
	{
	case GroupProjectType_Guild:
		ServerCmd_gslGuildProject_DonateSimpleItems(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, pBucket->pchBucketName, REF_STRING_FROM_HANDLE(pBucket->hItemDef), iQuantity * pBucket->iIncrementQuantity);
		return true;

	case GroupProjectType_Player:
		ServerCmd_gslPlayerProject_DonateSimpleItems(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, pBucket->pchBucketName, REF_STRING_FROM_HANDLE(pBucket->hItemDef), iQuantity * pBucket->iIncrementQuantity);
		return true;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskRewardQuantity");
S32 gclGroupProjectExprTaskRewardQuantity(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, S32 iQuantity)
{
	GroupProjectState *pState;
	GroupProjectDef *pGroupProject;
	GroupProjectConstant *pConstant;
	GroupProjectDonationRequirement *pTaskBucket;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, NULL, &pTaskBucket, NULL);
	if (!pState || !pTaskBucket)
		return 0;

	pGroupProject = GET_REF(pState->projectDef);
	if (!pGroupProject || !IS_HANDLE_ACTIVE(pGroupProject->contributionNumeric))
		return 0;

	pConstant = eaGet(&pGroupProject->constants, GroupProject_FindConstant(pGroupProject, pTaskBucket->contributionConstant));
	if (!pConstant)
		return 0;

	return iQuantity * pConstant->value;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetDisplayQuantity");
S32 gclGroupProjectExprGetDisplayQuantity(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, S32 iQuantity)
{
	GroupProjectState *pState;
	GroupProjectDonationRequirement *pTaskBucket;
	ItemDef *pItemDef = pBucket ? GET_REF(pBucket->hItemDef) : NULL;
	F32 fScaleUI = 1;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, NULL, &pTaskBucket, NULL);
	if (!pState || !pTaskBucket)
		return 0;

	if (pItemDef && pItemDef->eType == kItemType_Numeric && pItemDef->fScaleUI != 0 && pItemDef->fScaleUI != 1)
		fScaleUI = pItemDef->fScaleUI;

	return iQuantity * fScaleUI * pBucket->iIncrementQuantity;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskGetItemList");
bool gclGroupProjectExprGetItemList(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchBucket)
{
	InventorySlot ***peaInventorySlot = ui_GenGetManagedListSafe(pGen, InventorySlot);
	UIInventoryKey Key = {0};
	S32 i, iCount = 0;

	if (pBucket)
	{
		for (i = eaSize(&pBucket->eachAvailableItemKeys) - 1; i >= 0; i--)
		{
			if (gclInventoryParseKey(pBucket->eachAvailableItemKeys[i], &Key) && Key.pSlot)
			{
				gclInventoryUpdateSlot(Key.pEntity, Key.pBag, Key.pSlot);
				eaSet(peaInventorySlot, Key.pSlot, iCount++);
			}
		}
	}

	eaSetSize(peaInventorySlot, iCount);
	ui_GenSetManagedListSafe(pGen, peaInventorySlot, InventorySlot, false);
	return iCount > 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskQueueItem");
bool gclGroupProjectExprTaskQueueItem(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey)
{
	UIInventoryKey key = {0};
	GroupProjectState *pState;
	DonationTaskSlot *pSlot;
	DonationTaskDef *taskDef;
	S32 i;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	if ( pState == NULL || pSlot == NULL || !pBucket->bCanQueueItems )
	{
		return false;
	}

	taskDef = GET_REF(pBucket->hDonationTask);
	if ( taskDef == NULL )
	{
		return false;
	}

	for (i = eaSize(&pBucket->eachAvailableItemKeys) - 1; i >= 0; i--)
	{
		if (!stricmp(pBucket->eachAvailableItemKeys[i], pchInventoryKey))
			break;
	}
	if (i < 0)
	{
		return false;
	}

	for (i = eaSize(&pBucket->eachQueuedItemKeys) - 1; i >= 0; i--)
	{
		if (!stricmp(pBucket->eachQueuedItemKeys[i], pchInventoryKey))
			break;
	}
	if (i >= 0)
	{
		return false;
	}

	eaPush(&pBucket->eachQueuedItemKeys, StructAllocString(pchInventoryKey));
	pBucket->iQueuedItemCount = eaSize(&pBucket->eachQueuedItemKeys);
	return true;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskUnqueueItem");
bool gclGroupProjectExprTaskUnqueueItem(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey)
{
	UIInventoryKey key = {0};
	S32 i;

	if (pBucket)
	{
		for (i = eaSize(&pBucket->eachQueuedItemKeys) - 1; i >= 0; i--)
		{
			if (!stricmp(pBucket->eachQueuedItemKeys[i], pchInventoryKey))
				break;
		}
		if (i >= 0)
		{
			StructFreeString(eaRemove(&pBucket->eachQueuedItemKeys, i));
			pBucket->iQueuedItemCount = eaSize(&pBucket->eachQueuedItemKeys);
			return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskGetItemQueuePosition");
S32 gclGroupProjectExprGetItemQueuePosition(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey)
{
	if (pBucket)
	{
		S32 i;

		for (i = eaSize(&pBucket->eachQueuedItemKeys) - 1; i >= 0; i--)
		{
			if (!stricmp(pBucket->eachQueuedItemKeys[i], pchInventoryKey))
				return i;
		}
	}

	return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskClearQueuedItems");
void gclGroupProjectExprTaskClearQueuedItems(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket)
{
	if (pBucket)
	{
		while (eaSize(&pBucket->eachQueuedItemKeys) > 0)
			StructFreeString(eaPop(&pBucket->eachQueuedItemKeys));
		pBucket->iQueuedItemCount = eaSize(&pBucket->eachQueuedItemKeys);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskAddQueuedItems");
bool gclGroupProjectExprAddQueuedItems(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket)
{
	GroupProjectState *pState;
	GroupProjectDef *pGroupProjectDef;
	DonationTaskSlot *pSlot;
	DonationTaskDef *taskDef;
	ContributionItemList donationQueue = {0};
	bool bDonated = false;
	S32 i;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;
	if ( pState == NULL || pSlot == NULL || pGroupProjectDef == NULL )
	{
		return false;
	}

	taskDef = GET_REF(pBucket->hDonationTask);
	if ( taskDef == NULL )
	{
		return false;
	}

	for (i = 0; i < eaSize(&pBucket->eachQueuedItemKeys); i++)
	{
		UIInventoryKey key = {0};
		ContributionItemData *pDonate = NULL;
		ItemDef *itemDef;

		if ( !gclInventoryParseKey(pBucket->eachQueuedItemKeys[i], &key) || !key.pSlot || !key.pSlot->pItem )
		{
			StructReset(parse_ContributionItemList, &donationQueue);
			return false;
		}

		itemDef = GET_REF(key.pSlot->pItem->hItem);
		if ( !itemDef )
		{
			StructReset(parse_ContributionItemList, &donationQueue);
			return false;
		}

		// Add a single item to the queue
		pDonate = StructCreate(parse_ContributionItemData);
		pDonate->itemName = itemDef->pchName;
		pDonate->bagID = key.eBag;
		pDonate->slotIdx = key.iSlot;
		pDonate->count = 1;
		eaPush(&donationQueue.items, pDonate);
	}

	if (eaSize(&donationQueue.items) > 0)
	{
		switch (pGroupProjectDef->type)
		{
			xcase GroupProjectType_Guild:
		ServerCmd_gslGuildProject_DonateExpressionItemList(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, taskDef->name, pBucket->pchBucketName, &donationQueue);
		bDonated = true;
		xcase GroupProjectType_Player:
		ServerCmd_gslPlayerProject_DonateExpressionItemList(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, taskDef->name, pBucket->pchBucketName, &donationQueue);
		bDonated = true;
		}
	}

	StructReset(parse_ContributionItemList, &donationQueue);
	return bDonated;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskAddItemCount");
bool gclGroupProjectExprTaskAddItemCount(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey, S32 iCount)
{
	UIInventoryKey key = {0};
	GroupProjectState *pState;
	GroupProjectDef *pGroupProjectDef;
	DonationTaskSlot *pSlot;
	DonationTaskDef *taskDef;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;
	if ( pState == NULL || pSlot == NULL || pGroupProjectDef == NULL )
	{
		return false;
	}

	taskDef = GET_REF(pBucket->hDonationTask);
	if ( taskDef == NULL || iCount <= 0 )
	{
		return false;
	}

	// add item pointed to by the inventory key to the slot
	if ( gclInventoryParseKey(pchInventoryKey, &key) && key.pSlot && key.pSlot->pItem && iCount <= key.pSlot->pItem->count )
	{
		ItemDef *itemDef = GET_REF(key.pSlot->pItem->hItem);

		if ( itemDef != NULL)
		{
			switch (pGroupProjectDef->type)
			{
			case GroupProjectType_Guild:
				ServerCmd_gslGuildProject_DonateExpressionItem(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, taskDef->name, pBucket->pchBucketName, itemDef->pchName, key.eBag, key.iSlot, iCount);
				return true;

			case GroupProjectType_Player:
				ServerCmd_gslPlayerProject_DonateExpressionItem(REF_STRING_FROM_HANDLE(pState->projectDef), pSlot->taskSlotNum, taskDef->name, pBucket->pchBucketName, itemDef->pchName, key.eBag, key.iSlot, iCount);
				return true;
			}
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskAddItem");
bool gclGroupProjectExprTaskAddItem(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey)
{
	return gclGroupProjectExprTaskAddItemCount(pBucket, pchInventoryKey, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskRewardItemCount");
S32 gclGroupProjectExprTaskRewardItemCount(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey, S32 iCount)
{
	GroupProjectState *pState;
	GroupProjectDef *pGroupProject;
	GroupProjectConstant *pConstant;
	GroupProjectDonationRequirement *pTaskBucket;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, NULL, &pTaskBucket, NULL);
	if (!pState || !pTaskBucket || iCount <= 0)
		return 0;

	pGroupProject = GET_REF(pState->projectDef);
	if (!pGroupProject || !IS_HANDLE_ACTIVE(pGroupProject->contributionNumeric))
		return 0;

	pConstant = eaGet(&pGroupProject->constants, GroupProject_FindConstant(pGroupProject, pTaskBucket->contributionConstant));
	if (!pConstant)
		return 0;

	return pConstant->value * iCount;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskRewardItem");
S32 gclGroupProjectExprTaskRewardItem(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket, const char *pchInventoryKey)
{
	if (pBucket && pBucket->bCanQueueItems && eaSize(&pBucket->eachQueuedItemKeys) > 0)
		return gclGroupProjectExprTaskRewardItemCount(pBucket, pchInventoryKey, eaSize(&pBucket->eachQueuedItemKeys));
	return gclGroupProjectExprTaskRewardItemCount(pBucket, pchInventoryKey, 1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetProjectMessage");
const char *gclGroupProjectExprGetProjectMessage(U32 eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	return pState ? pState->projectMessage : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectSetProjectMessage");
void gclGroupProjectExprSetProjectMessage(U32 eProjectType, const char *pchGroupName, const char *pchMOTD)
{
	// set GroupProjectState MOTD
	if (eProjectType == GroupProjectType_Guild)
		ServerCmd_gslGuildProject_SetProjectMessage(pchGroupName, pchMOTD);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectCanSetProjectMessage");
bool gclGroupProjectExprCanSetProjectMessage(U32 eProjectType, const char *pchGroupName)
{
	// determine if the player has permission to change the project message
	Entity *pEnt = entActivePlayerPtr();
	if (eProjectType == GroupProjectType_Guild)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		if (pGuild)
		{
			GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
			if (pMember)
				return guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetProjectName");
const char *gclGroupProjectExprGetProjectName(U32 eProjectType, const char *pchGroupName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	return pState ? pState->projectPlayerName : NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectSetProjectName");
void gclGroupProjectExprSetProjectName(U32 eProjectType, const char *pchGroupName, const char *pchName)
{
	// set GroupProjectState name
	if (eProjectType == GroupProjectType_Guild)
		ServerCmd_gslGuildProject_SetProjectPlayerName(pchGroupName, pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectCanSetProjectName");
bool gclGroupProjectExprCanSetProjectName(U32 eProjectType, const char *pchGroupName)
{
	// determine if the player has permission to change the project message
	Entity *pEnt = entActivePlayerPtr();
	if (eProjectType == GroupProjectType_Guild)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		if (pGuild)
		{
			GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
			if (pMember)
				return guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement);
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectCanManageSlot");
bool gclGroupProjectExprCanManageSlots(U32 eProjectType, const char *pchGroupName, S32 iSlot)
{
	// determine if player has permission to set the active project or change the next project
	Entity *pEnt = entActivePlayerPtr();
	if (eProjectType == GroupProjectType_Guild)
	{
		Guild *pGuild = guild_GetGuild(pEnt);
		if (pGuild)
		{
			GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
			if (pMember)
				return guild_HasPermission(pMember->iRank, pGuild, GuildPermission_GuildProjectManagement);
		}
	}
	else if (eProjectType == GroupProjectType_Player)
		return pEnt != NULL;
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanPurchaseFromProvisionedStore");
bool gclGroupProjectExprCanPurchaseFromProvisionedStore(void)
{
    // determine if player has permission to purchase from provisioned stores
    Entity *pEnt = entActivePlayerPtr();
    Guild *pGuild = guild_GetGuild(pEnt);
    if (pGuild)
    {
        GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
        if (pMember)
            return guild_HasPermission(pMember->iRank, pGuild, GuildPermission_BuyProvisioned);
    }
    return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CanDonateToGuildProjects");
bool gclGroupProjectExprCanDonateToGuildProjects(void)
{
    // determine if player has permission to purchase from provisioned stores
    Entity *pEnt = entActivePlayerPtr();
    Guild *pGuild = guild_GetGuild(pEnt);
    if (pGuild)
    {
        GuildMember *pMember = eaIndexedGetUsingInt(&pGuild->eaMembers, entGetContainerID(pEnt));
        if (pMember)
            return guild_HasPermission(pMember->iRank, pGuild, GuildPermission_DonateToProjects);
    }
    return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetRewardList");
void gclGroupProjectExprGetRewardList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, S32 iSlot)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	gclGroupProjectUI_GetTaskRewardList(pGen, pState, NULL, iSlot);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetTaskRewardList");
void gclGroupProjectExprGetTaskRewardList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchTaskName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskDef *pTask = RefSystem_ReferentFromString("DonationTaskDef", pchTaskName);
	gclGroupProjectUI_GetTaskRewardList(pGen, pState, pTask, -1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetUnlockList");
void gclGroupProjectExprGetUnlockList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchNameFilter)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectUnlockFilter Filter = {0};
	Filter.pchNameFilter = pchNameFilter;
	Filter.bSortInternalGrouped = true;
	gclGroupProjectUI_GetUnlockList(pGen, pState, &Filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetUnlock");
SA_RET_NN_VALID GroupProjectUnlockUI *gclGroupProjectGetUnlock(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchUnlockName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectUnlockDef *pUnlock = RefSystem_ReferentFromString("GroupProjectUnlockDef", pchUnlockName);
	return gclGroupProjectUI_GetUnlock(pGen, pState, pUnlock);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskGetResult");
SA_RET_OP_VALID ContributionNotifyData *gclGroupProjectExprTaskGetResult(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket)
{
	Entity *pPlayer = entActivePlayerPtr();
	GroupProjectState *pState;
	DonationTaskSlot *pSlot;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	if (!pState || !pSlot || !pPlayer)
		return NULL;

	return gclGroupProject_GetContributionNotify(pPlayer, pState, pSlot, pBucket->pchBucketName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskClearResult");
void gclGroupProjectExprTaskClearResult(SA_PARAM_OP_VALID DonationTaskBucketUI *pBucket)
{
	GroupProjectState *pState;
	DonationTaskSlot *pSlot;

	gclGroupProject_GetBucketParams(SAFE_MEMBER(pBucket, pchBucketKey), &pState, &pSlot, NULL, NULL);
	if (!pState || !pSlot)
		return;

	gclGroupProject_ResetContributionNotify(pState, pSlot, pBucket->pchBucketName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectResultGetDonatedItems");
void gclGroupProjectExprResultGetDonatedItems(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ContributionNotifyData *pNotify)
{
	DonationTaskBucketDonationResultFilter filter = {0};
	filter.bIncludeSuccessfulDonations = true;
	filter.bIncludePartialDonations = true;
	gclGroupProjectUI_GetDonationResultItemList(pGen, pNotify, &filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GenGroupProjectResultGetAttemptedItems");
void gclGroupProjectExprResultGetAttemptedItems(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_OP_VALID ContributionNotifyData *pNotify)
{
	DonationTaskBucketDonationResultFilter filter = {0};
	filter.bIncludeSuccessfulDonations = true;
	filter.bIncludePartialDonations = true;
	filter.bIncludeFailedDonations = true;
	gclGroupProjectUI_GetDonationResultItemList(pGen, pNotify, &filter);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetUnlockListProgress");
F32 gclGroupProjectExprGetUnlockListProgress(const char *pchUnlockList, const char *pchUnlockName, S32 iCurrentValue)
{
	S32 iLastValue = 0;
	char *pchBuffer, *pchToken, *pchContext;

	strdup_alloca(pchBuffer, pchUnlockList);

	if (pchToken = strtok_r(pchBuffer, " \r\n\t,%|", &pchContext))
	{
		do
		{
			GroupProjectUnlockDef *pUnlock = RefSystem_ReferentFromString("GroupProjectUnlockDef", pchToken);

			switch (pUnlock->type)
			{
			xcase UnlockType_NumericValueEqualOrGreater:
				{
					if (!stricmp(pchUnlockName, pchToken))
					{
						if (pUnlock->triggerValue == iLastValue)
							return (iCurrentValue - iLastValue) > 0 ? 1 : (iCurrentValue - iLastValue) < 0 ? -1 : 0;
						return (F32)(iCurrentValue - iLastValue) / (F32)(pUnlock->triggerValue - iLastValue);
					}

					iLastValue = pUnlock->triggerValue;
				}
			}
		}
		while (pchToken = strtok_r(NULL, " \r\n\t,%|", &pchContext));
	}

	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectCountUnlocked");
S32 gclGroupProjectExprCountUnlocked(U32 eProjectType, const char *pchGroupName, const char *pchUnlockList)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	char *pchBuffer, *pchToken, *pchContext;
	S32 iUnlocked = 0, i;

	strdup_alloca(pchBuffer, pchUnlockList);

	if (pGroupProject && (pchToken = strtok_r(pchBuffer, " \r\n\t,%|", &pchContext)))
	{
		do
		{
			GroupProjectUnlockDef *pUnlock = RefSystem_ReferentFromString("GroupProjectUnlockDef", pchToken);

			if (!pUnlock)
				continue;

			for (i = eaSize(&pGroupProject->unlockDefs) - 1; i >= 0; i--)
			{
				if (GET_REF(pGroupProject->unlockDefs[i]->unlockDef) == pUnlock)
					break;
			}
			if (i < 0)
				continue;

			switch (pUnlock->type)
			{
			xcase UnlockType_Manual:
				{
					for (i = eaSize(&pState->unlocks) - 1; i >= 0; i--)
					{
						if (GET_REF(pState->unlocks[i]->unlockDef) == pUnlock)
							break;
					}
				}
			xcase UnlockType_NumericValueEqualOrGreater:
				{
					for (i = eaSize(&pState->numericData) - 1; i >= 0; i--)
					{
						if (GET_REF(pState->numericData[i]->numericDef) && GET_REF(pState->numericData[i]->numericDef) == GET_REF(pUnlock->numeric))
							break;
					}
					if (i >= 0 && pState->numericData[i]->numericVal < pUnlock->triggerValue)
						i = -1;
				}
			xdefault:
				{
					i = -1;
				}
			}

			if (i >= 0)
				iUnlocked++;
		}
		while (pchToken = strtok_r(NULL, " \r\n\t,%|", &pchContext));
	}

	return iUnlocked;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectTaskIsActive");
bool gclGroupProjectExprTaskIsActive(U32 eProjectType, const char *pchGroupName, const char *pchTaskName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	DonationTaskDef *pTask = RefSystem_ReferentFromString("DonationTaskDef", pchTaskName);

	if (pState && pTask)
	{
		S32 i;
		for (i = eaSize(&pState->taskSlots) - 1; i >= 0; i--)
		{
			if (GET_REF(pState->taskSlots[i]->taskDef) == pTask)
				return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetNumeric");
SA_RET_NN_VALID GroupProjectNumericUI *gclGroupProjectExprGetNumeric(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchNumericName)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectNumericDef *pNumeric = RefSystem_ReferentFromString("GroupProjectNumericDef", pchNumericName);

	return gclGroupProjectUI_GetNumericValue(pGen, pState, pNumeric);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetNumericValue");
S32 gclGroupProjectExprGetNumericValue(U32 eProjectType, const char *pchGroupName, const char *pchNumericName)
{
	GroupProjectNumericDef *pNumeric = RefSystem_ReferentFromString("GroupProjectNumericDef", pchNumericName);

	if (pchGroupName && strchr(pchGroupName, '*'))
	{
		GroupProjectContainer *pContainer = gclGroupProject_ResolveContainer(eProjectType);
		S32 i, j, iTotal = 0;

		if (pContainer && (pNumeric || pchNumericName && strchr(pchNumericName, '*')))
		{
			for (i = eaSize(&pContainer->projectList) - 1; i >= 0; i--)
			{
				GroupProjectState *pState = pContainer->projectList[i];
				GroupProjectDef *pGroupProject = GET_REF(pState->projectDef);

				if (!pGroupProject || !isWildcardMatch(pchGroupName, pGroupProject->name, false, true))
					continue;

				for (j = eaSize(&pState->numericData) - 1; j >= 0; j--)
				{
					GroupProjectNumericDef *pDataDef = GET_REF(pState->numericData[j]->numericDef);
					if (pNumeric && pDataDef == pNumeric || !pNumeric && pDataDef && isWildcardMatch(pchNumericName, pDataDef->name, false, true))
						iTotal += pState->numericData[j]->numericVal;
				}
			}
		}

		return iTotal;
	}
	else
	{
		GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
		GroupProjectNumericUI *pNumericUI = gclGroupProjectUI_GetNumericValue(NULL, pState, pNumeric);
		return pNumericUI->iCurrentValue;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectFindItem");
const char *gclGroupProjectExprFindItem(U32 eProjectType, const char *pchGroupName, const char *pchNumericPattern)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	S32 i, j;

	if (pGroupProject)
	{
		GroupProjectDonationRequirement *pBucket = NULL;

		for (i = 0; i < eaSize(&pGroupProject->donationTaskDefs); i++)
		{
			DonationTaskDef *pTaskDef = GET_REF(pGroupProject->donationTaskDefs[i]->taskDef);
			if (!pTaskDef)
				continue;

			for (j = 0; j < eaSize(&pTaskDef->buckets); j++)
			{
				ItemDef *pItemDef;

				if (pTaskDef->buckets[j]->specType != DonationSpecType_Item)
					continue;

				pItemDef = GET_REF(pTaskDef->buckets[j]->requiredItem);
				if (!pItemDef)
					continue;

				if (isWildcardMatch(pchNumericPattern, pItemDef->pchName, false, true))
					return pItemDef->pchName;
			}
		}
	}

	return NULL;
}

static S32 GroupProjectLevelTreeNode_SortByNumericDisplayName(const GroupProjectNumericUI **ppNumericL, const GroupProjectNumericUI **ppNumericR, const void *pContext)
{
	return stricmp_safe((*ppNumericL)->pchDisplayName, (*ppNumericR)->pchDisplayName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetNumericList");
void gclGroupProjectExprGetNumericList(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName, const char *pchNumericNames)
{
	static char **s_eapchNameFilters;
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectNumericFilter Filter = {0};
	char *pchBuffer, *pchContext, *pchToken;

	strdup_alloca(pchBuffer, pchNumericNames);
	if (pchToken = strtok_r(pchBuffer, " \r\n\t,%|", &pchContext))
	{
		do
		{
			eaPush(&s_eapchNameFilters, pchToken);
		} while (pchToken = strtok_r(NULL, " \r\n\t,%|", &pchContext));
	}

	Filter.eapchNameFilters = s_eapchNameFilters;
	gclGroupProjectUI_GetNumericList(pGen, pState, &Filter);

	eaClearFast(&s_eapchNameFilters);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectCanStartRemoteContact");
bool gclGroupProjectExprCanStartRemoteContact(U32 eProjectType, const char *pchGroupName, const char *pchContactKey)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;

	if (pState)
	{
		GroupProjectRemoteContact *pRemoteContact = NULL;
		S32 i;

		for (i = eaSize(&pGroupProjectDef->remoteContacts) - 1; i >= 0; i--)
		{
			if (!stricmp(pGroupProjectDef->remoteContacts[i]->key, pchContactKey))
				pRemoteContact = pGroupProjectDef->remoteContacts[i];
		}

		if (pRemoteContact)
		{
			for (i = eaSize(&pRemoteContact->requiredUnlocks) - 1; i >= 0; i--)
			{
				if (gclGroupProjectUI_IsUnlocked(GET_REF(pRemoteContact->requiredUnlocks[i]->unlockDef), pState))
					return true;
			}

			if (!eaSize(&pRemoteContact->requiredUnlocks))
				return true;
		}
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectStartRemoteContact");
bool gclGroupProjectExprStartRemoteContact(U32 eProjectType, const char *pchGroupName, const char *pchContactKey)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProjectDef = pState ? GET_REF(pState->projectDef) : NULL;

	if (pState)
	{
		GroupProjectRemoteContact *pRemoteContact = NULL;
		S32 i;

		for (i = eaSize(&pGroupProjectDef->remoteContacts) - 1; i >= 0; i--)
		{
			if (!stricmp(pGroupProjectDef->remoteContacts[i]->key, pchContactKey))
				pRemoteContact = pGroupProjectDef->remoteContacts[i];
		}

		if (pRemoteContact)
		{
			for (i = eaSize(&pRemoteContact->requiredUnlocks) - 1; i >= 0; i--)
			{
				if (gclGroupProjectUI_IsUnlocked(GET_REF(pRemoteContact->requiredUnlocks[i]->unlockDef), pState))
					break;
			}

			if (i >= 0 || !eaSize(&pRemoteContact->requiredUnlocks))
			{
				ServerCmd_contact_StartRemoteContact(pRemoteContact->contactDef);
				return true;
			}
		}
	}

	return false;
}

static S32 GroupProjectLevelTreeNode_SortByNumericUnlockValue(const GroupProjectLevelTreeNodeUI **ppNodeL, const GroupProjectLevelTreeNodeUI **ppNodeR, const void *pContext)
{
	return (*ppNodeL)->pNumericUnlock->iUnlockReferenceValue - (*ppNodeR)->pNumericUnlock->iUnlockReferenceValue;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetLevelTree");
void gclGroupProjectExprGetLevelTree(SA_PARAM_NN_VALID UIGen *pGen, U32 eProjectType, const char *pchGroupName)
{
	static char s_achKey[1024];
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	GroupProjectDef *pGroupProject = pState ? GET_REF(pState->projectDef) : NULL;
	GroupProjectLevelTreeUI ***peaLevelTree = ui_GenGetManagedListSafe(pGen, GroupProjectLevelTreeUI);
	GroupProjectLevelTreeUI *pLevelTreeUI;
	GroupProjectLevelTreeNodeUI *pLevelTreeNodeUI;
	S32 i, j, iCount = 0;

	for (i = eaSize(peaLevelTree) - 1; i >= 0; i--)
		(*peaLevelTree)[i]->iTotalNodes = 0;

	if (pGroupProject)
	{
		// First Pass: Building up valid nodes
		for (i = 0; i < eaSize(&g_GroupProjectLevelTreeDef.eaLevelNodes); i++)
		{
			GroupProjectLevelTreeNodeDef *pNodeDef = g_GroupProjectLevelTreeDef.eaLevelNodes[i];
			GroupProjectUnlockDefRef *pNumericUnlockRef = pNodeDef->pchNumericUnlock ? eaIndexedGetUsingString(&pGroupProject->unlockDefs, pNodeDef->pchNumericUnlock) : NULL;
			GroupProjectUnlockDefRef *pManualUnlockRef = pNodeDef->pchManualUnlock ? eaIndexedGetUsingString(&pGroupProject->unlockDefs, pNodeDef->pchManualUnlock) : NULL;
			GroupProjectUnlockDef *pNumericUnlock = pNumericUnlockRef ? GET_REF(pNumericUnlockRef->unlockDef) : NULL;
			GroupProjectUnlockDef *pManualUnlock = pManualUnlockRef ? GET_REF(pManualUnlockRef->unlockDef) : NULL;
			GroupProjectNumericDef *pUnlockNumeric = pNumericUnlock && pNumericUnlock->type == UnlockType_NumericValueEqualOrGreater ? GET_REF(pNumericUnlock->numeric) : NULL;

			if (pNumericUnlock && pManualUnlock && pUnlockNumeric)
			{
				pLevelTreeUI = NULL;
				pLevelTreeNodeUI = NULL;

				// Ensure the Tree exists

				for (j = eaSize(peaLevelTree) - 1; j >= 0; j--)
				{
					if (GET_REF((*peaLevelTree)[j]->pUnlockNumeric->hGroupProjectNumeric) == pUnlockNumeric)
					{
						if (j >= iCount)
							pLevelTreeUI = eaRemove(peaLevelTree, j);
						else
							pLevelTreeUI = (*peaLevelTree)[j];
						break;
					}
				}

				if (!pLevelTreeUI)
				{
					pLevelTreeUI = StructCreate(parse_GroupProjectLevelTreeUI);
					pLevelTreeUI->pUnlockNumeric = StructCreate(parse_GroupProjectNumericUI);
					pLevelTreeUI->pGroupProject = StructCreate(parse_GroupProjectUI);
				}

				if (j < 0 || j >= iCount)
					eaInsert(peaLevelTree, pLevelTreeUI, iCount++);
				gclGroupProjectUI_UpdateNumeric(pLevelTreeUI->pUnlockNumeric, pUnlockNumeric, 0, pState);

				// Ensure the TreeNode exists

				for (j = eaSize(&pLevelTreeUI->eaTreeNodes) - 1; j >= 0; j--)
				{
					if (GET_REF(pLevelTreeUI->eaTreeNodes[j]->pNumericUnlock->hGroupProjectUnlock) == pNumericUnlock
						&& GET_REF(pLevelTreeUI->eaTreeNodes[j]->pManualUnlock->hGroupProjectUnlock) == pManualUnlock)
					{
						pLevelTreeNodeUI = eaRemove(&pLevelTreeUI->eaTreeNodes, j);
						break;
					}
				}

				if (!pLevelTreeNodeUI)
				{
					pLevelTreeNodeUI = StructCreate(parse_GroupProjectLevelTreeNodeUI);
					pLevelTreeNodeUI->pUnlockNumeric = StructCreate(parse_GroupProjectNumericUI);
					pLevelTreeNodeUI->pNumericUnlock = StructCreate(parse_GroupProjectUnlockUI);
					pLevelTreeNodeUI->pManualUnlock = StructCreate(parse_GroupProjectUnlockUI);
				}

				eaInsert(&pLevelTreeUI->eaTreeNodes, pLevelTreeNodeUI, pLevelTreeUI->iTotalNodes++);
				gclGroupProjectUI_UpdateNumeric(pLevelTreeNodeUI->pUnlockNumeric, pUnlockNumeric, 0, pState);
				gclGroupProjectUI_UpdateUnlock(pLevelTreeNodeUI->pNumericUnlock, pNumericUnlock, pState);
				gclGroupProjectUI_UpdateUnlock(pLevelTreeNodeUI->pManualUnlock, pManualUnlock, pState);
				pLevelTreeNodeUI->pchStyle = pNodeDef->pchStyle;
				pLevelTreeNodeUI->pchImage = pNodeDef->pchImage;
				pLevelTreeNodeUI->pchIcon = pNodeDef->pchIcon;
				pLevelTreeNodeUI->pchLevelMessage = pNodeDef->pchLevelMessage;
				pLevelTreeNodeUI->pchXPMessage = pNodeDef->pchXPMessage;
				pLevelTreeNodeUI->pchXPUnlockMessage = pNodeDef->pchXPUnlockMessage;
			}
		}
	}

	eaSetSizeStruct(peaLevelTree, parse_GroupProjectLevelTreeUI, iCount);

	// Second Pass: Sort and fill in tree
	for (i = eaSize(peaLevelTree) - 1; i >= 0; i--)
	{
		pLevelTreeUI = (*peaLevelTree)[i];

		eaSetSizeStruct(&pLevelTreeUI->eaTreeNodes, parse_GroupProjectLevelTreeNodeUI, pLevelTreeUI->iTotalNodes);
		eaStableSort(pLevelTreeUI->eaTreeNodes, NULL, GroupProjectLevelTreeNode_SortByNumericUnlockValue);

		gclGroupProjectUI_UpdateGroupProject(pLevelTreeUI->pGroupProject, pGroupProject, pState);

		if (eaSize(&pLevelTreeUI->eaTreeNodes) > 0)
		{
			pLevelTreeUI->pchImage = pLevelTreeUI->eaTreeNodes[0]->pchImage;
			pLevelTreeUI->pchLevelMessage = pLevelTreeUI->eaTreeNodes[0]->pchLevelMessage;
			pLevelTreeUI->pchXPMessage = pLevelTreeUI->eaTreeNodes[0]->pchXPMessage;
			pLevelTreeUI->pchXPUnlockMessage = pLevelTreeUI->eaTreeNodes[0]->pchXPUnlockMessage;
		}
		else
		{
			pLevelTreeUI->pchImage = NULL;
			pLevelTreeUI->pchLevelMessage = NULL;
			pLevelTreeUI->pchXPMessage = NULL;
			pLevelTreeUI->pchXPUnlockMessage = NULL;
		}

		pLevelTreeUI->iGrantedNumericNodes = 0;
		pLevelTreeUI->iGrantedManualNodes = 0;
		for (j = eaSize(&pLevelTreeUI->eaTreeNodes) - 1; j >= 0; j--)
		{
			S32 iBaseNumericValue = 0;
			const char *unlockNumericString;
			const char *numericUnlockString;
			const char *manualUnlockString;

			pLevelTreeNodeUI = pLevelTreeUI->eaTreeNodes[j];

			// Set node key
			s_achKey[0] = '\0';
			strcat(s_achKey, pGroupProject->name);
			strcat(s_achKey, ",");
			unlockNumericString = REF_STRING_FROM_HANDLE(pLevelTreeNodeUI->pUnlockNumeric->hGroupProjectNumeric);
			ANALYSIS_ASSUME(unlockNumericString);
			strcat(s_achKey, unlockNumericString);
			strcat(s_achKey, ",");
			numericUnlockString = REF_STRING_FROM_HANDLE(pLevelTreeNodeUI->pNumericUnlock->hGroupProjectUnlock);
			ANALYSIS_ASSUME(numericUnlockString);
			strcat(s_achKey, numericUnlockString);
			strcat(s_achKey, ",");
			manualUnlockString = REF_STRING_FROM_HANDLE(pLevelTreeNodeUI->pManualUnlock->hGroupProjectUnlock);
			ANALYSIS_ASSUME(manualUnlockString);
			strcat(s_achKey, manualUnlockString);
			if (pLevelTreeNodeUI->pchKey && stricmp(s_achKey, pLevelTreeNodeUI->pchKey))
				StructFreeStringSafe(&pLevelTreeNodeUI->pchKey);
			if (s_achKey[0] && !pLevelTreeNodeUI->pchKey)
				pLevelTreeNodeUI->pchKey = StructAllocString(s_achKey);

			if (j > 0)
				iBaseNumericValue = pLevelTreeUI->eaTreeNodes[j - 1]->pNumericUnlock->iUnlockReferenceValue;

			// Set parameters based on structures
			pLevelTreeNodeUI->iRequiredProgress = pLevelTreeNodeUI->pNumericUnlock->iUnlockReferenceValue - iBaseNumericValue;
			pLevelTreeNodeUI->iNumericProgress = pLevelTreeNodeUI->pUnlockNumeric->iCurrentValue - iBaseNumericValue;
			pLevelTreeNodeUI->bNumericStarted = pLevelTreeNodeUI->iNumericProgress >= 0;
			pLevelTreeNodeUI->bNumericUnlocked = pLevelTreeNodeUI->pNumericUnlock->bUnlocked;
			pLevelTreeNodeUI->bManualUnlocked = pLevelTreeNodeUI->pManualUnlock->bUnlocked;
			MAX1(pLevelTreeNodeUI->iNumericProgress, 0);
			MIN1(pLevelTreeNodeUI->iNumericProgress, pLevelTreeNodeUI->iRequiredProgress);

			// Determine status
			if (pLevelTreeNodeUI->bManualUnlocked)
				pLevelTreeNodeUI->eStatusNumber = kGroupProjectLevelTreeNodeState_Complete;
			else if (pLevelTreeNodeUI->bNumericUnlocked)
				pLevelTreeNodeUI->eStatusNumber = kGroupProjectLevelTreeNodeState_Ready;
			else if (pLevelTreeNodeUI->bNumericStarted)
				pLevelTreeNodeUI->eStatusNumber = kGroupProjectLevelTreeNodeState_Progress;
			else
				pLevelTreeNodeUI->eStatusNumber = kGroupProjectLevelTreeNodeState_Locked;
			pLevelTreeNodeUI->pchStatusName = StaticDefineIntRevLookup(GroupProjectLevelTreeNodeStatusEnum, pLevelTreeNodeUI->eStatusNumber);

			// Update counters
			if (pLevelTreeNodeUI->bManualUnlocked)
				pLevelTreeUI->iGrantedManualNodes++;
			if (pLevelTreeNodeUI->bNumericUnlocked)
				pLevelTreeUI->iGrantedNumericNodes++;
		}
	}

	ui_GenSetManagedListSafe(pGen, peaLevelTree, GroupProjectLevelTreeUI, true);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetLevelTreeNodes");
void gclGroupProjectExprGetLevelTreeNodes(SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID GroupProjectLevelTreeUI *pLevelTreeUI)
{
	ui_GenSetManagedListSafe(pGen, &pLevelTreeUI->eaTreeNodes, GroupProjectLevelTreeNodeUI, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("GroupProjectGetLevelTreeCount");
S32 gclGroupProjectExprGetLevelTreeCount(U32 eProjectType, const char *pchGroupName, const char *pchHint, U32 uFlags)
{
	GroupProjectState *pState = gclGroupProject_ResolveState(eProjectType, pchGroupName);
	return GroupProject_GetLevelTreeCount(pState, pchHint, uFlags);
}

AUTO_STARTUP(GroupProjectUI) ASTRT_DEPS(GroupProjects);
void GroupProjectUIStartup(void)
{
	ui_GenInitStaticDefineVars(GroupProjectTypeEnum, "GroupProject");
	ui_GenInitStaticDefineVars(GroupProjectTaskSlotTypeEnum, "GroupProjectTaskSlot");
	ui_GenInitStaticDefineVars(DonationTaskCategoryTypeEnum, "DonationTaskCategory");
	ui_GenInitStaticDefineVars(GroupProjectLevelTreeNodeStatusEnum, "GroupProjectLevelTreeNode");
	ui_GenInitStaticDefineVars(GroupProjectLevelTreeCountEnum, "GroupProjectLevelTreeCount");

}

#include "AutoGen/gclGroupProjectUI_h_ast.c"
