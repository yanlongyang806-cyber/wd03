/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ActivityCommon.h"
#include "contact_common.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "Expression.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslActivity.h"
#include "gslEventSend.h"
#include "gslInterior.h"
#include "gslItemAssignments.h"
#include "gslLogSettings.h"
#include "loggingEnums.h"
#include "LoggedTransactions.h"
#include "gslVolume.h"
#include "InteriorCommon.h"
#include "inventoryCommon.h"
#include "inventoryTransactions.h"
#include "itemTransaction.h"
#include "ItemAssignments.h"
#include "MapDescription.h"
#include "mission_common.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "qsortG.h"
#include "rand.h"
#include "RegionRules.h"
#include "Reward.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "WorldGrid.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "AutoGen/gslItemAssignments_c_ast.h"
#include "AutoGen/ItemAssignments_h_ast.h"
#include "AutoGen/Player_h_ast.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

#define ITEM_ASSIGNMENT_REQUEST_TIMEOUT 5

AUTO_STRUCT;
typedef struct ItemAssignmentCBData
{
	EntityRef erEnt;
	ContainerID eID;
	F32 fAssignmentSpeedBonus;
	ItemAssignmentDefRef** eaRefs;
	REF_TO(ItemAssignmentDef) hDef;
	const char* pchOutcome; AST(POOL_STRING)
	U32 uAssignmentID;
	S32 iPersonalBucketIndex;
	U32 *eaOldSlots;
} ItemAssignmentCBData;

AUTO_STRUCT;
typedef struct ItemAssignmentVolumeData
{
	const char* pchVolumeName; AST(POOL_STRING)
	int iPartitionIdx;
	ItemAssignmentDefRef** eaRefs;
	S32 iCurrentRefreshIndex;
} ItemAssignmentVolumeData;

AUTO_STRUCT;
typedef struct ItemAssignmentMapData
{
	const char* pchRequestMap; AST(POOL_STRING)
	ItemAssignmentVolumeData** eaVolumeData;
} ItemAssignmentMapData;

static ItemAssignmentMapData* s_pAvailableItemAssignmentsCached = NULL;
static S32 s_iRefreshIndexOffset = 0;
static const char* s_pchOverrideMap = NULL;
static const char* s_pchOverrideVolume = NULL;
static const char** s_ppchForceAssignmentAdds = NULL;
static const char** s_ppchTrackedAssignmentActivities = NULL;
static ItemAssignmentDefRefs* s_pRemoteAssignments = NULL;

static void gslItemAssignments_UpdateAutograntedAssignments(Entity* pEnt, bool bForce);


// if the ItemAssignmentDef has CompletionExperience, then we add its itemDef so the user can see the experience in the rewards
static void gslItemAssignmentDef_AddCategoryXPNumericToRewardRequest(ItemAssignmentDef* pDef, InvRewardRequest *pRewards)
{
	if (pDef->iCompletionExperience)
	{
		ItemAssignmentCategorySettings* pCategorySettings = ItemAssignmentCategory_GetSettings(pDef->eCategory);

		if (pCategorySettings && pCategorySettings->pchNumericXP1)
		{
			ItemDef *pItemDef = item_DefFromName(pCategorySettings->pchNumericXP1);
			if (pItemDef)
			{
				ItemNumericData *pXPNumeric = StructCreate(parse_ItemNumericData);
				REF_HANDLE_SET_FROM_STRING(g_hItemDict, pCategorySettings->pchNumericXP1, pXPNumeric->hDef);
				pXPNumeric->iNumericValue = pDef->iCompletionExperience;
				eaPush(&pRewards->eaNumericRewards, pXPNumeric);
			}
		}
	}
}

static bool earray32Compare(int **a, int **b)
{
	int i;

	if(ea32Size(a) != ea32Size(b))
		return false;

	for(i=0;i<ea32Size(a);i++)
	{
		if((*a)[i] != (*b)[i])
			return false;
	}

	return true;
}

static void gslItemAssignment_UpdateSlots_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pReturnData)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pReturnData->eID);
	ItemAssignmentPersistedData *pData = NULL;
	
	if(!pEnt)
		pEnt = entForClientCmd(pReturnData->eID,pEnt);

	if(pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		int i;

		pData = SAFE_MEMBER2(pEnt,pPlayer,pItemAssignmentPersistedData);

		if(!pData)
			return;

		for(i=0;i<ea32Size(&pData->eaItemAssignmentSlotsUnlocked);i++)
		{
			if(ea32Find(&pReturnData->eaOldSlots,pData->eaItemAssignmentSlotsUnlocked[i]) == -1)
			{
				ItemAssignmentSlotUnlockExpression *pUnlock = ItemAssignment_GetUnlockFromKey(pData->eaItemAssignmentSlotsUnlocked[i]);
				char *pchTemp = NULL;
				estrStackCreate(&pchTemp);
				FormatMessageKey(&pchTemp,"ItemAssignments_NewSlotUnlocked",STRFMT_MESSAGE("SlotRequirement",pUnlock->displayReason),STRFMT_END);
				//Notify
				notify_NotifySend(pEnt,kNotifyType_ItemAssignmentFeedback,pchTemp,"ItemAssignments_NewSlotUnlocked",NULL);
				estrDestroy(&pchTemp);
			}
		}
	}

	StructDestroy(parse_ItemAssignmentCBData,pReturnData);
}

void gslItemAssignments_CheckExpressionSlots(Entity *pEnt, ItemAssignmentCompletedDetails *pCompletedDetails)
{
	static U32 *eaiUnlocked = NULL;
	ItemAssignmentSettingsSlots *pSettings = g_ItemAssignmentSettings.pStrictAssignmentSlots;
	ItemAssignmentPersistedData *pData = SAFE_MEMBER2(pEnt,pPlayer,pItemAssignmentPersistedData);

	if(pSettings)
	{
		int i;
		ea32Clear(&eaiUnlocked);

		for(i=0;i<eaSize(&pSettings->ppUnlockExpression);i++)
		{
			if(ItemAssignments_CheckItemSlotExpression(pSettings->ppUnlockExpression[i],pEnt,pCompletedDetails))
			{
				ea32Push(&eaiUnlocked,pSettings->ppUnlockExpression[i]->key);
			}
		}

		if(pData && earray32Compare(&eaiUnlocked,&(U32*)pData->eaItemAssignmentSlotsUnlocked) == false)
		{
			ItemAssignmentUnlockedSlots *pSlots = StructCreate(parse_ItemAssignmentUnlockedSlots);
			ItemAssignmentCBData *pReturnData = StructCreate(parse_ItemAssignmentCBData);
			TransactionReturnVal *pReturn = LoggedTransactions_CreateManagedReturnValEnt("CompleteItemAssignment", pEnt, gslItemAssignment_UpdateSlots_CB, pReturnData);

			ea32Copy(&pSlots->eaSlots,&eaiUnlocked);
			ea32Copy(&pReturnData->eaOldSlots,&pData->eaItemAssignmentSlotsUnlocked);
			pReturnData->eID = entGetContainerID(pEnt);

			AutoTrans_ItemAssignmentsUpdateUnlockedSlots(pReturn,GetAppGlobalType(),GLOBALTYPE_ENTITYPLAYER,pEnt->myContainerID,pSlots);
		}
	}
}

// server fixup function for ItemAssignmentDef
// currently will create a sample reward list of items for the first ItemAssignmentOutcome 
void gslItemAssignmentDef_Fixup(ItemAssignmentDef* pDef)
{
	ItemAssignmentOutcome *pSampleRewardOutcome = eaTail(&pDef->eaOutcomes);
	if (g_ItemAssignmentSettings.bGenerateSampleRewardTable && pSampleRewardOutcome && pSampleRewardOutcome->pResults)
	{
		InventoryBag** eaRewardBags = NULL;
		RewardTable *pRewardTable = GET_REF(pSampleRewardOutcome->pResults->hRewardTable);
		if (pRewardTable)
		{
			U32 uSeed = 0;
			// Generate reward bags
			reward_GenerateBagsForItemAssignment(PARTITION_UNINITIALIZED, NULL, pRewardTable, 1, 1.f, 
													NULL, false, &uSeed, &eaRewardBags);
						
		}
		
		if (pDef->iCompletionExperience || eaSize(&eaRewardBags))
		{
			InvRewardRequest *pSampleRewards = NULL;
			pSampleRewardOutcome->pResults->pSampleRewards = StructCreate(parse_InvRewardRequest);
			pSampleRewards = pSampleRewardOutcome->pResults->pSampleRewards;
			if (eaSize(&eaRewardBags))
				inv_FillRewardRequest(eaRewardBags, pSampleRewardOutcome->pResults->pSampleRewards);

			gslItemAssignmentDef_AddCategoryXPNumericToRewardRequest(pDef, pSampleRewards);
		}
			
		eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
	}

}


///////////////////////////////////////////////////////////////////////////////////////////
// Helper functions
///////////////////////////////////////////////////////////////////////////////////////////

static ItemAssignmentCBData* gslItemAssignments_CreateCBData(Entity* pEnt,
															 ItemAssignment *pAssignment,
															 ItemAssignmentDef* pDef,
															 const char* pchOutcome)
{
	ItemAssignmentCBData* pData = StructCreate(parse_ItemAssignmentCBData);
	
	pData->erEnt = entGetRef(pEnt);
	pData->eID = entGetContainerID(pEnt);

	if (pchOutcome)
		pData->pchOutcome = allocAddString(pchOutcome);

	
	if (pAssignment)
		pData->uAssignmentID = pAssignment->uAssignmentID;

	if (pDef)
	{
		if (pAssignment && pAssignment->uDuration && pDef->uDuration)
		{
			pData->fAssignmentSpeedBonus = 1.0f - (F32)pAssignment->uDuration / (F32)pDef->uDuration;
			pData->fAssignmentSpeedBonus = CLAMP(pData->fAssignmentSpeedBonus, 0.f, 1.f);
		}

		SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pData->hDef);
	}
	return pData;
}

static void gslItemAssignments_NotifySendWithReason(Entity* pEnt, NotifyType eType,ItemAssignmentDef* pDef, const char* pchMsgKey, const char* pchReasonDisplayString)
{
	if (pEnt)
	{
		const char* pchDisplayName = "<InvalidAssignmentName>";
		char* estrMessage = NULL;
		estrStackCreate(&estrMessage);

		if (pDef)
		{
			pchDisplayName = entTranslateDisplayMessage(pEnt, pDef->msgDisplayName);
		}

		entFormatGameMessageKey(pEnt, &estrMessage, pchMsgKey, 
			STRFMT_STRING("ItemAssignmentName", pchDisplayName), 
			STRFMT_STRING("Reason", NULL_TO_EMPTY(pchReasonDisplayString)), 
			STRFMT_END);
		ClientCmd_NotifySend(pEnt, eType, estrMessage, pchMsgKey, NULL);

		estrDestroy(&estrMessage);
	}
}

static void gslItemAssignments_NotifySend(Entity* pEnt, ItemAssignmentDef* pDef, const char* pchMsgKey)
{
	gslItemAssignments_NotifySendWithReason(pEnt, kNotifyType_ItemAssignmentFeedback, pDef, pchMsgKey, NULL);
}

static void gslItemAssignments_NotifySendFailure(Entity* pEnt, ItemAssignmentDef* pDef, const char* pchMsgKey)
{
	gslItemAssignments_NotifySendWithReason(pEnt, kNotifyType_ItemAssignmentFeedbackFailed, pDef, pchMsgKey, NULL);
}


// Check to see if the player is in one of the valid volumes specified in the settings data
static bool gslItemAssignments_IsEntityInValidVolume(Entity* pEnt, const char** ppchVolumeName)
{
	if (pEnt && pEnt->pPlayer)
	{
		S32 i;
		for (i = eaSize(&pEnt->pPlayer->InteractStatus.eaInVolumes)-1; i >= 0; i--)
		{
			const char* pchVolumeName = pEnt->pPlayer->InteractStatus.eaInVolumes[i];
			if (eaFind(&g_ItemAssignmentSettings.ppchValidVolumes, pchVolumeName) >= 0)
			{
				if (ppchVolumeName)
				{
					(*ppchVolumeName) = pchVolumeName;
				}
				return true;
			}
		}
	}
	return false;
}

// Get cached volume data given a volume name
static ItemAssignmentVolumeData* gslItemAssignments_GetVolumeData(int iPartitionIdx, const char* pchVolumeName)
{
	if (s_pAvailableItemAssignmentsCached)
	{
		S32 i;
		for (i = eaSize(&s_pAvailableItemAssignmentsCached->eaVolumeData)-1; i >= 0; i--)
		{
			ItemAssignmentVolumeData* pVolumeData = s_pAvailableItemAssignmentsCached->eaVolumeData[i];
			if (pVolumeData->iPartitionIdx == iPartitionIdx &&
				pVolumeData->pchVolumeName == pchVolumeName)
			{
				return pVolumeData;
			}
		}
	}
	return NULL;
}

// Gets data about the player's current location
void gslItemAssignments_GetPlayerLocationDataEx(SA_PARAM_NN_VALID Entity* pEnt, bool bInteriorCheck, SA_PARAM_NN_VALID ItemAssignmentLocationData* pData)
{
	bool bDeriveMapTypeAndRegion = false;

	pData->pchMapName = zmapInfoGetPublicName(NULL);
	pData->pchVolumeName = NULL;
	pData->eMapType = zmapInfoGetMapType(NULL);
	pData->eRegionType = entGetWorldRegionTypeOfEnt(pEnt);
	pData->bInValidVolume = true;

	if (bInteriorCheck && g_ItemAssignmentSettings.bInteriorsUseLastStaticMap && InteriorCommon_IsCurrentMapInterior())
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		pData->pchMapName = gslInterior_GetMapOwnerReturnMap(iPartitionIdx);
		pData->pchVolumeName = gslInterior_GetOwnerLastItemAssignmentVolume(iPartitionIdx);
		bDeriveMapTypeAndRegion = true;

		if (g_ItemAssignmentSettings.bRequirePlayerInValidVolume)
		{
			if (!pData->pchVolumeName || eaFind(&g_ItemAssignmentSettings.ppchValidVolumes, pData->pchVolumeName) < 0)
			{
				pData->bInValidVolume = false;
			}
		}
	}
	else if (g_ItemAssignmentSettings.bRequirePlayerInValidVolume && 
			!gslItemAssignments_IsEntityInValidVolume(pEnt, &pData->pchVolumeName))
	{
		pData->bInValidVolume = false;
	}

	if (s_pchOverrideMap && s_pchOverrideMap != pData->pchMapName)
	{
		pData->pchMapName = s_pchOverrideMap;
		bDeriveMapTypeAndRegion = true;
	}
	if (s_pchOverrideVolume)
	{
		pData->pchVolumeName = s_pchOverrideVolume;
	}

	if (bDeriveMapTypeAndRegion)
	{
		ZoneMapInfo* pZoneMapInfo = zmapInfoGetByPublicName(pData->pchMapName);
		WorldRegion** eaWorldRegions = zmapInfoGetWorldRegions(pZoneMapInfo);
		
		// Get the map type
		pData->eMapType = zmapInfoGetMapType(pZoneMapInfo);

		// Guess at the region type
		if (eaSize(&eaWorldRegions) > 0)
		{
			RegionRules* pRules = getRegionRulesFromRegion(eaWorldRegions[0]);
			if (pRules)
			{
				pData->eRegionType = pRules->eRegionType;
			}
		}
	}
}

static U32 gslItemAssignments_GetSeedForPeriodicUpdate(const char* pchMapName, const char* pchVolumeName, S32 iRefreshIndex)
{
	S32 uSeed = 0;
	S32 iMapIdx;
	for (iMapIdx = eaSize(&g_ItemAssignmentSettings.eaMapSettings)-1; iMapIdx >= 0; iMapIdx--)
	{
		if (g_ItemAssignmentSettings.eaMapSettings[iMapIdx]->pchMapName == pchMapName)
		{
			break;
		}
	}
	if (iMapIdx >= 0 && iMapIdx < (1<<ITEM_ASSIGNMENT_SEED_NUM_MAP_BITS))
	{
		S32 iVolumeIdx = 0;
		if (pchVolumeName && pchVolumeName[0])
		{
			iVolumeIdx = eaFind(&g_ItemAssignmentSettings.ppchValidVolumes, pchVolumeName);
		}
		if (iVolumeIdx >= 0 && iVolumeIdx < (1<<ITEM_ASSIGNMENT_SEED_NUM_VOLUME_BITS))
		{
			uSeed |= iRefreshIndex << (ITEM_ASSIGNMENT_SEED_NUM_MAP_BITS+ITEM_ASSIGNMENT_SEED_NUM_VOLUME_BITS);
			uSeed |= iVolumeIdx << ITEM_ASSIGNMENT_SEED_NUM_MAP_BITS;
			uSeed |= iMapIdx;
		}
	}
	return uSeed;
}

static S32 gslItemAssignment_RefListFindRarity(ItemAssignmentRefsInRarity** eaRarityRefs, ItemAssignmentWeightType eWeight)
{
	S32 i;
	for (i = eaSize(&eaRarityRefs)-1; i >= 0; i--)
	{
		if (eaRarityRefs[i]->eWeight == eWeight)
		{
			return i;
		}
	}
	return -1;
}

// Randomly selects an ItemAssignment from the passed in list
static S32 gslItemAssignments_GetRandomRarity(ItemAssignmentRefsInRarity** eaRarityRefs, U32* pSeed, bool bFeaturedList)
{
	F32 fTotalWeight = 0.0f;
	F32 fCurWeight = 0.0f;
	F32 fRandomChoiceValue;
	S32 i, iSize = eaSize(&eaRarityRefs);

	for (i = iSize-1; i >= 0; i--)
	{
		if ((bFeaturedList && eaSize(&eaRarityRefs[i]->eaFeaturedRefs)) || 
			(!bFeaturedList && eaSize(&eaRarityRefs[i]->eaStandardRefs)))
		{
			fTotalWeight += ItemAssignmentWeightType_GetWeightValue(eaRarityRefs[i]->eWeight);
		}
	}

	if (fTotalWeight > 0.0f)
	{
		fRandomChoiceValue = randomPositiveF32Seeded(pSeed, RandType_LCG) * fTotalWeight;
		for (i = 0; i < iSize; i++)
		{
			if ((bFeaturedList && eaSize(&eaRarityRefs[i]->eaFeaturedRefs)) || 
				(!bFeaturedList && eaSize(&eaRarityRefs[i]->eaStandardRefs)))
			{
				fCurWeight += ItemAssignmentWeightType_GetWeightValue(eaRarityRefs[i]->eWeight);
				if (fRandomChoiceValue <= fCurWeight)
				{
					return i;
				}
			}
		}
	}
	return -1;
}

static bool gslItemAssignment_ShouldFilterByLocation(SA_PARAM_OP_VALID ItemAssignmentDef* pDef,
													 ItemAssignmentLocationData* pLocationData)
{
	if (pDef && pDef->pRequirements && pLocationData)
	{
		if (eaSize(&pDef->pRequirements->ppchRequiredMaps) &&
			eaFind(&pDef->pRequirements->ppchRequiredMaps, pLocationData->pchMapName) < 0)
		{
			return true;
		}
		else if (eaSize(&pDef->pRequirements->ppchRequiredVolumes) &&
				 eaFind(&pDef->pRequirements->ppchRequiredVolumes, pLocationData->pchVolumeName) < 0)
		{
			return true;
		}
		else if (eaiSize(&pDef->pRequirements->peRequireMapTypes) &&
				 eaiFind(&pDef->pRequirements->peRequireMapTypes, pLocationData->eMapType) < 0)
		{
			return true;
		}
		else if (eaiSize(&pDef->pRequirements->peRequiredRegionTypes) &&
				 eaiFind(&pDef->pRequirements->peRequiredRegionTypes, pLocationData->eRegionType) < 0)
		{
			return true;
		}
	}
	return false;
}	

static void gslItemAssignments_BuildRarityRefList(ItemAssignmentRefsInRarity*** peaItemAssignmentRarityData)
{
	RefDictIterator pDefIter;
	ItemAssignmentDef* pDef;

	PERFINFO_AUTO_START_FUNC();

	// Create a data structure to optimize lookups
	RefSystem_InitRefDictIterator(g_hItemAssignmentDict, &pDefIter);
	while (pDef = (ItemAssignmentDef*)RefSystem_GetNextReferentFromIterator(&pDefIter))
	{
		ItemAssignmentRefsInRarity* pRarityRef;
		ItemAssignmentDefRef* pRef;
		S32 i;

		if (pDef->bDisabled)
		{
			continue;
		}

		i = gslItemAssignment_RefListFindRarity(*peaItemAssignmentRarityData, pDef->eWeight);
		if (i < 0)
		{
			pRarityRef = StructCreate(parse_ItemAssignmentRefsInRarity);
			pRarityRef->eWeight = pDef->eWeight;
			eaPush(peaItemAssignmentRarityData, pRarityRef);
		}
		else
		{
			pRarityRef = (*peaItemAssignmentRarityData)[i];
		}

		pRef = StructCreate(parse_ItemAssignmentDefRef);
		SET_HANDLE_FROM_REFERENT("ItemAssignmentDef", pDef, pRef->hDef);

		if (pDef->pchFeaturedActivity && pDef->pchFeaturedActivity[0])
		{
			ItemAssignmentActivityRefs* pActivityRefs = eaIndexedGetUsingString(&pRarityRef->eaActivityRefs, pDef->pchFeaturedActivity);
			if (!pActivityRefs)
			{
				pActivityRefs = StructCreate(parse_ItemAssignmentActivityRefs);
				pActivityRefs->pchActivity = allocAddString(pDef->pchFeaturedActivity);
				eaIndexedEnable(&pRarityRef->eaActivityRefs, parse_ItemAssignmentActivityRefs);
				eaPush(&pRarityRef->eaActivityRefs, pActivityRefs);
			}
			eaIndexedEnable(&pActivityRefs->eaRefs, parse_ItemAssignmentDefRef);
			eaPush(&pActivityRefs->eaRefs, pRef);
		}
		else
		{
			eaIndexedEnable(&pRarityRef->eaRefs, parse_ItemAssignmentDefRef);
			eaPush(&pRarityRef->eaRefs, pRef);
		}
	}

	PERFINFO_AUTO_STOP();
}

// Build a tree structure to optimize lookups when generating assignment lists
// TODO(MK): This is currently built on demand. If this is problematic for run-time perfomance, call it during load-time. 
static ItemAssignmentRefsInRarity** gslItemAssignments_GetRarityRefList(void)
{
	static ItemAssignmentRefsInRarity** s_eaItemAssignmentRarityData = NULL;
	if (g_bRebuildItemAssignmentTree || !eaSize(&s_eaItemAssignmentRarityData))
	{
		eaClearStruct(&s_eaItemAssignmentRarityData, parse_ItemAssignmentRefsInRarity);
		gslItemAssignments_BuildRarityRefList(&s_eaItemAssignmentRarityData);
		g_bRebuildItemAssignmentTree = false;
	}
	return s_eaItemAssignmentRarityData;
}

static void gslItemAssignments_BuildTemporaryRarityLists(ItemAssignmentRefsInRarity** eaChosenRarities, bool bBuildFeaturedList)
{
	S32 i, j;
	for (i = 0; i < eaSize(&eaChosenRarities); i++)
	{
		ItemAssignmentRefsInRarity* pRarityList = eaChosenRarities[i];
		for (j = 0; j < eaSize(&pRarityList->eaActivityRefs); j++)
		{
			ItemAssignmentActivityRefs* pActRefs = pRarityList->eaActivityRefs[j];
			if (bBuildFeaturedList && gslActivity_IsActive(pActRefs->pchActivity))
			{
				eaPushEArray(&pRarityList->eaFeaturedRefs, &pActRefs->eaRefs);
			}
			else
			{
				eaPushEArray(&pRarityList->eaStandardRefs, &pActRefs->eaRefs);
			}
		}
		eaPushEArray(&pRarityList->eaStandardRefs, &pRarityList->eaRefs);
	}
}

static void gslItemAssignments_ClearTemporaryRarityLists(ItemAssignmentRefsInRarity** eaChosenRarities)
{
	S32 i;
	for (i = 0; i < eaSize(&eaChosenRarities); i++)
	{
		ItemAssignmentRefsInRarity* pRarityList = eaChosenRarities[i];
		eaClearFast(&pRarityList->eaStandardRefs);
		eaClearFast(&pRarityList->eaFeaturedRefs);
	}
}

static void gslItemAssignments_GenerateAssignmentsForRarities(ItemAssignmentRefsInRarity** eaChosenRarities,
															  ItemAssignmentLocationData* pLocationData,
															  U32* pSeed,
															  S32 iAssignmentCount,
															  bool bFeaturedList,
															  ItemAssignmentDefRef*** peaAssignmentsOut)
{
	S32 i;
	// Generate the specified number of assignments for the current category
	for (i = 0; i < iAssignmentCount; i++)
	{
		S32 iIdx = gslItemAssignments_GetRandomRarity(eaChosenRarities, pSeed, bFeaturedList);
		if (iIdx >= 0)
		{
			ItemAssignmentRefsInRarity* pRarityList = eaChosenRarities[iIdx];
			ItemAssignmentDefRef*** peaRefs;
			ItemAssignmentDef* pCheckDef;
			S32 iListSize;
			S32 iRandomIndex = 0;
			S32 iNumAttempts = 0;
			bool bValidAssignment = false;

			if (bFeaturedList)
			{
				peaRefs = &pRarityList->eaFeaturedRefs;
			}
			else
			{
				peaRefs = &pRarityList->eaStandardRefs;
			}
			iListSize = eaSize(peaRefs);

			while (!bValidAssignment && iListSize > 0 && (++iNumAttempts) < 100)
			{
				iRandomIndex = randomIntRangeSeeded(pSeed, RandType_LCG, 0, iListSize-1);
				pCheckDef = GET_REF((*peaRefs)[iRandomIndex]->hDef);
				bValidAssignment = true;

				if (gslItemAssignment_ShouldFilterByLocation(pCheckDef, pLocationData))
				{
					bValidAssignment = false;
				}
			} 

			if (bValidAssignment)
			{
				// Remove the chosen assignment so that it is not chosen again
				ItemAssignmentDefRef* pChosenRef = eaRemove(peaRefs, iRandomIndex);

				// Push the chosen assignment onto the result list
				eaIndexedEnable(peaAssignmentsOut, parse_ItemAssignmentDefRef);
				eaPush(peaAssignmentsOut, StructClone(parse_ItemAssignmentDefRef, pChosenRef));
			}
		}
	}
}

static void gslItemAssignments_CacheHiddenCategories(Entity *pEnt, S32 **ppiCategoryIsHidden)
{
	const ItemAssignmentCategorySettings **eaCategoryList = ItemAssignmentCategory_GetCategoryList( );

	S32 iSize = eaSize(&eaCategoryList);

	if (iSize)
	{
		// categories start from kItemAssignmentWeightType_Default, first data defined should be 1
		eaiSetSize(ppiCategoryIsHidden, iSize + 1);
		FOR_EACH_IN_EARRAY(eaCategoryList, const ItemAssignmentCategorySettings, pCategory)
		{
			S32 bIsHidden = ItemAssignments_EvaluateCategoryIsHidden(pEnt, pCategory);
			eaiSet(ppiCategoryIsHidden, bIsHidden, FOR_EACH_IDX(-, pCategory) + 1);
		}
		FOR_EACH_END
	}
}

// returns true if the given ItemAssignmentWeightType
static bool gslItemAssignments_IsItemAssignmentWeightTypeHiddenByCategory(	ItemAssignmentWeightType eType,
																			S32 *piCategoryIsHidden)
{
	// check to see if we need to filter out/hide this by the category it could be in
	// the weights must have a category set in ItemAssignmentWeights.def in order to do this lookup
	ItemAssignmentWeight *pWeightDef = ItemAssignmentWeightType_GetWeightDef(eType);
	if (pWeightDef && pWeightDef->eCategory != kItemAssignmentWeightType_Default)
	{
		ItemAssignmentCategorySettings* pCategory = ItemAssignmentCategory_GetSettings(pWeightDef->eCategory);
		if (pCategory)
		{
			return eaiGet(&piCategoryIsHidden, pCategory->eCategory);
		}
	}

	return false;
}

void gslItemAssignments_GenerateAssignmentList(Entity* pEnt,
											   ItemAssignmentRarityCountType* peRarityCounts,
											   ItemAssignmentLocationData* pLocationData,
											   U32* pSeed,
											   ItemAssignmentDefRef*** peaAssignmentsOut)
{
	S32 i;
	ItemAssignmentRefsInRarity** eaRarityLists = gslItemAssignments_GetRarityRefList();
	S32 *piCategoryIsHidden = NULL;

	PERFINFO_AUTO_START_FUNC();

	gslItemAssignments_CacheHiddenCategories(pEnt, &piCategoryIsHidden);


	for (i = eaiSize(&peRarityCounts)-1; i >= 0; i--)
	{
		ItemAssignmentRarityCount* pRarityCount = ItemAssignment_GetRarityCountByType(peRarityCounts[i]);
		ItemAssignmentRefsInRarity** eaChosenRarities = NULL;

		if(!pRarityCount)
			continue;

		// Find the category in the list
		FOR_EACH_IN_EARRAY(eaRarityLists, ItemAssignmentRefsInRarity, pRefsInRarity)
		{
			if (eaiFind(&pRarityCount->peWeights, pRefsInRarity->eWeight) < 0)
				continue;

			// check to see if we need to filter out/hide this by the category it could be in
			// the weights must have a category set in ItemAssignmentWeights.def in order to do this lookup
			if (gslItemAssignments_IsItemAssignmentWeightTypeHiddenByCategory(pRefsInRarity->eWeight, piCategoryIsHidden))
			{
				continue;
			}

			eaPush(&eaChosenRarities, pRefsInRarity);
		}
		FOR_EACH_END

		if (eaSize(&eaChosenRarities))
		{
			gslItemAssignments_BuildTemporaryRarityLists(eaChosenRarities, pRarityCount->iFeaturedAssignmentCount > 0);
			gslItemAssignments_GenerateAssignmentsForRarities(eaChosenRarities, pLocationData, pSeed, pRarityCount->iAssignmentCount, false, peaAssignmentsOut);
			gslItemAssignments_GenerateAssignmentsForRarities(eaChosenRarities, pLocationData, pSeed, pRarityCount->iFeaturedAssignmentCount, true, peaAssignmentsOut);
			gslItemAssignments_ClearTemporaryRarityLists(eaChosenRarities);
			eaDestroy(&eaChosenRarities);
		}
	}

	if (eaSize(&s_ppchForceAssignmentAdds) > 0)
	{
		for (i = 0; i < eaSize(&s_ppchForceAssignmentAdds); i++)
		{
			if (eaIndexedFindUsingString(peaAssignmentsOut, s_ppchForceAssignmentAdds[i]) < 0)
			{
				ItemAssignmentDefRef *pForcedDef = StructCreate(parse_ItemAssignmentDefRef);
				SET_HANDLE_FROM_STRING(g_hItemAssignmentDict, s_ppchForceAssignmentAdds[i], pForcedDef->hDef);
				eaIndexedEnable(peaAssignmentsOut, parse_ItemAssignmentDefRef);
				eaPush(peaAssignmentsOut, pForcedDef);
			}
		}
	}

	eaiDestroy(&piCategoryIsHidden);

	PERFINFO_AUTO_STOP_FUNC();
}

// Randomly selects an ItemAssignmentOutcome using the passed in weights
static S32 gslItemAssignments_GetRandomOutcome(F32* pfOutcomeWeights, U32* pSeed)
{
	F32 fTotalWeight = 0.0f;
	F32 fCurWeight = 0.0f;
	F32 fRandomChoiceValue;
	S32 i, iSize = eaiSize(&pfOutcomeWeights);

	for (i = iSize-1; i >= 0; i--)
	{
		fTotalWeight += pfOutcomeWeights[i];
	}

	fRandomChoiceValue = randomPositiveF32Seeded(pSeed, RandType_LCG) * fTotalWeight;
	for (i = 0; i < iSize; i++)
	{
		fCurWeight += pfOutcomeWeights[i];
		if (fRandomChoiceValue <= fCurWeight)
		{
			return i;
		}
	}
	return -1;
}

// Choose a random outcome using the current time as the seed
// If pchForceOutcome is valid, specifically choose that outcome instead. pchForceOutcome should only be used by debug commands.
static ItemAssignmentOutcome* gslItemAssignments_ChooseOutcome(Entity* pEnt, ItemAssignment* pAssignment, const char* pchForceOutcome)
{
	ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
	ItemAssignmentOutcome* pResultOutcome = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	if (pchForceOutcome && pDef)
	{
		const char* pchForceOutcomePooled = allocFindString(pchForceOutcome);
		if (pchForceOutcomePooled)
		{
			S32 i;
			for (i = eaSize(&pDef->eaOutcomes)-1; i >= 0; i--)
			{
				if (pchForceOutcomePooled == pDef->eaOutcomes[i]->pchName)
				{
					pResultOutcome = pDef->eaOutcomes[i];
					break;
				}
			}
		}
		if (!pResultOutcome)
		{
			Errorf("Couldn't find Outcome %s for ItemAssignment %s", pchForceOutcome, pDef->pchName);
		}
	}
	else if (pEnt && pDef)
	{
		F32* pfOutcomeWeights = NULL;
		ItemAssignmentSlottedItem** eaSlottedItems = (ItemAssignmentSlottedItem**)pAssignment->eaSlottedItems;
		ItemAssignments_CalculateOutcomeWeights(pEnt, pDef, eaSlottedItems, &pfOutcomeWeights);

		// Choose a random outcome using the calculated weights
		pResultOutcome = eaGet(&pDef->eaOutcomes, gslItemAssignments_GetRandomOutcome(pfOutcomeWeights, NULL));

		// Cleanup
		eaiDestroy(&pfOutcomeWeights);
	}
	PERFINFO_AUTO_STOP_FUNC();
	return pResultOutcome;
}

static F32 gslItemAssignment_GetScaleAllNumericsValue(Entity* pEnt, ItemAssignmentOutcome* pOutcome)
{
	F32 fResult = 1.0f;
	if (pOutcome->pExprScaleAllNumerics)
	{
		MultiVal mVal;
		ExprContext* pContext = ItemAssignments_GetContext(pEnt);
		exprEvaluate(pOutcome->pExprScaleAllNumerics, pContext, &mVal);
		fResult = (F32)MultiValGetFloat(&mVal,NULL);
	}
	return fResult;
}

static void gslItemAssignmentOutcome_AddNumericRewardScale(const char* pchNumeric, F32 fScale, RewardNumericScale*** peaNumericScales)
{
	RewardNumericScale* pNumericScale = eaIndexedGetUsingString(peaNumericScales, pchNumeric);
	if (!pNumericScale)
	{
		pNumericScale = StructCreate(parse_RewardNumericScale);
		pNumericScale->pchNumericItem = allocAddString(pchNumeric);
		pNumericScale->fScale = 1.0f;
		eaIndexedEnable(peaNumericScales, parse_RewardNumericScale);
		eaPush(peaNumericScales, pNumericScale);
	}
	pNumericScale->fScale *= fScale;
}

// Wrapper for reward_GenerateBagsForItemAssignment. Generates rewards for an outcome.
static bool gslItemAssignmentOutcome_GenerateRewards(Entity* pEnt, 
													 ItemAssignment* pAssignment,
													 ItemAssignmentCompletedDetails* pCompletedDetails,
													 ItemAssignmentDef* pDef,
													 ItemAssignmentOutcome* pOutcome, 
													 bool bScaleRewards,
													 InventoryBag*** peaRewardBags)
{
	RewardTable* pRewardTable = NULL;
	U32 uSeed = 0;
		
	if (pAssignment)
	{
		if (pAssignment->pchRewardOutcome)
		{
			pOutcome = ItemAssignment_GetOutcomeByName(pDef, pAssignment->pchRewardOutcome);
		}
		uSeed = pAssignment->uTimeStarted;
	}
	else if (pCompletedDetails)
	{
		pOutcome = ItemAssignment_GetOutcomeByName(pDef, pCompletedDetails->pchOutcome);
		uSeed = pCompletedDetails->uTimeStarted;
	}
	if (pOutcome && pOutcome->pResults)
	{
		pRewardTable = GET_REF(pOutcome->pResults->hRewardTable);
	}
	if (pDef && pRewardTable)
	{
		S32 iLevel = entity_GetSavedExpLevel(pEnt);
		int iPartIdx = GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER ? 1 : entGetPartitionIdx(pEnt);
		F32 fScale = 1.0f;
		RewardNumericScale** eaNumericScales = NULL;
		bool bUseRewardMods = !g_ItemAssignmentSettings.bDisableRewardModifiers;

		if (bScaleRewards)
		{
			F32 fDurationScale = ItemAssignment_GetDurationScale(pAssignment, pDef);
			F32 fQualityScale = ItemAssignment_GetNumericQualityScale(pEnt, pAssignment, pCompletedDetails, pDef);
			int i;

			// Always apply the scale all numerics value
			fScale *= gslItemAssignment_GetScaleAllNumericsValue(pEnt, pOutcome);

			// Apply the duration scale value
			if (eaSize(&g_ItemAssignmentSettings.ppchDurationScaleNumerics))
			{
				for (i = eaSize(&g_ItemAssignmentSettings.ppchDurationScaleNumerics)-1; i >= 0; i--)
				{
					const char* pchNumeric = g_ItemAssignmentSettings.ppchDurationScaleNumerics[i];
					gslItemAssignmentOutcome_AddNumericRewardScale(pchNumeric, fDurationScale, &eaNumericScales);
				}
			}
			else
			{
				fScale *= fDurationScale;
			}

			// Apply the quality scale value
			if (eaSize(&g_ItemAssignmentSettings.ppchQualityScaleNumerics))
			{
				for (i = eaSize(&g_ItemAssignmentSettings.ppchQualityScaleNumerics)-1; i >= 0; i--)
				{
					const char* pchNumeric = g_ItemAssignmentSettings.ppchQualityScaleNumerics[i];
					gslItemAssignmentOutcome_AddNumericRewardScale(pchNumeric, fQualityScale, &eaNumericScales);
				}
			}
			else
			{
				fScale *= fQualityScale;
			}
		}

		// Generate reward bags
		reward_GenerateBagsForItemAssignment(iPartIdx, pEnt, pRewardTable, iLevel, fScale, eaNumericScales, bUseRewardMods, &uSeed, peaRewardBags);
		// Cleanup
		eaDestroyStruct(&eaNumericScales, parse_RewardNumericScale);

		return eaSize(peaRewardBags) > 0;
	}
	return false;
}

// Update uNextUpdateTime so that the player tick function knows when to update next 
// without requiring a full traversal of all assignments each tick
void gslItemAssignments_PlayerUpdateNextProcessTime(Entity* pEnt)
{
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	if (pPersistedData)
	{
		S32 i;
		U32 uCurrentTime = timeSecondsSince2000();
		S32 iPersistedListSize = 0;

		PERFINFO_AUTO_START_FUNC();
		
		iPersistedListSize = eaSize(&pPersistedData->eaPersistedPersonalAssignmentBuckets);

		// check all our personal assignment buckets to get the next time we should update
		if (iPersistedListSize)
		{
			S32 iPersonalSettingsSize = eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings);
			if (iPersistedListSize != iPersonalSettingsSize)
			{	// our lists aren't the same size, force an update
				pPersistedData->uNextUpdateTime = 0;
			}
			else
			{
				FOR_EACH_IN_EARRAY(pPersistedData->eaPersistedPersonalAssignmentBuckets, ItemAssignmentPersonalPersistedBucket, pBucket)
				{
					ItemAssignmentPersonalAssignmentSettings *pPersonalSettings;
					pPersonalSettings = eaGet(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings, FOR_EACH_IDX(-, pBucket));

					if (pPersonalSettings && pBucket->uLastPersonalUpdateTime)
					{
						U32 uLastPersonalUpdateTime = pBucket->uLastPersonalUpdateTime;
						U32 uRefreshTime = pPersonalSettings->uAssignmentRefreshTime;

						pPersistedData->uNextUpdateTime = uLastPersonalUpdateTime + uRefreshTime;
					}
					else
					{
						pPersistedData->uNextUpdateTime = 0;
						break;
					}
				}
				FOR_EACH_END
			}
		}
		else
		{
			pPersistedData->uNextUpdateTime = 0;
		}

		// if we're not going to update next, see if we have anything completing that we should update for
		if (pPersistedData->uNextUpdateTime > uCurrentTime)
		{
			for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
			{
				ItemAssignment* pAssignment = pPersistedData->eaActiveAssignments[i];
				ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
				if (pDef && !pAssignment->pchRewardOutcome)
				{
					U32 uCompleteTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);

					if (!pPersistedData->uNextUpdateTime || uCompleteTime < pPersistedData->uNextUpdateTime)
					{
						pPersistedData->uNextUpdateTime = uCompleteTime;
						if (pEnt->pPlayer->pItemAssignmentData)
							pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime = uCompleteTime - 1;
					}
				}
			}

			if (pEnt->pPlayer->pItemAssignmentData && 
				uCurrentTime >= pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime)
			{
				 pPersistedData->uNextUpdateTime = pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime;
			}
		}

		PERFINFO_AUTO_STOP();
	}
}

// Filter out assignments that should not be shown in the player's available assignment list
bool gslItemAssignments_IsValidAvailableAssignment(Entity* pEnt, ItemAssignmentDef* pDef, ItemAssignmentLocationData* pLocationData)
{
	ItemAssignmentPersistedData* pPersistedData;
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	if (!pEnt || !pDef)
	{
		return false;
	}

	
	if (!SAFE_MEMBER(pPlayerData, bUnfilterExpression) && !ItemAssignments_CheckRequirementsExpression(pDef, pEnt))
	{
		return false;
	}
	if (pDef->pRequirements)
	{
		if (!SAFE_MEMBER(pPlayerData, bUnfilterAllegiance) && GET_REF(pDef->pRequirements->hRequiredAllegiance))
		{
			if (GET_REF(pEnt->hAllegiance) != GET_REF(pDef->pRequirements->hRequiredAllegiance) &&
				GET_REF(pEnt->hSubAllegiance) != GET_REF(pDef->pRequirements->hRequiredAllegiance))
			{
				return false;
			}
		}
		if (!SAFE_MEMBER(pPlayerData, bUnfilterMaximumLevel) && pDef->pRequirements->iMaximumLevel)
		{
			S32 iEntLevel = entity_GetSavedExpLevel(pEnt);
			if (iEntLevel > pDef->pRequirements->iMaximumLevel)
			{
				return false;
			}
		}
	}
	
    pPersistedData = SAFE_MEMBER(pEnt->pPlayer, pItemAssignmentPersistedData);

	if (!SAFE_MEMBER(pPlayerData, bUnfilterActive) && pPersistedData)
	{
		S32 iMaxUniqueAssignments = 0;
		// check if we are allowed to have multiple instances of this assignment running

		iMaxUniqueAssignments = pDef->iUniqueAssignmentCount;

		if (iMaxUniqueAssignments == 0 && 
			!g_ItemAssignmentSettings.bAllowDuplicateActiveAssignments)
		{	// no max is set, but by the default setting, bAllowDuplicateActiveAssignments, we are only allowed one
			iMaxUniqueAssignments = 1;
		}

		if (iMaxUniqueAssignments > 0)
		{
			S32 iActiveCount = 0;
			FOR_EACH_IN_EARRAY(pPersistedData->eaActiveAssignments, ItemAssignment, pActiveAssignment)
			{
				if (!pActiveAssignment->pchRewardOutcome)
				{	// hasn't been completed yet
					ItemAssignmentDef* pActiveDef = GET_REF(pActiveAssignment->hDef);
					if (pDef == pActiveDef)
					{	
						iActiveCount++;
						if (iActiveCount >= iMaxUniqueAssignments)
							return false;
					}
				}
			}
			FOR_EACH_END
		}
	}
	
	// Filter out non-repeatable assignments that have already been completed or assignments in cooldown
	if (!pDef->bRepeatable || pDef->uCooldownAfterCompletion)
	{
		ItemAssignmentCompleted* pCompleted = ItemAssignments_PlayerGetCompletedAssignment(pEnt, pDef);
		if (!SAFE_MEMBER(pPlayerData, bUnfilterNotRepeatable) && pCompleted && !pDef->bRepeatable)
		{
			return false;
		}
		else if (!SAFE_MEMBER(pPlayerData, bUnfilterCooldown) && pCompleted && pDef->uCooldownAfterCompletion)
		{
			U32 uCurrentTime = timeSecondsSince2000();
			U32 uFinishCooldown = pCompleted->uCompleteTime + pDef->uCooldownAfterCompletion;
			if (uCurrentTime < uFinishCooldown)
			{
				return false;
			}
		}
	}
	// Filter out assignments that do not have their required assignment or mission completed yet
	if (pDef->pRequirements)
	{
		ItemAssignmentDef* pRequiredAssignment = GET_REF(pDef->pRequirements->hRequiredAssignment);
		MissionDef* pRequiredMission = GET_REF(pDef->pRequirements->hRequiredMission);

		if (!SAFE_MEMBER(pPlayerData, bUnfilterRequiredAssignment) && pRequiredAssignment && !ItemAssignments_PlayerGetCompletedAssignment(pEnt, pRequiredAssignment))
		{
			return false;
		}
		if (!SAFE_MEMBER(pPlayerData, bUnfilterRequiredMission) && pRequiredMission && !mission_GetCompletedMissionByDef(mission_GetInfoFromPlayer(pEnt), pRequiredMission))
		{
			return false;
		}
	}
	// Check to see if this assignment should be filtered by location
	if (!SAFE_MEMBER(pPlayerData, bUnfilterLocation) && gslItemAssignment_ShouldFilterByLocation(pDef, pLocationData))
	{
		return false;
	}
	return true;
}

bool gslItemAssignment_IsFeatured(ItemAssignmentDef* pDef)
{
	if (pDef && pDef->pchFeaturedActivity && pDef->pchFeaturedActivity[0])
	{
		return gslActivity_IsActive(pDef->pchFeaturedActivity);
	}
	return false;
}

// creates ItemAssignmentPersistedData struct and initializes with necessary default fields
static NOCONST(ItemAssignmentPersistedData)* ItemAssignmentPersistedData_Create()
{
	NOCONST(ItemAssignmentPersistedData)* pPersistedData = StructCreateNoConst(parse_ItemAssignmentPersistedData);
	
	if (pPersistedData)
	{
		S32 i, iNumPersonalBuckets = eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings);

		for (i = 0; i < iNumPersonalBuckets; ++i)
		{
			eaPush(&pPersistedData->eaPersistedPersonalAssignmentBuckets, StructCreateNoConst(parse_ItemAssignmentPersonalPersistedBucket));
		}
	}

	return pPersistedData;
}

// This fixes up the eaPersonalAssignmentBuckets on our persisted data, and just makes sure the list sizes are the same
// between the g_ItemAssignmentSettings.eaPersonalAssignmentSettings and the persisted bucket list
static void ItemAssignmentPersistedData_FixupPersonalAssignmentBuckets(NOCONST(ItemAssignmentPersistedData) *pPersistedData)
{
	S32 iCurListSize = eaSize(&pPersistedData->eaPersistedPersonalAssignmentBuckets);
	S32 iSettingsSize = eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings);
	S32 i;

	if (iCurListSize == iSettingsSize)
		return;

	if (iSettingsSize > iCurListSize)
	{
		// we need more in our persisted list 
		for (i = eaSize(&pPersistedData->eaPersistedPersonalAssignmentBuckets); i < iSettingsSize; ++i)
		{
			eaPush(&pPersistedData->eaPersistedPersonalAssignmentBuckets, StructCreateNoConst(parse_ItemAssignmentPersonalPersistedBucket));
		}

	}
	else
	{
		// remove ones from the end of the list to match the size
		for (i = iSettingsSize; i < iCurListSize; ++i)
		{
			NOCONST(ItemAssignmentPersonalPersistedBucket)* pData = eaPop(&pPersistedData->eaPersistedPersonalAssignmentBuckets);
			if (pData)
				StructDestroyNoConst(parse_ItemAssignmentPersonalPersistedBucket, pData);
		}
	}
}

// gets the given bucket, fist fixup the number of g_ItemAssignmentSettings.eaPersonalAssignmentSettings
// and then return a valid pointer if possible.
static NOCONST(ItemAssignmentPersonalPersistedBucket)* ItemAssignmentPersistedData_GetPersonalBucket(NOCONST(ItemAssignmentPersistedData) *pPersistedData, S32 idx)
{
	if (idx >= eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings))
		return NULL; // this is outside of anything we'd expect

	ItemAssignmentPersistedData_FixupPersonalAssignmentBuckets(pPersistedData);

	return eaGet(&pPersistedData->eaPersistedPersonalAssignmentBuckets, idx);
}


// Creates the ItemAssignmentPlayerData struct and initializes the necessary default fields
static ItemAssignmentPlayerData* ItemAssignmentPlayerData_Create()
{
	ItemAssignmentPlayerData *pData = StructCreate(parse_ItemAssignmentPlayerData);
	if (pData)
	{
		S32 i, iNumPersonalBuckets = eaSize(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings);
				
		for (i = 0; i < iNumPersonalBuckets; ++i)
		{
			eaPush(&pData->eaPersonalAssignmentBuckets, StructCreate(parse_ItemAssignmentPersonalAssignmentBucket));
		}
	}

	return pData;
}

// adds the given eaRefs to peaAvailableOut, but only if they are valid 
static void gslItemAssignments_UpdateAvailableList(Entity* pEnt,
												   ItemAssignmentLocationData* pLocationData,
												   ItemAssignmentDefRef** eaRefs,
												   ItemAssignmentDefRef*** peaAvailableOut)
{
	S32 i;
	for (i = eaSize(&eaRefs)-1; i >= 0; i--)
	{
		ItemAssignmentDef* pDef = GET_REF(eaRefs[i]->hDef);

		if (!pDef)
		{
			continue;
		}

		if (gslItemAssignments_IsValidAvailableAssignment(pEnt, pDef, pLocationData) || 
			eaFind(&s_ppchForceAssignmentAdds, pDef->pchName) >= 0)
		{
			ItemAssignmentDefRef* pRef = StructCreate(parse_ItemAssignmentDefRef);
			COPY_HANDLE(pRef->hDef, eaRefs[i]->hDef);
			pRef->bFeatured = gslItemAssignment_IsFeatured(pDef);
			eaIndexedEnable(peaAvailableOut, parse_ItemAssignmentDefRef);
			if (!eaPush(peaAvailableOut, pRef))
			{
				StructDestroy(parse_ItemAssignmentDefRef, pRef);
			}
		}
	}
}

// helper to  gslItemAssignments_UpdateAvailableAssignments
// takes the list of new refs and old refs and updates the peaOldRefsInOut to the peaNewRefsInOut if they are different
// peaOldRefsInOut will be either used or destroyed
static bool _SetNewRefsListIfDifferent(Entity* pEnt, 
										ItemAssignmentDefRef*** peaNewRefsInOut, 
										ItemAssignmentDefRef*** peaOldRefsInOut)
{
	S32 iCurrentListSize = 0;
	S32 iNewListList = 0;
	bool bSame = true;
	
	iCurrentListSize = eaSize(peaOldRefsInOut);
	iNewListList = eaSize(peaNewRefsInOut);
	
	if (iNewListList != iCurrentListSize)
	{
		bSame = false;
	}
	else
	{
		S32 i;
		for (i = 0; i < iCurrentListSize; i++)
		{
			ItemAssignmentDefRef* pRef = (*peaOldRefsInOut)[i];
			if (GET_REF(pRef->hDef) != GET_REF((*peaNewRefsInOut)[i]->hDef))
			{
				bSame = false;
				break;
			}
		}
	}

	if (!bSame)
	{
		// destroy the old list.
		eaDestroyStruct(peaOldRefsInOut, parse_ItemAssignmentDefRef);
		// set the new list to point at the
		(*peaOldRefsInOut) = (*peaNewRefsInOut);
		
		// Set dirty bit
		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
	else
	{
		eaDestroyStruct(peaNewRefsInOut, parse_ItemAssignmentDefRef);
	}

	*peaNewRefsInOut = NULL;
	return !bSame;
}

// updates the non-persisted ItemAssignmentData with the assignments from the persisted ItemAssignmentPersistedData
// if a the non-persisted list is the same as the persisted one, nothing will change
static void gslItemAssignments_UpdateAvailableAssignments(Entity* pEnt, 
														  ItemAssignmentVolumeData* pVolumeData,
														  ItemAssignmentLocationData* pLocationData,
														  bool bPersonalList)
{
	if (!pEnt || !pEnt->pPlayer)
	{
		return;
	}
		
	// if we don't have an ItemAssignmentData yet create one 
	if (!pEnt->pPlayer->pItemAssignmentData)
	{
		pEnt->pPlayer->pItemAssignmentData = ItemAssignmentPlayerData_Create();
		if (!pEnt->pPlayer->pItemAssignmentData)
			return;
	}
	
	{
		ItemAssignmentPlayerData* pItemAssignmentData = pEnt->pPlayer->pItemAssignmentData;
		ItemAssignmentPersistedData* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		ItemAssignmentDefRef** eaRefs = NULL;
		
		if (bPersonalList)
		{	
			// go through each of the persisted personal buckets and try to update each list 
			if (pPersistedData)
			{
				FOR_EACH_IN_EARRAY(pPersistedData->eaPersistedPersonalAssignmentBuckets, 
									ItemAssignmentPersonalPersistedBucket, pData)
				{
					ItemAssignmentPersonalAssignmentBucket *pPersonalAssignmentBucket = NULL;
					pPersonalAssignmentBucket = eaGet(&pItemAssignmentData->eaPersonalAssignmentBuckets, FOR_EACH_IDX(-, pData));

					if (pPersonalAssignmentBucket)
					{
						gslItemAssignments_UpdateAvailableList(pEnt, pLocationData, 
																(ItemAssignmentDefRef**)pData->eaAvailableAssignments, &eaRefs);
						
						if (_SetNewRefsListIfDifferent(pEnt, &eaRefs, &pPersonalAssignmentBucket->eaAvailableAssignments))
							pPersonalAssignmentBucket->bUpdatedList = true;
					}
				}
				FOR_EACH_END
			}
		}
		else if (pVolumeData)
		{
			gslItemAssignments_UpdateAvailableList(pEnt, NULL, pVolumeData->eaRefs, &eaRefs);
			_SetNewRefsListIfDifferent(pEnt, &eaRefs, &pItemAssignmentData->eaVolumeAvailableAssignments);
		}
	}
}

// Checks to see if the player request to generate a new list of personal assignments for the given personal bucket index
bool gslItemAssignments_ShouldRequestPersonalAssignments(Entity* pEnt, U32* puRequestTime, S32 iBucketIdx)
{
	ItemAssignmentPersonalAssignmentBucket *pPersonalAssignmentBucket = NULL;
	ItemAssignmentPersonalAssignmentSettings *pPersonalAssignmentSettings = NULL;
	U32 uCurrentTime = timeSecondsSince2000();
	
	pPersonalAssignmentSettings = eaGet(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings, iBucketIdx);
	if (!pEnt || !pEnt->pPlayer || !pPersonalAssignmentSettings || !eaiSize(&pPersonalAssignmentSettings->peRarityCounts))
	{
		return false;
	}

	if (puRequestTime)
	{
		(*puRequestTime) = uCurrentTime;
	}

	// see if our non-persisted ItemAssignmentData wants us to update this personal assignment bucket
	if (pEnt->pPlayer->pItemAssignmentData)
	{
		ItemAssignmentPlayerData *pItemAssignmentData = pEnt->pPlayer->pItemAssignmentData;

		if (pItemAssignmentData->bDebugForceRequestPersonalAssignments)
		{
			return true;
		}

		pPersonalAssignmentBucket = eaGet(&pItemAssignmentData->eaPersonalAssignmentBuckets, iBucketIdx);

		if (pPersonalAssignmentBucket && uCurrentTime < pPersonalAssignmentBucket->uNextPersonalRequestTime)
		{
			return false;
		}
	}
	
	// check if the persisted assignment data wants us to update 
	if (pEnt->pPlayer->pItemAssignmentPersistedData)
	{
		ItemAssignmentPersistedData *pItemAssignmentPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
		ItemAssignmentPersonalPersistedBucket *pPeristedPersonalAssignmentBucket = NULL;
			
		pPeristedPersonalAssignmentBucket = eaGet(&pItemAssignmentPersistedData->eaPersistedPersonalAssignmentBuckets, iBucketIdx);
	
		if (pPeristedPersonalAssignmentBucket)
		{
			U32 uLastRequestTime = pPeristedPersonalAssignmentBucket->uLastPersonalUpdateTime;
			U32 uNextRequestTime = uLastRequestTime + pPersonalAssignmentSettings->uAssignmentRefreshTime;

			if (uCurrentTime < uNextRequestTime)
			{
				if (pPersonalAssignmentBucket && !pPersonalAssignmentBucket->bUpdatedList)
				{
					if (puRequestTime)
					{
						(*puRequestTime) = uLastRequestTime;
					}
				}
				else
				{
					return false;
				}
			}
		}
	}

	return true;
}

// Retrieve the map settings for the current map
static ItemAssignmentRarityCountType* gslItemAssignments_GetCurrentMapRarityCounts(const char* pchMapNamePooled)
{
	if (pchMapNamePooled && pchMapNamePooled[0])
	{
		S32 i;
		for (i = eaSize(&g_ItemAssignmentSettings.eaMapSettings)-1; i >= 0; i--)
		{
			ItemAssignmentMapSettings* pMapSettings = g_ItemAssignmentSettings.eaMapSettings[i];
			
			if (pchMapNamePooled == pMapSettings->pchMapName)
			{
				if (eaiSize(&pMapSettings->peRarityCounts))
				{
					return pMapSettings->peRarityCounts;
				}
				else
				{
					return g_ItemAssignmentSettings.peGlobalMapRarityCounts;
				}
			}
		}
	}
	return NULL;
}

// big hammer for when player starts, cancels, completes or collects assignments
static void gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(int iPartitionIdx, Entity* pEnt, 
																	ItemAssignmentDef *pDef, bool bRemoveGrantedAssignments)
{
	ItemAssignmentVolumeData* pVolumeData = NULL;
	ItemAssignmentLocationData LocationData = {0};

	// Get the player's location data
	gslItemAssignments_GetPlayerLocationDataEx(pEnt, true, &LocationData);

	// Update map-specific assignments
	if (pVolumeData = gslItemAssignments_GetVolumeData(iPartitionIdx, LocationData.pchVolumeName))
	{
		gslItemAssignments_UpdateAvailableAssignments(pEnt, pVolumeData, NULL, false);
	}

	// Remove granted assignments when they are completed
	if (!bRemoveGrantedAssignments || !pDef || 
		!gslItemAssignments_UpdateGrantedAssignments(pEnt, pDef, kItemAssignmentOperation_Remove))
	{
		// If this wasn't a granted assignment, update personal assignments
		gslItemAssignments_UpdateAvailableAssignments(pEnt, NULL, &LocationData, true);
	}

	gslItemAssignments_UpdateAutograntedAssignments(pEnt, true);

	// Update the next process time
	gslItemAssignments_PlayerUpdateNextProcessTime(pEnt);
}


///////////////////////////////////////////////////////////////////////////////////////////
// Transactions
///////////////////////////////////////////////////////////////////////////////////////////



static void gslItemAssignment_CancelActive_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);
	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(entGetPartitionIdx(pEnt), pEnt, NULL, false);

		// Send notifications
		gslItemAssignments_NotifySend(pEnt, pDef, "ItemAssignments_CancelActiveSucceeded");
	}
	else if (pEnt)
	{
		gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_CancelActiveFailed");
	}
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pitemassignmentpersisteddata");
enumTransactionOutcome ItemAssignmentsUpdateUnlockedSlots(ATR_ARGS, NOCONST(Entity)* pEnt, ItemAssignmentUnlockedSlots *pSlots)
{
	NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;

	ea32Clear(&pAssignmentData->eaItemAssignmentSlotsUnlocked);
	ea32Copy(&pAssignmentData->eaItemAssignmentSlotsUnlocked,&pSlots->eaSlots);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pplayer.Pitemassignmentpersisteddata, .pInventoryV2.Ppinventorybags, .Pplayer.Pugckillcreditlimit, .Psaved.Ppallowedcritterpets, .Psaved.Uiindexbuild, .Hallegiance, .Hsuballegiance, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Pplitebags, .Pplayer.Playertype, .Psaved.Ppbuilds, .Psaved.Ppownedcontainers, .pInventoryV2.Peaowneduniqueitems, .Itemidmax, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid")
	ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome gslItemAssignment_tr_CancelActive(ATR_ARGS, 
														 NOCONST(Entity)* pEnt,
														 CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														 U32 uAssignmentID, 
														 const ItemChangeReason *pReason,
														 GameAccountDataExtract* pExtract)
{
	NOCONST(ItemAssignment)* pAssignment = ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (NONNULL(pAssignment))
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;
		ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uCompleteTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);
		
		if (pDef && pDef->bIsAbortable && uCurrentTime < uCompleteTime)
		{
			enumTransactionOutcome eResult = TRANSACTION_OUTCOME_SUCCESS;
			S32 i;

			// Unslot all items
			for (i = 0; i < eaSize(&pAssignment->eaSlottedItems); i++)
			{
				NOCONST(InventorySlot)* pSlot;
				pSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_PASS_ARGS, pEnt, pAssignment->eaSlottedItems[i], pExtract);

				if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
				{
					pSlot->pItem->flags &= ~kItemFlag_SlottedOnAssignment;
				}
			}

			// Refund the item costs
			if (pDef->pRequirements && eaSize(&pDef->pRequirements->eaItemCosts))
			{
				for (i = 0; i < eaSize(&pDef->pRequirements->eaItemCosts); i++)
				{
					ItemAssignmentItemCost* pItemCost = pDef->pRequirements->eaItemCosts[i];
					S32 iCount = pItemCost->iCount;
					ItemDef* pItemDef = GET_REF(pItemCost->hItem);

					if (pItemDef)
					{
						if (pItemDef->eType == kItemType_Numeric)
						{
							if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pItemDef->pchName, iCount, pReason))
							{
								eResult = TRANSACTION_OUTCOME_FAILURE;
							}
						}
						else
						{
							Item* pItem = pItemDef ? item_FromDefName(pItemDef->pchName) : NULL;
							InvBagIDs eBagID = inv_trh_GetBestBagForItemDef(pEnt, pItemDef, iCount, true, pExtract);
							CONTAINER_NOCONST(Item, pItem)->count = iCount;
							eResult = inv_AddItem(ATR_PASS_ARGS, pEnt, eaPets, eBagID, -1, pItem, pItemDef->pchName, ItemAdd_UseOverflow, pReason, pExtract);
							StructDestroy(parse_Item, pItem);
						}
					}
					else
					{
						eResult = TRANSACTION_OUTCOME_FAILURE;
					}
					if (eResult == TRANSACTION_OUTCOME_FAILURE)
					{
						break;
					}
				}
			}

			// Remove and destroy the assignment
			eaFindAndRemove(&pAssignmentData->eaActiveAssignments, pAssignment);
			StructDestroyNoConst(parse_ItemAssignment, pAssignment);

			if (gbEnableItemAssignmentLogging) {
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEMASSIGNMENT, "ItemAssignment:CancelActive", 
																		"AssignmentName %s", pDef->pchName);
			}

			return eResult;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// checks for any unlocks that checks the expression GetCategoriesAboveRank
static void gslItemAssignments_RankGainedCB(TransactionReturnVal* returnVal, ItemAssignmentCBData *pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	
	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eID,pEnt);

	if (pEnt)
		gslItemAssignments_CheckExpressionSlots(pEnt, NULL);

	// Clean up
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

// checks the ranking schedule and will adjust numerics as necessary
static void gslItemAssignments_CheckRankSchedule(	Entity* pEnt,
													ItemAssignmentRankingSchedule *pSchedule, 
													const char *pchXPNumeric,
													const char *pchRankNumeric,
													const char *pchNotifyMessage, 
													DisplayMessage *pMsg)
{
	S32 iCurrentXP, iCurrentRank, iNewRank; 
	S32 iNumRanks;

	if (!pchXPNumeric || !pchRankNumeric)
		return;

	iCurrentXP = inv_GetNumericItemValue(pEnt, pchXPNumeric);
	iCurrentRank = inv_GetNumericItemValue(pEnt, pchRankNumeric);
	

	iNumRanks = eaiSize(&pSchedule->eaiExperience);
	if (iNumRanks == 0)
		return;

	iNewRank = iNumRanks - 1;

	// see what our current rank should be
	{
		S32 i;
		for (i = 1; i < iNumRanks; ++i)
		{
			if (iCurrentXP < pSchedule->eaiExperience[i])
			{
				iNewRank = i - 1;
				break;
			}
		}
	}

	if (iNewRank <= iCurrentRank)
		return; // same rank (and ignoring losing ranks), nothing to do
	
	{	// we've gotten a rank!
		// increase the Rank numeric
		// send a notification to the entity
		ItemChangeReason reason = {0};
		ItemAssignmentCBData* pCBData = gslItemAssignments_CreateCBData(pEnt, NULL, NULL, NULL);

		inv_FillItemChangeReason(&reason, pEnt, "itemAssignment:RankLevel", NULL);

		itemtransaction_SetNumeric(pEnt, pchRankNumeric, (F32)iNewRank, &reason, gslItemAssignments_RankGainedCB, pCBData);
	
		// send notification
		{
			char* estrMessage = NULL;
			const char* pchDisplayName = "<NoCategory>";
			estrStackCreate(&estrMessage);

			if (pMsg)
			{
				pchDisplayName = entTranslateDisplayMessage(pEnt, *pMsg);
			}

			entFormatGameMessageKey(pEnt, &estrMessage, pchNotifyMessage, 
									STRFMT_INT("Rank", iNewRank), 
									STRFMT_STRING("Category", pchDisplayName), 
									STRFMT_END);
			ClientCmd_NotifySend(pEnt, kNotifyType_ItemAssignmentFeedback, estrMessage, pchNotifyMessage, NULL);

			estrDestroy(&estrMessage);
		}
	}
}

static void gslItemAssignment_GiveExperience_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);

	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eID,pEnt);

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		// the player has collected the rewards. check 
		if (pEnt && g_ItemAssignmentSettings.pCategoryRankingSchedule && pDef)
		{
			ItemAssignmentCategorySettings* pSettings = ItemAssignmentCategory_GetSettings(pDef->eCategory);

			if(pSettings)
			{
				gslItemAssignments_CheckRankSchedule(	pEnt, g_ItemAssignmentSettings.pCategoryRankingSchedule, 
														pSettings->pchNumericXP1, pSettings->pchNumericRank1,
														"ItemAssignments_CategoryRankIncreased",
														&pSettings->msgDisplayName);
			}
		}
	}
	
	// do this regardless of the outcome 
	if(pEnt)
		gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(entGetPartitionIdx(pEnt), pEnt, NULL, false);
	
	
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

// award the completion experience. 
static void gslItemAssignment_AwardCompletionExperience(Entity* pEnt, ItemAssignmentDef* pDef, U32 uItemAssignmentID)
{
	if (pDef && pDef->iCompletionExperience > 0)
	{
		ItemAssignmentCategorySettings* pSettings = ItemAssignmentCategory_GetSettings(pDef->eCategory);

		if (pSettings && pSettings->pchNumericXP1)
		{
			ItemChangeReason reason = {0};
			ItemAssignmentCBData* pCBData = gslItemAssignments_CreateCBData(pEnt, NULL, pDef, NULL);
			pCBData->uAssignmentID = uItemAssignmentID;
			
			inv_FillItemChangeReason(&reason, pEnt, "itemAssignment:Experience", pDef->pchName);

			itemtransaction_AddNumeric(pEnt, pSettings->pchNumericXP1, (F32)pDef->iCompletionExperience, 
										&reason, gslItemAssignment_GiveExperience_CB, pCBData);
		}

	}
}

// callback for when rewards are collected from the item assignment
static void gslItemAssignment_CollectRewards_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);

	if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		pEnt = entForClientCmd(pData->eID,pEnt);
	}

	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		gslItemAssignments_NotifySend(pEnt, pDef, "ItemAssignments_CollectRewardsSucceeded");

		if (g_ItemAssignmentSettings.pMetaRankingSchedule)
		{
			gslItemAssignments_CheckRankSchedule(	pEnt, g_ItemAssignmentSettings.pMetaRankingSchedule, 
													g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericXP, 
													g_ItemAssignmentSettings.pMetaRankingSchedule->pchNumericRank,
													"ItemAssignments_MetaRankIncreased", NULL);
		}

		// do PlayerUpdateAssignmentsOnStateChange only if we don't have completion XP.
		// otherwise, we're going to do this in gslItemAssignment_GiveExperience_CB
		if (pDef && pDef->iCompletionExperience > 0)
		{
			gslItemAssignment_AwardCompletionExperience(pEnt, pDef, pData->uAssignmentID);
		}
		else
		{
			gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(entGetPartitionIdx(pEnt), pEnt, NULL, false);
		}
	}
	else
	{
		gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_CollectRewardsFailed");
	}
	
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Skilltype, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .pInventoryV2.Pplitebags, .pInventoryV2.Peaowneduniqueitems, .Pplayer.Pitemassignmentpersisteddata, .Psaved.Ppallowedcritterpets, .pInventoryV2.Ppinventorybags, .Hallegiance, .Hsuballegiance, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, .Itemidmax, .Pplayer.Pugckillcreditlimit, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pchar.Ilevelexp, .Psaved.Costumedata.Eaunlockedcostumerefs, .Pplayer.Pplayeraccountdata.Eapendingkeys, .Psaved.Ppownedcontainers, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .Pchar.Uipoweridmax, .Psaved.Ipetid, .Psaved.Ppuppetmaster.Curid, .Psaved.Ppuppetmaster.Curtype, .Psaved.Ppuppetmaster.Curtempid, .Pplayer.Playertype")
ATR_LOCKS(eaPets, ".Pcritter.Petdef, .pInventoryV2.Peaowneduniqueitems");
enumTransactionOutcome gslItemAssignment_tr_CollectRewards(ATR_ARGS, 
														   NOCONST(Entity)* pEnt, 
														   CONST_EARRAY_OF(NOCONST(Entity)) eaPets,
														   U32 uAssignmentID, 
														   GiveRewardBagsData* pRewards,
														   const ItemChangeReason *pReason,
														   GameAccountDataExtract* pExtract)
{
	NOCONST(ItemAssignment)* pAssignment = ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (NONNULL(pAssignment) && pRewards)
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;

		// Attempt to give the player the rewards
		if (inv_trh_GiveRewardBags(ATR_PASS_ARGS, pEnt, eaPets, pRewards, kRewardOverflow_DisallowOverflowBag, NULL, pReason, pExtract, NULL))
		{
			// Remove and destroy the assignment
			eaFindAndRemove(&pAssignmentData->eaActiveAssignments, pAssignment);
			StructDestroyNoConst(parse_ItemAssignment, pAssignment);
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pitemassignmentpersisteddata");
enumTransactionOutcome gslItemAssignment_tr_RemoveAndDestroyActiveAssignment(ATR_ARGS, 
																			   NOCONST(Entity)* pEnt, 
																			   U32 uAssignmentID)
{
	NOCONST(ItemAssignment)* pAssignment = ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (NONNULL(pAssignment))
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;

		if (pAssignmentData)
		{
			// Remove and destroy the assignment
		eaFindAndRemove(&pAssignmentData->eaActiveAssignments, pAssignment);
		StructDestroyNoConst(parse_ItemAssignment, pAssignment);
		return TRANSACTION_OUTCOME_SUCCESS;
		}
		
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

static void gslItemAssignment_StartNew_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);

	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eID,pEnt);

	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		
		gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(iPartitionIdx, pEnt, NULL, false);

		// Send notifications and game events
		gslItemAssignments_NotifySend(pEnt, pDef, "ItemAssignments_StartNewSucceeded");
		if (pDef)
		{
			eventsend_RecordItemAssignmentStarted(iPartitionIdx, pEnt, pDef->pchName);
		}
	}
	else if (pEnt)
	{
		gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_StartNewFailed");
	}
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}


AUTO_TRANS_HELPER;
U32 gslItemAssignments_trh_GetNewAssignmentID(ATH_ARG NOCONST(ItemAssignmentPersistedData)* pAssignmentData)
{
	U32 uNewID = ++pAssignmentData->uMaxAssignmentID;
	if (!uNewID)
	{
		uNewID++;
	}
	return uNewID;
}

AUTO_TRANS_HELPER;
S32 gslItemAssignments_trh_RemoveItemCosts(ATR_ARGS, 
										   ATH_ARG NOCONST(Entity)* pEnt,
										   ItemAssignmentDef* pDef,
										   const ItemChangeReason *pReason,
										   GameAccountDataExtract* pExtract)
{
	if (pDef->pRequirements && eaSize(&pDef->pRequirements->eaItemCosts))
	{
		S32 i;
		InvBagFlag eSearchBags = ItemAssignments_GetSearchInvBagFlags();

		for (i = 0; i < eaSize(&pDef->pRequirements->eaItemCosts); i++)
		{
			ItemAssignmentItemCost* pItemCost = pDef->pRequirements->eaItemCosts[i];
			ItemDef* pItemDef = GET_REF(pItemCost->hItem);
			S32 iCount = pItemCost->iCount;

			if (!pItemDef || !iCount)
				continue;

			if (pItemDef->eType == kItemType_Numeric)
			{
				S32 iValue = inv_trh_GetNumericValue(ATR_PASS_ARGS, pEnt, pItemDef->pchName);
				if (!inv_ent_trh_SetNumeric(ATR_PASS_ARGS, pEnt, false, pItemDef->pchName, iValue - iCount, pReason))
				{
					return false;
				}
			}
			else
			{
				if (!inv_trh_FindItemCountByDefNameEx(ATR_PASS_ARGS, pEnt, eSearchBags, pItemDef->pchName, 
														iCount, true, pReason, pExtract))
				{
					return false;
				}
			}
		}
	}
	return true;
}

AUTO_TRANS_HELPER;
NOCONST(ItemAssignment)* gslItemAssignments_trh_StartNew(ATR_ARGS, 
														 ATH_ARG NOCONST(Entity)* pEnt, 
														 const char* pchAssignmentDef, 
														 const char* pchMapMsgKey,
														 S32 iItemAssignmentSlot,
														 ItemAssignmentSlots* pSlots,
														 U32 uStartTime,
														 U32 uDuration, 
														 const ItemChangeReason *pReason,
														 GameAccountDataExtract* pExtract)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	
	if (pDef && pSlots && NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && 
		ItemAssignments_trh_CheckRequirements(ATR_PASS_ARGS, pEnt, pDef, pSlots, pExtract) &&
		gslItemAssignments_trh_RemoveItemCosts(ATR_PASS_ARGS, pEnt, pDef, pReason, pExtract))
	{
		S32 i;
		NOCONST(ItemAssignment)* pNewAssignment;
		if (ISNULL(pEnt->pPlayer->pItemAssignmentPersistedData))
		{
			pEnt->pPlayer->pItemAssignmentPersistedData = ItemAssignmentPersistedData_Create();
		}

		iItemAssignmentSlot = CLAMP(iItemAssignmentSlot, 0, 255);

		// if we care about iAssignmentSlot, check to make sure there are no conflicts.
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			if (!ItemAssignments_trh_IsValidNewItemAssignmentSlot(ATR_PASS_ARGS, pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, iItemAssignmentSlot))
			{
				return NULL;
			}
		}

		// Create the new assignment
		pNewAssignment = StructCreateNoConst(parse_ItemAssignment);
		pNewAssignment->uTimeStarted = uStartTime;
		pNewAssignment->uDuration = uDuration;
		pNewAssignment->uAssignmentID = gslItemAssignments_trh_GetNewAssignmentID(pEnt->pPlayer->pItemAssignmentPersistedData);
		pNewAssignment->pchMapMsgKey = allocAddString(pchMapMsgKey);
		
		pNewAssignment->uItemAssignmentSlot = (U8)iItemAssignmentSlot;

		SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pNewAssignment->hDef);

		// Create item slots
		for (i = 0; i < eaSize(&pSlots->eaSlots); i++)
		{
			NOCONST(ItemAssignmentSlottedItem)* pSlottedItem = CONTAINER_NOCONST(ItemAssignmentSlottedItem, pSlots->eaSlots[i]);
			NOCONST(InventorySlot)* pSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_PASS_ARGS, pEnt, pSlottedItem, pExtract);

			if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
			{
				NOCONST(ItemAssignmentSlottedItem)* pNewSlottedItem = StructCreateNoConst(parse_ItemAssignmentSlottedItem);
				pNewSlottedItem->uItemID = pSlottedItem->uItemID;
				pNewSlottedItem->iAssignmentSlot = pSlottedItem->iAssignmentSlot;
				eaPush(&pNewAssignment->eaSlottedItems, pNewSlottedItem);

				// Mark the item as being slotted on an item assignment
				pSlot->pItem->flags |= kItemFlag_SlottedOnAssignment;
			}
			else
			{
				StructDestroyNoConstSafe(parse_ItemAssignment, &pNewAssignment);
				return NULL;
			}
		}
		eaPush(&pEnt->pPlayer->pItemAssignmentPersistedData->eaActiveAssignments, pNewAssignment);

		if (gbEnableItemAssignmentLogging) {
			TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEMASSIGNMENT, "ItemAssignment:StartNew", 
																"AssignmentName %s", pDef->pchName);
		}
		
		return pNewAssignment;
	}
	return NULL;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pplayer.Pitemassignmentpersisteddata, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .pInventoryV2.Ppinventorybags, .Pplayer.Skilltype, .Hallegiance, .Hsuballegiance, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags");
enumTransactionOutcome gslItemAssignments_tr_StartNew(ATR_ARGS, 
													  NOCONST(Entity)* pEnt, 
													  const char* pchAssignmentDef, 
													  const char* pchMapMsgKey,
													  S32 iItemAssignmentSlot,
													  ItemAssignmentSlots* pSlots,
													  U32 uStartTime,
													  U32 uDuration,
													  const ItemChangeReason *pReason,
													  GameAccountDataExtract* pExtract)
{
	NOCONST(ItemAssignment)* pNewAssignment;
	pNewAssignment = gslItemAssignments_trh_StartNew(ATR_PASS_ARGS, 
													 pEnt, 
													 pchAssignmentDef, 
													 pchMapMsgKey, 
													 iItemAssignmentSlot,
													 pSlots, 
													 uStartTime, 
													 uDuration,
													 pReason, 
													 pExtract);
	if (NONNULL(pNewAssignment))
	{
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

static void gslItemAssignment_Complete_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER,pData->eID);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, pData->uAssignmentID);

	if(!pEnt && GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		pEnt = entForClientCmd(pData->eID,pEnt);


	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		ItemAssignmentCompletedDetails* pCompletedDetails = ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, pData->uAssignmentID);
				
		gslItemAssignments_PlayerUpdateAssignmentsOnStateChange(iPartitionIdx, pEnt, pDef, true);
		
		// Clear the read flag
		if (pCompletedDetails)
		{
			pCompletedDetails->bMarkedAsRead = false;

			// Set dirty bit
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
		
		// Send notifications and game events
		gslItemAssignments_NotifySend(pEnt, pDef, "ItemAssignments_CompleteSucceeded");
		if (pDef)
		{
			eventsend_RecordItemAssignmentCompleted(iPartitionIdx, pEnt, pDef->pchName, pData->pchOutcome, pData->fAssignmentSpeedBonus);
		}
	}
	if (pAssignment)
	{
		pAssignment->bCompletionPending = false;
	}
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

// Add the assignment to the list of completed assignments if:
// 1.) The assignment is not repeatable
// 2.) The assignment has a cooldown period after completion
// 3.) This assignment has any dependencies.
// 4.) This assignment requires another assignment. Do this to anticipate growth in dependency chains.
static bool gslItemAssignments_IsValidCompletedAssignment(SA_PARAM_OP_VALID ItemAssignmentDef* pDef, bool bIgnoreCooldown)
{
	if (pDef && 
		(!pDef->bRepeatable ||
		 (!bIgnoreCooldown && pDef->uCooldownAfterCompletion) ||
		 eaSize(&pDef->eaDependencies) || 
		 (pDef->pRequirements && GET_REF(pDef->pRequirements->hRequiredAssignment))))
	{
		return true;
	}
	return false;
}

AUTO_TRANS_HELPER;
void gslItemAssignments_trh_CleanupCompletedAssignments(ATR_ARGS, ATH_ARG NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt->pPlayer))
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;

		if (NONNULL(pAssignmentData))
		{
			U32 uCurrentTime = timeSecondsSince2000();
			S32 i;
			for (i = eaSize(&pAssignmentData->eaCompletedAssignments)-1; i >= 0; i--)
			{
				NOCONST(ItemAssignmentCompleted)* pRef = pAssignmentData->eaCompletedAssignments[i];
				ItemAssignmentDef* pDef = GET_REF(pRef->hDef);

				if (!gslItemAssignments_IsValidCompletedAssignment(pDef, true))
				{
					if (pDef && pDef->uCooldownAfterCompletion)
					{
						U32 uFinishCooldown = pRef->uCompleteTime + pDef->uCooldownAfterCompletion;
						if (uCurrentTime < uFinishCooldown)
						{
							// Don't remove this completed assignment if it hasn't finished cooldown
							continue;
						}
					}
					eaRemove(&pAssignmentData->eaCompletedAssignments, i);
					StructDestroyNoConst(parse_ItemAssignmentCompleted, pRef);
				}
			}
		}
	}
}

static bool gslItemAssignments_ShouldDestroyItem(ItemDef* pItemDef, ItemAssignmentOutcome* pOutcome)
{
	bool bHasItemCategories = false;

	if (!ItemAssignments_CheckDestroyRequirements(pItemDef, pOutcome))
		return false;

	return randomPositiveF32() <= pOutcome->pResults->fDestroyChance;
}

AUTO_TRANSACTION
	ATR_LOCKS(pEnt, ".Psaved.pSCPdata.Isummonedscp, .Psaved.pSCPdata.Fcachedpetbonusxppct, .Psaved.pSCPdata.erSCP, .Pchar.Ppsecondarypaths, .Pchar.Hpath, .Pplayer.Pitemassignmentpersisteddata, .pInventoryV2.Ppinventorybags, .Pchar.Ilevelexp, .Pplayer.Pplayeraccountdata.Eagamepermissionmaxvaluenumerics, .Pplayer.Skilltype, .Hallegiance, .Hsuballegiance, .Pplayer.Eskillspecialization, .Pchar.Hclass, .Pplayer.Pugckillcreditlimit, .Psaved.Conowner.Containerid, .Psaved.Conowner.Containertype, .pInventoryV2.Peaowneduniqueitems, .Psaved.Uiindexbuild, .Psaved.Ppbuilds, pInventoryV2.ppLiteBags")
	ATR_LOCKS(pData, ".Iversion, .Eakeys");
enumTransactionOutcome gslItemAssignments_tr_CompleteAssignment(ATR_ARGS, 
																NOCONST(Entity)* pEnt, 
																NOCONST(GameAccountData)* pData,
																U32 uAssignmentID, 
																const char* pchOutcomeName,
																U32 bHasItemRewards,
																U32 bForceComplete,
																S32 iForceCompleteCost,
																U32 bUseToken,
																const ItemChangeReason *pReason,
																GameAccountDataExtract* pExtract)
{
	NOCONST(ItemAssignment)* pAssignment = ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (NONNULL(pAssignment) && !EMPTY_TO_NULL(pAssignment->pchRewardOutcome))
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;
		ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uCompleteTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);
		ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pchOutcomeName);
		bool bCanComplete = bForceComplete ? true : uCurrentTime >= uCompleteTime;
		
		if (bForceComplete)
		{
			if (bUseToken)
			{
				const char* pchKey = MicroTrans_GetItemAssignmentCompleteNowGADKey();
				if (!slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pData, pchKey, -1, 0, 100000))
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
				pData->iVersion++;
			}
			else if (g_ItemAssignmentSettings.pExprForceCompleteNumericCost)
			{
				ItemDef* pNumericDef = GET_REF(g_ItemAssignmentSettings.hForceCompleteNumeric);
				assert(pNumericDef);
				if (!inv_ent_trh_AddNumeric(ATR_PASS_ARGS, pEnt, false, pNumericDef->pchName, -iForceCompleteCost, pReason))
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
			}
			
		}
		if (pDef && pOutcome && bCanComplete)
		{
			ItemAssignmentDef* pNewAssignmentDef = NULL;
			NOCONST(ItemAssignmentSlottedItem)** eaNewAssignmentSlottedItems = NULL;
			NOCONST(ItemAssignmentCompletedDetails)* pRecentlyCompletedData;
			const char* pchMapMsgKey = pAssignment->pchMapMsgKey;
			S32 i;
			U32 uOldestInactiveCompleted = 0;
			S32 iLastInactiveCompletedIndex = -1;
			S32 iRecentlyCompletedSize = 0;

			if (NONNULL(pOutcome->pResults))
			{
				pNewAssignmentDef = GET_REF(pOutcome->pResults->hNewAssignment);
			}

			// Remove old completed assignments that no longer need to be persisted
			gslItemAssignments_trh_CleanupCompletedAssignments(ATR_PASS_ARGS, pEnt);

			if (gslItemAssignments_IsValidCompletedAssignment(pDef, false))
			{
				NOCONST(ItemAssignmentCompleted)* pCompleted;
				pCompleted = ItemAssignments_trh_PlayerGetCompletedAssignment(pEnt, pDef);
				if (ISNULL(pCompleted))
				{
					pCompleted = StructCreateNoConst(parse_ItemAssignmentCompleted);
					SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pCompleted->hDef);
					eaIndexedEnableNoConst(&pAssignmentData->eaCompletedAssignments, parse_ItemAssignmentCompleted);
					eaPush(&pAssignmentData->eaCompletedAssignments, pCompleted);
				}
				if (pDef->uCooldownAfterCompletion)
				{
					pCompleted->uCompleteTime = bForceComplete ? uCurrentTime : uCompleteTime;
				}
			}

			// This loop is brute-force for now
			for (i = eaSize(&pAssignmentData->eaRecentlyCompletedAssignments)-1; i >= 0; i--)
			{
				NOCONST(ItemAssignmentCompletedDetails)* pDetails = pAssignmentData->eaRecentlyCompletedAssignments[i];
				if (!ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, pDetails->uAssignmentID))
				{
					iRecentlyCompletedSize++;
					if (!uOldestInactiveCompleted || pDetails->uAssignmentID < uOldestInactiveCompleted)
					{
						iLastInactiveCompletedIndex = i;
						uOldestInactiveCompleted = pDetails->uAssignmentID;
					}
				}
			}

			// Create a new RecentlyCompletedAssignment entry
			if (iLastInactiveCompletedIndex < 0 || iRecentlyCompletedSize < g_ItemAssignmentSettings.iMaxAssignmentHistoryCount)
			{
				pRecentlyCompletedData = StructCreateNoConst(parse_ItemAssignmentCompletedDetails);
				eaPush(&pAssignmentData->eaRecentlyCompletedAssignments, pRecentlyCompletedData);
			}
			else
			{
				// Overwriting the oldest entry. Reordering the array here, will not reorder the non-transact
				// information that's on the complete details.
				pRecentlyCompletedData = pAssignmentData->eaRecentlyCompletedAssignments[iLastInactiveCompletedIndex];
				eaClearStructNoConst(&pRecentlyCompletedData->eaSlottedItemRefs, parse_ItemAssignmentSlottedItemResults);
			}
			
			SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pRecentlyCompletedData->hDef);
			pRecentlyCompletedData->uAssignmentID = pAssignment->uAssignmentID;
			pRecentlyCompletedData->uNewAssignmentID = 0;
			pRecentlyCompletedData->pchOutcome = allocAddString(pOutcome->pchName);
			pRecentlyCompletedData->pchMapMsgKey = allocAddString(pchMapMsgKey);
			pRecentlyCompletedData->uTimeStarted = pAssignment->uTimeStarted;
			pRecentlyCompletedData->uDuration = ItemAssignment_GetDuration(pAssignment, pDef);


			for (i = 0; i < eaSize(&pAssignment->eaSlottedItems); i++)
			{
				NOCONST(InventorySlot)* pSlot;
				pSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_PASS_ARGS, pEnt, pAssignment->eaSlottedItems[i], pExtract);

				if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
				{
					ItemDef* pItemDef = GET_REF(pSlot->pItem->hItem);
					NOCONST(ItemAssignmentSlottedItemResults)* pSlottedItemResults = NULL;

					if (pItemDef)
					{
						pSlottedItemResults = StructCreateNoConst(parse_ItemAssignmentSlottedItemResults);
						COPY_HANDLE(pSlottedItemResults->hDef, pSlot->pItem->hItem);
						pSlottedItemResults->uItemID = pSlot->pItem->id;
						eaPush(&pRecentlyCompletedData->eaSlottedItemRefs, pSlottedItemResults);
					}
					// Handle completion actions for this item
					if (NONNULL(pOutcome->pResults))
					{
						bool bShouldDestroyItems = false;

						if (pItemDef && 
							gslItemAssignments_ShouldDestroyItem(pItemDef, pOutcome))
						{
							// Destroy the item and cleanup the slot
							StructDestroyNoConstSafe(parse_Item, &pSlot->pItem);
							
							if (NONNULL(pSlottedItemResults))
							{
								// Set the destroyed flag on the slotted item results
								pSlottedItemResults->bDestroyed = true;
							}
						}
						else if (pNewAssignmentDef && randomPositiveF32() <= pOutcome->pResults->fNewAssignmentChance)
						{
							// Push the slotted item onto the new assignment list
							eaPush(&eaNewAssignmentSlottedItems, eaRemove(&pAssignment->eaSlottedItems, i--));
							
							if (NONNULL(pSlottedItemResults))
							{
								// Set the new assignment flag on the slotted item results
								pSlottedItemResults->bNewAssignment = true;
							}
						}
					}
					if (NONNULL(pSlot->pItem))
					{
						pSlot->pItem->flags &= ~kItemFlag_SlottedOnAssignment;
					}
				}
			}
			eaDestroyStructNoConst(&pAssignment->eaSlottedItems, parse_ItemAssignmentSlottedItem);

			
			if (bHasItemRewards || g_ItemAssignmentSettings.bKeepEmptyRewardOutcomeAssignments)
			{
				pAssignment->pchRewardOutcome = allocAddString(pchOutcomeName);
			}
			else
			{
				// Remove and destroy the assignment
				eaFindAndRemove(&pAssignmentData->eaActiveAssignments, pAssignment);
				StructDestroyNoConst(parse_ItemAssignment, pAssignment);
			}

			if (gbEnableItemAssignmentLogging) {
				TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(LOG_ITEMASSIGNMENT, "ItemAssignment:Complete", 
										"AssignmentName %s Forced %s ForceCost %d", 
												pDef->pchName, (bForceComplete) ? "YES" : "NO", iForceCompleteCost);
			}

			// Start a new assignment, if requested
			if (eaSize(&eaNewAssignmentSlottedItems))
			{
				NOCONST(ItemAssignment)* pNewAssignment;
				ItemAssignmentSlots NewSlots = {0};
				U32 uNewAssignmentStartTime = bForceComplete ? uCurrentTime : uCompleteTime;

				// Redo assignment slots indices for the new assignment
				for (i = 0; i < eaSize(&eaNewAssignmentSlottedItems); i++)
				{
					eaNewAssignmentSlottedItems[i]->iAssignmentSlot = i;
				}
				NewSlots.eaSlots = (ItemAssignmentSlottedItem**)eaNewAssignmentSlottedItems;

				// note: duration scaling not yet supported on follow up assignments

				pNewAssignment = gslItemAssignments_trh_StartNew(ATR_PASS_ARGS, 
																 pEnt, 
																 pNewAssignmentDef->pchName, 
																 pchMapMsgKey, 
																(S32)pAssignment->uItemAssignmentSlot,
																 &NewSlots,
																 uNewAssignmentStartTime,
																 pNewAssignmentDef->uDuration, 
																 pReason,
																 pExtract);
				StructDeInit(parse_ItemAssignmentSlots, &NewSlots);
				if (ISNULL(pNewAssignment))
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
				else
				{
					pRecentlyCompletedData->uNewAssignmentID = pNewAssignment->uAssignmentID;
				}
			}
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

static void gslItemAssignment_RemoveSlottedItem_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentDef* pDef = GET_REF(pData->hDef);
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		gslItemAssignments_NotifySend(pEnt, pDef, "ItemAssignments_RemoveSlottedItemSuccess");
	}
	else
	{
		gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_RemoveSlottedItemFailed");
	}
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

// Removes a slotted item from an active assignment
AUTO_TRANSACTION
ATR_LOCKS(pEnt, "pInventoryV2.ppLiteBags[], .Pplayer.Pitemassignmentpersisteddata, .pInventoryV2.Ppinventorybags, pPlayer.pPlayerAccountData.eaGamePermissionMaxValueNumerics[]")
ATR_LOCKS(pData,".Iversion,.Eakeys");
enumTransactionOutcome gslItemAssignments_tr_RemoveSlottedItem(ATR_ARGS, 
															   NOCONST(Entity)* pEnt, 
															   NOCONST(GameAccountData)* pData,
															   U32 uAssignmentID, 
															   U64 uItemID,
															   GameAccountDataExtract* pExtract)
{
	NOCONST(ItemAssignment)* pAssignment = ItemAssignment_trh_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);
	const char* pchKey = MicroTrans_GetItemAssignmentUnslotTokensGADKey();

	if (NONNULL(pAssignment))
	{
		NOCONST(ItemAssignmentPersistedData)* pAssignmentData = pEnt->pPlayer->pItemAssignmentPersistedData;
		S32 i;
		for (i = 0; i < eaSize(&pAssignment->eaSlottedItems); i++)
		{
			NOCONST(ItemAssignmentSlottedItem)* pSlottedItem = pAssignment->eaSlottedItems[i];

			if (pSlottedItem->uItemID == uItemID)
			{
				NOCONST(InventorySlot)* pSlot;
				pSlot = ItemAssignments_trh_GetInvSlotFromSlottedItem(ATR_PASS_ARGS, pEnt, pSlottedItem, pExtract);
				if (NONNULL(pSlot) && NONNULL(pSlot->pItem))
				{
					pSlot->pItem->flags &= ~kItemFlag_SlottedOnAssignment;
				}
				StructDestroyNoConst(parse_ItemAssignmentSlottedItem, eaRemove(&pAssignment->eaSlottedItems, i));

				// If there are no more slotted items, destroy the assignment
				if (!eaSize(&pAssignment->eaSlottedItems))
				{
					eaFindAndRemove(&pAssignmentData->eaActiveAssignments, pAssignment);
					StructDestroyNoConst(parse_ItemAssignment, pAssignment);
				}
				// Fail if the player cannot remove a token
				if (!slGAD_trh_ChangeAttribClamped(ATR_PASS_ARGS, pData, pchKey, -1, 0, 100000))
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}
				pData->iVersion++;
				return TRANSACTION_OUTCOME_SUCCESS;
			}
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

static void gslItemAssignments_UpdatePersonalAssignments(Entity* pEnt, ItemAssignmentLocationData* pData)
{
	// populate the client's personal assignment list 
	gslItemAssignments_UpdateAvailableAssignments(pEnt, NULL, pData, true);
		
	// Update the next process time
	gslItemAssignments_PlayerUpdateNextProcessTime(pEnt);
}

static void gslItemAssignments_UpdatePersonalAssignments_CB(TransactionReturnVal* pReturn, ItemAssignmentCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erEnt);
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);
	if (pEnt && pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		ItemAssignmentLocationData LocationData = {0};
		gslItemAssignments_GetPlayerLocationDataEx(pEnt, true, &LocationData);
		gslItemAssignments_UpdatePersonalAssignments(pEnt, &LocationData);
	}
	if (pPlayerData)
	{
		ItemAssignmentPersonalAssignmentBucket *pBucket = eaGet(&pPlayerData->eaPersonalAssignmentBuckets, pData->iPersonalBucketIndex);

		if (pBucket)
		{
			pBucket->uNextPersonalRequestTime = 0;
		}
	}
	StructDestroySafe(parse_ItemAssignmentCBData, &pData);
}

// update the personal bucket with the given array of refs in ItemAssignmentDefRefs
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pitemassignmentpersisteddata");
enumTransactionOutcome gslItemAssignments_tr_UpdatePersonalAssignments(ATR_ARGS, NOCONST(Entity)* pEnt, U32 uRequestTime, 
																		U32 bForceUpdate, S32 iPersonalBucket, 
																		ItemAssignmentDefRefs* pRefs)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer))
	{
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uLastRequestTime, uNextRequestTime;
		ItemAssignmentPersonalAssignmentSettings *pBucketSettings = NULL;
		NOCONST(ItemAssignmentPersonalPersistedBucket) *pPersistedBucket = NULL;
		
		if (ISNULL(pEnt->pPlayer->pItemAssignmentPersistedData))
		{
			pEnt->pPlayer->pItemAssignmentPersistedData = ItemAssignmentPersistedData_Create();
		}

		pPersistedBucket = ItemAssignmentPersistedData_GetPersonalBucket(pEnt->pPlayer->pItemAssignmentPersistedData, iPersonalBucket);
		if (!pPersistedBucket)
			return TRANSACTION_OUTCOME_FAILURE;
		
		pBucketSettings = eaGet(&g_ItemAssignmentSettings.eaPersonalAssignmentSettings, iPersonalBucket);
		if (!pBucketSettings)
			return TRANSACTION_OUTCOME_FAILURE;
		

		uLastRequestTime = pPersistedBucket->uLastPersonalUpdateTime;
		uNextRequestTime = uLastRequestTime + pBucketSettings->uAssignmentRefreshTime;
		if (bForceUpdate || !uLastRequestTime || uNextRequestTime <= uCurrentTime)
		{
			int i;
		
			// Clear previous list of personal assignments
			eaDestroyStructNoConst(&pPersistedBucket->eaAvailableAssignments, parse_ItemAssignmentDefRefCont);
			eaIndexedEnableNoConst(&pPersistedBucket->eaAvailableAssignments, parse_ItemAssignmentDefRefCont);
	
			// Copy the new list of personal assignments
			for (i = 0; i < eaSize(&pRefs->eaRefs); i++)
			{
				NOCONST(ItemAssignmentDefRefCont)* pRef = StructCreateNoConst(parse_ItemAssignmentDefRefCont);
				COPY_HANDLE(pRef->hDef, pRefs->eaRefs[i]->hDef); 
				eaPush(&pPersistedBucket->eaAvailableAssignments, pRef);

			}
			pPersistedBucket->uLastPersonalUpdateTime = uRequestTime;
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Pplayer.Pitemassignmentpersisteddata.Eacompletedassignments");
enumTransactionOutcome gslItemAssignments_tr_ClearCompleteTimes(ATR_ARGS, NOCONST(Entity)* pEnt)
{
	if (NONNULL(pEnt) && NONNULL(pEnt->pPlayer) && NONNULL(pEnt->pPlayer->pItemAssignmentPersistedData))
	{
		S32 i;

		for (i = 0; i < eaSize(&pEnt->pPlayer->pItemAssignmentPersistedData->eaCompletedAssignments); i++)
		{
			pEnt->pPlayer->pItemAssignmentPersistedData->eaCompletedAssignments[i]->uCompleteTime = 0;
		}
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// If bForce is set, then ignore completion restrictions
bool gslItemAssignments_CompleteAssignment(Entity* pEnt, ItemAssignment* pAssignment, const char* pchForceOutcome, bool bForce, bool bUseToken)
{
	ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
	U32 uCurrentTime = timeSecondsSince2000();
	U32 uCompleteTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);
	bool bCanComplete = bForce ? true : uCurrentTime >= uCompleteTime;
	ItemAssignmentOutcome* pOutcome;

	if (pAssignment->bCompletionPending)
	{
		return false;
	}
	if (bUseToken)
	{
		const char* pchKey = MicroTrans_GetItemAssignmentCompleteNowGADKey();
		S32 iNumTokens = gad_GetAttribInt(entity_GetGameAccount(pEnt), pchKey);
		if (iNumTokens < 1)
		{
			return false;
		}
	}
	if (pEnt->pPlayer && 
		pDef && bCanComplete && 
		!pAssignment->pchRewardOutcome && 
		(pOutcome = gslItemAssignments_ChooseOutcome(pEnt, pAssignment, pchForceOutcome)))
	{
		ContainerID uEntID = entGetContainerID(pEnt);
		bool bHasItemRewards = false;
		InventoryBag** eaRewardBags = NULL;
		ItemAssignmentCBData* pData = NULL;
		TransactionReturnVal* pReturn = NULL;
		ItemChangeReason reason = {0};
		GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		S32 iForcedCompletionCost = 0;

		if (bForce && g_ItemAssignmentSettings.pExprForceCompleteNumericCost)
		{
			if (!ItemAssignments_GetForceCompleteNumericCost(pEnt, pAssignment, &iForcedCompletionCost))
				return false;

			{
				ItemDef* pNumericDef = GET_REF(g_ItemAssignmentSettings.hForceCompleteNumeric);
				S32 iNumericValue;
				assert(pNumericDef);
				iNumericValue = inv_GetNumericItemValue(pEnt, pNumericDef->pchName);
				if (iNumericValue < iForcedCompletionCost)
					return false;
			}
		}

		pData = gslItemAssignments_CreateCBData(pEnt, pAssignment, pDef, pOutcome->pchName);
		pReturn = LoggedTransactions_CreateManagedReturnValEnt("CompleteItemAssignment", pEnt, gslItemAssignment_Complete_CB, pData);

		if (gslItemAssignmentOutcome_GenerateRewards(pEnt, pAssignment, NULL, pDef, pOutcome, false, &eaRewardBags))
		{
			S32 i;
			for (i = 0; i < eaSize(&eaRewardBags); i++)
			{
				InventoryBag* pBag = eaRewardBags[i];
				if (eaSize(&pBag->ppIndexedInventorySlots))
				{
					bHasItemRewards = true;
					break;
				}
			}
		}

		inv_FillItemChangeReason(&reason, pEnt, "ItemAssign:Complete", pDef->pchName);


		AutoTrans_gslItemAssignments_tr_CompleteAssignment(pReturn, objServerType(), 
			GLOBALTYPE_ENTITYPLAYER, uEntID, 
			GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID,
			pAssignment->uAssignmentID, pOutcome->pchName, bHasItemRewards, bForce, iForcedCompletionCost, bUseToken, &reason, pExtract);

		pAssignment->bCompletionPending = true;

		// Cleanup
		eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
		return true;
	}
	return false;
}

static void gslItemAssignments_PeriodicVolumeUpdate(Entity* pEnt)
{
	const char* pchRequestMap = zmapInfoGetPublicName(NULL);

	// This is currently the only reason to save off the last volume the player was in
	if (g_ItemAssignmentSettings.bInteriorsUseLastStaticMap && 
		ItemAssignments_PlayerCanAccessAssignments(pEnt) &&
		gslItemAssignments_GetCurrentMapRarityCounts(pchRequestMap))
	{
		ZoneMapType eMapType = zmapInfoGetMapType(NULL);
		if (eMapType == ZMTYPE_STATIC || eMapType == ZMTYPE_SHARED)
		{
			const char* pchVolumeName = NULL;
			gslItemAssignments_IsEntityInValidVolume(pEnt, &pchVolumeName);

			// Save off the volume used for generating the assignment list
			if (pchVolumeName)
			{
				pEnt->pPlayer->pchLastItemAssignmentVolume = allocAddString(pchVolumeName);
			}
			else
			{
				pEnt->pPlayer->pchLastItemAssignmentVolume = NULL;
			}
		}
	}
	else if (pEnt->pPlayer->pchLastItemAssignmentVolume)
	{
		pEnt->pPlayer->pchLastItemAssignmentVolume = NULL;
	}
}

// Update active and personal assignments on the player
void gslItemAssignments_UpdatePlayerAssignments(Entity* pEnt)
{	
	ItemAssignmentPersistedData* pPersistedData = pEnt->pPlayer->pItemAssignmentPersistedData;
	
	PERFINFO_AUTO_START_FUNC();

	gslItemAssignments_PeriodicVolumeUpdate(pEnt);

	if (pPersistedData && pPersistedData->uNextUpdateTime > 0)
	{
		U32 uCurrentTime = timeSecondsSince2000();
		bool bUpdated = false;
		if (uCurrentTime < pPersistedData->uNextUpdateTime)
		{
			PERFINFO_AUTO_STOP();
			return;
		}

		if (pEnt->pPlayer->pItemAssignmentData)
		{
			ItemAssignmentPlayerData *pData = pEnt->pPlayer->pItemAssignmentData;
			FOR_EACH_IN_EARRAY(pData->eaPersonalAssignmentBuckets, ItemAssignmentPersonalAssignmentBucket, pBucket)
			{
				if (pBucket->bUpdatedList && 
					gslItemAssignments_ShouldRequestPersonalAssignments(pEnt, NULL, FOR_EACH_IDX(-, pBucket)) )
				{
					bUpdated = true;
					eaDestroyStruct(&pBucket->eaAvailableAssignments, parse_ItemAssignmentDefRef);
					pBucket->bUpdatedList = false;
				}
			}
			FOR_EACH_END

			if (bUpdated)
			{
				gslItemAssignments_PlayerUpdateNextProcessTime(pEnt);
				// Set dirty bit
				entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
			}
			
		}
		
		/*
		if (SAFE_MEMBER3(pEnt, pPlayer, pItemAssignmentData, bUpdatedPersonalList) &&
			gslItemAssignments_ShouldRequestPersonalAssignments(pEnt, NULL))
		{
			eaDestroyStruct(&pEnt->pPlayer->pItemAssignmentData->eaAvailablePersonalAssignments, parse_ItemAssignmentDefRef);
			pEnt->pPlayer->pItemAssignmentData->bUpdatedPersonalList = false;

			gslItemAssignments_PlayerUpdateNextProcessTime(pEnt);
			
			// Set dirty bit
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
		else
		*/

		// changing old functionality because it seemed possibly prone to problems if we are updating and checking completion at the same time
		// so, always run the completion checks even if the we did update a personal assignment list
		{
			S32 i;
			for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
			{
				ItemAssignment* pAssignment = pPersistedData->eaActiveAssignments[i];

				gslItemAssignments_CompleteAssignment(pEnt, pAssignment, NULL, false, false);
			}
		}

	}
	
	PERFINFO_AUTO_STOP();
}

bool gslItemAssignments_UpdateGrantedAssignments(Entity* pEnt, ItemAssignmentDef* pDef, S32 eOperation)
{
	bool bSuccess = false;
	if (pEnt && pEnt->pPlayer && pDef)
	{
		ItemAssignmentDefRef* pRef;
		S32 i = ItemAssignments_PlayerFindGrantedAssignment(pEnt, pDef);

		switch (eOperation) 
		{
			xcase kItemAssignmentOperation_Add:
			{
				if (i < 0)
				{
					if (!pEnt->pPlayer->pItemAssignmentData)
					{
						pEnt->pPlayer->pItemAssignmentData = ItemAssignmentPlayerData_Create();
					}
					pRef = StructCreate(parse_ItemAssignmentDefRef);
					SET_HANDLE_FROM_REFERENT("ItemAssignmentDef", pDef, pRef->hDef);

					eaIndexedEnable(&pEnt->pPlayer->pItemAssignmentData->eaGrantedPersonalAssignments, parse_ItemAssignmentDefRef);
					eaPush(&pEnt->pPlayer->pItemAssignmentData->eaGrantedPersonalAssignments, pRef);

					bSuccess = true;
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

				}
				ClientCmd_gclItemAssignment_NewAssignment(pEnt, pDef->pchName);
			}
			xcase kItemAssignmentOperation_Remove:
			{
				if (i >= 0)
				{
					pRef = eaRemove(&pEnt->pPlayer->pItemAssignmentData->eaGrantedPersonalAssignments, i);
					StructDestroy(parse_ItemAssignmentDefRef, pRef);
					bSuccess = true;
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

				}
			}
		}
		if (bSuccess)
		{
			ItemAssignmentLocationData LocationData = {0};
			gslItemAssignments_GetPlayerLocationDataEx(pEnt, true, &LocationData);
			gslItemAssignments_UpdateAvailableAssignments(pEnt, NULL, &LocationData, true);
		}
	}
	return bSuccess;
}


// updates the player's list if necessary of the assignments as defined by the ItemAssignmentSettings' peAutograntAssignmentWeights
static void gslItemAssignments_UpdateAutograntedAssignments(Entity* pEnt, bool bForce)
{
	S32 i, numWeights;
	ItemAssignmentRefsInRarity** eaRarityLists = gslItemAssignments_GetRarityRefList();
	static ItemAssignmentDefRef **s_eaNewAssignmentList = NULL;
	U32 uCurrentTime = timeSecondsSince2000();
	bool bUpdated = false;
	S32 *piCategoryIsHidden = NULL;
	
	devassert(pEnt->pPlayer->pItemAssignmentData);

	if (!bForce && uCurrentTime < pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime)
	{
		return;
	}
	
	gslItemAssignments_CacheHiddenCategories(pEnt, &piCategoryIsHidden);

	eaIndexedEnable(&s_eaNewAssignmentList, parse_ItemAssignmentDefRef);

	entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime = uCurrentTime + 10;
	
	numWeights = eaiSize(&g_ItemAssignmentSettings.peAutograntAssignmentWeights);
	for(i = 0; i < numWeights; ++i)
	{
		ItemAssignmentWeightType eWeight = g_ItemAssignmentSettings.peAutograntAssignmentWeights[i];
		S32 idx = gslItemAssignment_RefListFindRarity(eaRarityLists, eWeight);
		ItemAssignmentRefsInRarity *pRarityRefs = eaGet(&eaRarityLists, idx);
		
		if (pRarityRefs && eaSize(&pRarityRefs->eaRefs))
		{
			// see if this weight is hidden
			if (gslItemAssignments_IsItemAssignmentWeightTypeHiddenByCategory(pRarityRefs->eWeight, piCategoryIsHidden))
				continue;
			
			FOR_EACH_IN_EARRAY_FORWARDS(pRarityRefs->eaRefs, ItemAssignmentDefRef, pRef)
			{
				if (gslItemAssignments_IsValidAvailableAssignment(pEnt, GET_REF(pRef->hDef), NULL))
				{
					eaPush(&s_eaNewAssignmentList, pRef);
				}
			}
			FOR_EACH_END
		}
	}

	if (eaSize(&s_eaNewAssignmentList) != eaSize(&pEnt->pPlayer->pItemAssignmentData->eaAutograntedPersonalAssignments))
	{
		bUpdated = true;
	}	
	else
	{
		S32 s = eaSize(&s_eaNewAssignmentList);
		for (i = 0; i < s; ++i)
		{
			if (!REF_COMPARE_HANDLES(s_eaNewAssignmentList[i]->hDef, 
									 pEnt->pPlayer->pItemAssignmentData->eaAutograntedPersonalAssignments[i]->hDef))
			{
				bUpdated = true;
				break;
			}
		}
	}

	if (bUpdated)
	{
		eaDestroyStruct(&pEnt->pPlayer->pItemAssignmentData->eaAutograntedPersonalAssignments, parse_ItemAssignmentDefRef);
		eaIndexedEnable(&pEnt->pPlayer->pItemAssignmentData->eaAutograntedPersonalAssignments, parse_ItemAssignmentDefRef);

		FOR_EACH_IN_EARRAY_FORWARDS(s_eaNewAssignmentList, ItemAssignmentDefRef, pRef)
		{
			ItemAssignmentDefRef* pNewRef = StructCreate(parse_ItemAssignmentDefRef);
			COPY_HANDLE(pNewRef->hDef, pRef->hDef);

			eaPush(&pEnt->pPlayer->pItemAssignmentData->eaAutograntedPersonalAssignments, pNewRef);
		}
		FOR_EACH_END

		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);

		if(GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
		{
			pEnt->pPlayer->pItemAssignmentData->uLastUpdateTime = uCurrentTime;
		}
	}

	eaClear(&s_eaNewAssignmentList);
	eaiDestroy(&piCategoryIsHidden);
}


void gslItemAssignment_AddRemoteAssignment(ItemAssignmentDef* pDef)
{
	int i;
	if (!s_pRemoteAssignments)
	{
		s_pRemoteAssignments = StructCreate(parse_ItemAssignmentDefRefs);
	}
	i = eaIndexedFindUsingString(&s_pRemoteAssignments->eaRefs, pDef->pchName);
	if (i < 0)
	{
		ItemAssignmentDefRef* pRef = StructCreate(parse_ItemAssignmentDefRef);

		if (!s_pRemoteAssignments->eaRefs)
			eaIndexedEnable(&s_pRemoteAssignments->eaRefs, parse_ItemAssignmentDefRef);
		
		SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pRef->hDef);
		eaPush(&s_pRemoteAssignments->eaRefs, pRef);
	}
}

void gslItemAssignment_ValidateTrackedActivity(const char* pchActivity)
{
	U32 uRefreshTime = g_ItemAssignmentSettings.uAssignmentRefreshTime;
	if (uRefreshTime)
	{
		RefDictIterator eventIterator;
		EventDef* pEventDef;
		char pchAdjTime[128];
		char pchPACTime[128];
		S32 i;

		RefSystem_InitRefDictIterator(g_hEventDictionary, &eventIterator);
		while(pEventDef = (EventDef *)RefSystem_GetNextReferentFromIterator(&eventIterator))
		{
			for (i = eaSize(&pEventDef->ppActivities)-1; i >= 0; i--)
			{
				if (pEventDef->ppActivities[i]->pchActivityName == pchActivity)
				{
					break;
				}
			}
			if (i >= 0)
			{
				for (i = eaSize(&pEventDef->ShardTimingDef.ppTimingEntries)-1; i >= 0; i--)
				{
					ShardEventTimingEntry* pTimingEntry = pEventDef->ShardTimingDef.ppTimingEntries[i];

					if (pTimingEntry->uDateStart)
					{
						U32 uMod = pTimingEntry->uDateStart % uRefreshTime;
						if (uMod)
						{
							U32 uAdjustedTime = pTimingEntry->uDateStart - uMod;
							timeMakeDateStringFromSecondsSince2000(pchAdjTime,uAdjustedTime);
							timeMakePACDateStringFromSecondsSince2000(pchPACTime,uAdjustedTime);
							Errorf("Activity (%s) start time doesn't line up with periodic map refresh time. Suggestion: UTC %s, PAC %s",
								pchActivity, pchAdjTime, pchPACTime);
						}
					}
					if (pTimingEntry->uTimeEnd)
					{
						U32 uMod = pTimingEntry->uTimeEnd % uRefreshTime;
						if (uMod)
						{
							U32 uAdjustedTime = pTimingEntry->uTimeEnd - uMod;
							timeMakeDateStringFromSecondsSince2000(pchAdjTime,uAdjustedTime);
							timeMakePACDateStringFromSecondsSince2000(pchPACTime,uAdjustedTime);
							Errorf("Activity (%s) end time doesn't line up with periodic map refresh time. Suggestion: UTC %s, PAC %s",
								pchActivity, pchAdjTime, pchPACTime);
						}
					}
					if (pTimingEntry->uTimeRepeat)
					{
						U32 uMod = pTimingEntry->uTimeRepeat % uRefreshTime;
						if (uMod)
						{
							U32 uAdjustedTime = pTimingEntry->uTimeRepeat - uMod;
							timeMakeDateStringFromSecondsSince2000(pchAdjTime,uAdjustedTime);
							timeMakePACDateStringFromSecondsSince2000(pchPACTime,uAdjustedTime);
							Errorf("Activity (%s) repeat time doesn't line up with periodic map refresh time. Suggestion: UTC %s, PAC %s",
								pchActivity, pchAdjTime, pchPACTime);
						}
					}
				}
			}
		}
	}
}

void gslItemAssignment_AddTrackedActivity(ItemAssignmentDef* pDef)
{
	if (pDef->pchFeaturedActivity && pDef->pchFeaturedActivity[0])
	{
		int i = (int)eaBFind(s_ppchTrackedAssignmentActivities, strCmp, pDef->pchFeaturedActivity);
		if (i == eaSize(&s_ppchTrackedAssignmentActivities) || s_ppchTrackedAssignmentActivities[i] != pDef->pchFeaturedActivity)
		{
			eaInsert(&s_ppchTrackedAssignmentActivities, pDef->pchFeaturedActivity, i);

			if (isDevelopmentMode())
			{
				gslItemAssignment_ValidateTrackedActivity(pDef->pchFeaturedActivity);
			}
		}
	}
}

void gslItemAssignments_NotifyActivityStarted(const char* pchActivityName)
{
	int i = (int)eaBFind(s_ppchTrackedAssignmentActivities, strCmp, pchActivityName);
	if (i < eaSize(&s_ppchTrackedAssignmentActivities) && s_ppchTrackedAssignmentActivities[i] == pchActivityName)
	{
		// Rebuild the assignment tree if an activity that we care about starts
		g_bRebuildItemAssignmentTree = true;
	}
}

U32 gslItemAssignment_GetRefreshIndexOffset(void)
{
	return s_iRefreshIndexOffset;
}

///////////////////////////////////////////////////////////////////////////////////////////
// Commands
///////////////////////////////////////////////////////////////////////////////////////////

bool gslUpdatePersistedItemAssignmentList(Entity *pEnt, bool *bUpdatedPersonalAssignments)
{
	U32 uRequestTime = 0;
	U32 uCurrentTime = timeSecondsSince2000();
	bool bReturn = false;
	
	if (!pEnt->pPlayer->pItemAssignmentData)
	{
		pEnt->pPlayer->pItemAssignmentData = ItemAssignmentPlayerData_Create();
	}

	// go through each personal assignment bucket and see if we should generate the personal assignment list again
	FOR_EACH_IN_EARRAY(g_ItemAssignmentSettings.eaPersonalAssignmentSettings, 
		ItemAssignmentPersonalAssignmentSettings, pPersonalBucketSettings)
	{

		if (gslItemAssignments_ShouldRequestPersonalAssignments(pEnt, &uRequestTime, FOR_EACH_IDX(-, pPersonalBucketSettings)))
		{
			ItemAssignmentPersonalPersistedBucket *pPersistedPersonalBucket = NULL;

			bReturn = true;
			if (pEnt->pPlayer->pItemAssignmentPersistedData)
				pPersistedPersonalBucket = eaGet(&pEnt->pPlayer->pItemAssignmentPersistedData->eaPersistedPersonalAssignmentBuckets, FOR_EACH_IDX(-, pPersonalBucketSettings));

			if (!pPersistedPersonalBucket || !pPersistedPersonalBucket->uLastPersonalUpdateTime || 
				uRequestTime != pPersistedPersonalBucket->uLastPersonalUpdateTime)
			{
				// we are going to try and update this list now, 
				// Run a transaction to update the personal assignment timer
				U32 uSeed = uRequestTime;
				ItemAssignmentDefRef** eaRefs = NULL;

				gslItemAssignments_GenerateAssignmentList(pEnt, pPersonalBucketSettings->peRarityCounts, NULL, 
															&uSeed, &eaRefs);

				{
					bool bForceUpdate = pEnt->pPlayer->pItemAssignmentData->bDebugForceRequestPersonalAssignments;

					ContainerID uEntID = entGetContainerID(pEnt);
					ItemAssignmentDefRefs Refs = {0};
					ItemAssignmentCBData* pData = gslItemAssignments_CreateCBData(pEnt, NULL, NULL, NULL);
					TransactionReturnVal* pReturn = NULL;
					ItemAssignmentPersonalAssignmentBucket *pPersonalBucket = NULL;

					pData->iPersonalBucketIndex = FOR_EACH_IDX(-, pPersonalBucketSettings);

					pReturn = LoggedTransactions_CreateManagedReturnValEnt("UpdatePersonalItemAssignmentTime", pEnt, gslItemAssignments_UpdatePersonalAssignments_CB, pData);

					Refs.eaRefs = eaRefs;
					AutoTrans_gslItemAssignments_tr_UpdatePersonalAssignments(pReturn, objServerType(), GLOBALTYPE_ENTITYPLAYER, 
						uEntID, uRequestTime, bForceUpdate, 
						pData->iPersonalBucketIndex, &Refs);

					pPersonalBucket = eaGet(&pEnt->pPlayer->pItemAssignmentData->eaPersonalAssignmentBuckets, FOR_EACH_IDX(-, pPersonalBucketSettings));
					if (pPersonalBucket)
						pPersonalBucket->uNextPersonalRequestTime = uCurrentTime + ITEM_ASSIGNMENT_REQUEST_TIMEOUT;

					pEnt->pPlayer->pItemAssignmentData->bDebugForceRequestPersonalAssignments = false;

					StructDeInit(parse_ItemAssignmentDefRefs, &Refs);
					if(bUpdatedPersonalAssignments)
						(*bUpdatedPersonalAssignments) = true;
				}
			}
		}
	}
	FOR_EACH_END

	return bReturn;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslRequestItemAssignments(Entity* pEnt)
{
	ItemAssignmentLocationData LocationData = {0};
	ItemAssignmentRarityCountType* peMapRarityCounts = NULL;
	U32 uCurrentTime = timeSecondsSince2000();
	bool bRequestedPersonalAssignments = false;
	bool bUpdatedPersonalAssignments = false;

	PERFINFO_AUTO_START_FUNC();

	if (!pEnt || !pEnt->pPlayer || !g_ItemAssignmentSettings.uAssignmentRefreshTime)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	if (!ItemAssignments_PlayerCanAccessAssignments(pEnt))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	gslItemAssignments_GetPlayerLocationDataEx(pEnt, true, &LocationData);
	if (!LocationData.bInValidVolume)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	if (!(peMapRarityCounts = gslItemAssignments_GetCurrentMapRarityCounts(LocationData.pchMapName)) 
		&& !g_ItemAssignmentSettings.bDoNotRequireMapRaritiesForAssignmentGeneration)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (!pEnt->pPlayer->pItemAssignmentData)
	{
		pEnt->pPlayer->pItemAssignmentData = ItemAssignmentPlayerData_Create();
	}
	
	bRequestedPersonalAssignments = gslUpdatePersistedItemAssignmentList(pEnt,&bUpdatedPersonalAssignments);

	if (!bUpdatedPersonalAssignments)
	{
		gslItemAssignments_UpdatePersonalAssignments(pEnt, &LocationData);
	}
	else if (!bRequestedPersonalAssignments)
	{
		gslItemAssignments_UpdateAvailableAssignments(pEnt, NULL, &LocationData, true);
	}
	
	gslItemAssignments_UpdateAutograntedAssignments(pEnt, false);

	if (peMapRarityCounts && LocationData.pchMapName)
	{
		int iPartitionIdx = entGetPartitionIdx(pEnt);
		bool bHasPersonalAssignments = false;
		bool bHasPublicAssignments = false;
		ItemAssignmentVolumeData* pVolumeData = gslItemAssignments_GetVolumeData(iPartitionIdx, LocationData.pchVolumeName);
		S32 iRefreshIndex;
		
		iRefreshIndex = uCurrentTime / MAX(g_ItemAssignmentSettings.uAssignmentRefreshTime, 1);
		iRefreshIndex = iRefreshIndex % (1<<ITEM_ASSIGNMENT_SEED_NUM_TIME_BITS);
		iRefreshIndex += s_iRefreshIndexOffset;

		if (g_bRebuildItemAssignmentTree ||
			!s_pAvailableItemAssignmentsCached ||
			s_pAvailableItemAssignmentsCached->pchRequestMap != LocationData.pchMapName ||
			(!pVolumeData || pVolumeData->iCurrentRefreshIndex != iRefreshIndex))
		{
			U32 uSeed = gslItemAssignments_GetSeedForPeriodicUpdate(LocationData.pchMapName, LocationData.pchVolumeName, iRefreshIndex);
			
			if (!s_pAvailableItemAssignmentsCached)
			{
				s_pAvailableItemAssignmentsCached = StructCreate(parse_ItemAssignmentMapData);
			}
			// Update the map name
			if (s_pAvailableItemAssignmentsCached->pchRequestMap != LocationData.pchMapName)
			{
				s_pAvailableItemAssignmentsCached->pchRequestMap = allocAddString(LocationData.pchMapName);
			}
			// Update volume data
			if (!pVolumeData)
			{
				pVolumeData = StructCreate(parse_ItemAssignmentVolumeData);
				pVolumeData->pchVolumeName = allocAddString(LocationData.pchVolumeName);
				pVolumeData->iPartitionIdx = iPartitionIdx;
				eaPush(&s_pAvailableItemAssignmentsCached->eaVolumeData, pVolumeData);
			}
			else
			{
				eaClearStruct(&pVolumeData->eaRefs, parse_ItemAssignmentDefRef);
			}
			pVolumeData->iCurrentRefreshIndex = iRefreshIndex;

			// Generate a random list of assignments using the seed
			gslItemAssignments_GenerateAssignmentList(pEnt, peMapRarityCounts,
													  &LocationData,
													  &uSeed,
													  &pVolumeData->eaRefs);
		}
		// Update the player's available assignment list
		gslItemAssignments_UpdateAvailableAssignments(pEnt, pVolumeData, NULL, false);
	}

	PERFINFO_AUTO_STOP();

}

// Determine if the assignment has requirements that include a unique item
bool gslItemAssignment_RequirementIncludesUniqueItem(ItemAssignment *pAssignment)
{
	ItemAssignmentDef *pDef = GET_REF(pAssignment->hDef);
	int i;

	if (pDef) {
		for (i = 0; i < eaSize(&pDef->pRequirements->eaItemCosts); i++)
		{
			ItemAssignmentItemCost* pItemCost = pDef->pRequirements->eaItemCosts[i];
			ItemDef* pItemDef = GET_REF(pItemCost->hItem);

			if (pItemDef && (pItemDef->flags & kItemDefFlag_Unique))
			{
				return true;
			}
		}
	}

	return false;
}

// returns true if the items can be returned to the entity
// will also send failure notifications to the player if there was a failure
S32 gslItemAssignments_CheckAndNotifyIfCannotRecieveCanceledItems(Entity *pEnt, ItemAssignmentDef* pDef, 
																	ItemChangeReason *pReason,
																	GameAccountDataExtract* pExtract)
{
	// create a fake entity from ours and try and give back the items to the entity
	Entity* pFakeEnt = entity_CreateOwnerCopy(pEnt, pEnt, true, true, false, false, false);
	S32 bRet = true;
	FOR_EACH_IN_EARRAY(pDef->pRequirements->eaItemCosts, ItemAssignmentItemCost, pItemCost)
	{
		S32 iCount = pItemCost->iCount;
		ItemDef* pItemDef = GET_REF(pItemCost->hItem);

		if (pItemDef)
		{
			InvBagIDs eBagID = InvBagIDs_None;
			enumTransactionOutcome eResult = TRANSACTION_OUTCOME_SUCCESS;
			
			if (pItemDef->eType != kItemType_Numeric)
			{
				Item* pItem = pItemDef ? item_FromDefName(pItemDef->pchName) : NULL;
				eBagID = inv_trh_GetBestBagForItemDef(CONTAINER_NOCONST(Entity, pFakeEnt), pItemDef, iCount, true, pExtract);
				CONTAINER_NOCONST(Item, pItem)->count = iCount;
				eResult = inv_AddItem(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pFakeEnt), NULL, eBagID, -1, 
										pItem, pItemDef->pchName, kRewardOverflow_DisallowOverflowBag, pReason, pExtract);
				StructDestroy(parse_Item, pItem);
			}

			if (eResult == TRANSACTION_OUTCOME_FAILURE)
			{
				const char* pchBagFull = NULL;
				
				bRet = false;
				if (eBagID != InvBagIDs_None)
				{
					NOCONST(InventoryBag)* pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), (InvBagIDs)eBagID, pExtract);
					const InvBagDef* pBagDef = invbag_trh_def(pBag);
					if (pBagDef)
					{
						pchBagFull = entTranslateDisplayMessage(pEnt, pBagDef->msgBagFull);
					}
				}
				if (pchBagFull && pchBagFull[0])
				{
					gslItemAssignments_NotifySendWithReason(pEnt, kNotifyType_ItemAssignmentFeedbackFailed, pDef, "ItemAssignments_CancelActiveFailedReason", pchBagFull);
					break;
				}
				else
				{
					gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_CancelActiveFailed");
					break;
				}
			}
		}
	}
	FOR_EACH_END

	StructDestroy(parse_Entity, pFakeEnt);
	return bRet;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentCancelActiveAssignment) ACMD_HIDE;
void gslItemAssignments_CancelActiveAssignment(Entity* pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (pAssignment)
	{
		ItemAssignmentDef* pDef = GET_REF(pAssignment->hDef);
		ContainerID uEntID = entGetContainerID(pEnt);
		U32 uCurrentTime = timeSecondsSince2000();
		U32 uCompleteTime = pAssignment->uTimeStarted + ItemAssignment_GetDuration(pAssignment, pDef);
		
		if (pDef && pDef->bIsAbortable && uCurrentTime < uCompleteTime)
		{
			ItemAssignmentCBData* pData = gslItemAssignments_CreateCBData(pEnt, pAssignment, pDef, NULL);
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemAssignmentCancelActive", pEnt, gslItemAssignment_CancelActive_CB, pData);
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			ItemChangeReason reason = {0};
			U32* eaPets = NULL;

			// Only lock the pets if the assignment requirements include a unique item
			if (gslItemAssignment_RequirementIncludesUniqueItem(pAssignment)) 
			{
				ea32Create(&eaPets);
				Entity_GetPetIDList(pEnt, &eaPets);
			}

			inv_FillItemChangeReason(&reason, pEnt, "ItemAssign:Cancel", pDef->pchName);

			if (gslItemAssignments_CheckAndNotifyIfCannotRecieveCanceledItems(pEnt, pDef, &reason, pExtract))
			{
				AutoTrans_gslItemAssignment_tr_CancelActive(pReturn, objServerType(), 
															GLOBALTYPE_ENTITYPLAYER, uEntID,
															GLOBALTYPE_ENTITYSAVEDPET, &eaPets,
															uAssignmentID, &reason, pExtract);
			}

			ea32Destroy(&eaPets);
		}
		else
		{
			gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_CancelActiveFailed");
		}
	}
}

void gslItemAssignments_FillOutcomeRewardRequest(Entity* pEnt, 
														ItemAssignment* pAssignment,
														ItemAssignmentCompletedDetails* pDetails,
														ItemAssignmentDef* pDef,
														ItemAssignmentOutcome* pOutcome,
														ItemAssignmentOutcomeRewardRequest* pRequest)
{
	InventoryBag** eaRewardBags = NULL;
	gslItemAssignmentOutcome_GenerateRewards(pEnt, pDetails ? NULL : pAssignment, pDetails, pDef, pOutcome, true, &eaRewardBags);
	
	pRequest->pchOutcome = allocAddString(pOutcome->pchName);
	if (eaSize(&eaRewardBags) || pDef->iCompletionExperience)
	{
		pRequest->pData = StructCreate(parse_InvRewardRequest);

		if (eaSize(&eaRewardBags))
			inv_FillRewardRequest(eaRewardBags, pRequest->pData);

		gslItemAssignmentDef_AddCategoryXPNumericToRewardRequest(pDef, pRequest->pData);
	}

	eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentRequestRewards) ACMD_PRIVATE;
void gslItemAssignments_RequestRewards(Entity* pEnt, const char* pchAssignmentDef, U32 uAssignmentID)
{
	ItemAssignmentRewardRequestData* pRequestData = StructCreate(parse_ItemAssignmentRewardRequestData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);

	SET_HANDLE_FROM_REFERENT(g_hItemAssignmentDict, pDef, pRequestData->hDef);

	if (uAssignmentID)
	{
		ItemAssignmentCompletedDetails* pCompletedDetails = ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, uAssignmentID);
		ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);
		ItemAssignmentOutcome* pOutcome = NULL;

		if (pAssignment)
		{
			pOutcome = ItemAssignment_GetOutcomeByName(pDef, pAssignment->pchRewardOutcome);
		}
		else if (pCompletedDetails)
		{
			pOutcome = ItemAssignment_GetOutcomeByName(pDef, pCompletedDetails->pchOutcome);
		}

		if (pOutcome)
		{
			ItemAssignmentOutcomeRewardRequest* pRequest = StructCreate(parse_ItemAssignmentOutcomeRewardRequest);
			gslItemAssignments_FillOutcomeRewardRequest(pEnt, pAssignment, pCompletedDetails, pDef, pOutcome, pRequest);
			eaPush(&pRequestData->eaOutcomes, pRequest);
		}
	}
	else if (pDef)
	{
		S32 i;
		for (i = 0; i < eaSize(&pDef->eaOutcomes); i++)
		{
			ItemAssignmentOutcomeRewardRequest* pRequest = StructCreate(parse_ItemAssignmentOutcomeRewardRequest);
			ItemAssignmentOutcome* pOutcome = pDef->eaOutcomes[i];
			gslItemAssignments_FillOutcomeRewardRequest(pEnt, NULL, NULL, pDef, pOutcome, pRequest);
			eaPush(&pRequestData->eaOutcomes, pRequest);
		}
	}

	ClientCmd_gclItemAssignments_ReceiveRewardsForOutcome(pEnt, pRequestData);
	StructDestroy(parse_ItemAssignmentRewardRequestData, pRequestData);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentCollectRewards) ACMD_HIDE;
void gslItemAssignments_CollectRewards(Entity* pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);
	ItemAssignmentCompletedDetails* pCompletedDetails = ItemAssignment_EntityGetRecentlyCompletedAssignmentByID(pEnt, uAssignmentID);
	ItemAssignmentDef* pDef = pAssignment ? GET_REF(pAssignment->hDef) : NULL;

	if (pDef && pAssignment->pchRewardOutcome)
	{
		ItemAssignmentOutcome* pOutcome = ItemAssignment_GetOutcomeByName(pDef, pAssignment->pchRewardOutcome);
		ContainerID uEntID = entGetContainerID(pEnt);
		
		if (pOutcome)
		{
			InventoryBag** eaRewardBags = NULL;
			if (gslItemAssignmentOutcome_GenerateRewards(pEnt, NULL, pCompletedDetails, pDef, pOutcome, true, &eaRewardBags))
			{
				int i;
				S32 eFailBag = InvBagIDs_None;
				Entity* pFakeEnt = entity_CreateOwnerCopy(pEnt, pEnt, true, true, false, false, false);
				GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
				GiveRewardBagsData Rewards = {0};
				ItemChangeReason reason = {0};
				NOCONST(Entity)** eaPetEnts = NULL;
				bool bUniqueItemsInBags = inv_CheckUniqueItemsInBags(eaRewardBags);
				bool bSuccess = false;
				
				inv_FillItemChangeReason(&reason, pEnt, "ItemAssign:CollectRewards", pDef->pchName);
				Rewards.ppRewardBags = eaRewardBags;

				if (bUniqueItemsInBags)
				{
					if (pEnt->pSaved)
					{
						for (i = eaSize(&pEnt->pSaved->ppOwnedContainers)-1; i >= 0; i--)
						{
							PetRelationship* pPet = pEnt->pSaved->ppOwnedContainers[i];
							PuppetEntity* pPuppet = SavedPet_GetPuppetFromPet(pEnt, pPet);
							Entity* pPetEnt = SavedPet_GetEntity(entGetPartitionIdx(pEnt), pPet);

							if (pPetEnt)
							{
								if (pPuppet)
								{
									if (pPuppet->curType != pEnt->pSaved->pPuppetMaster->curType ||	
										pPuppet->curID != pEnt->pSaved->pPuppetMaster->curID)
									{
										eaPush(&eaPetEnts, CONTAINER_NOCONST(Entity, pPetEnt));
									}
								}
								else
								{
									eaPush(&eaPetEnts, CONTAINER_NOCONST(Entity, pPetEnt));
								}
							}
						}
					}
				}

				if (inv_trh_GiveRewardBags(ATR_EMPTY_ARGS, CONTAINER_NOCONST(Entity, pFakeEnt), eaPetEnts, &Rewards, kRewardOverflow_DisallowOverflowBag, &eFailBag, &reason, pExtract, NULL))
				{
					bSuccess = true;
				}
				eaDestroy(&eaPetEnts);

				if (bSuccess)
				{
					U32* eaPets = NULL;
					ItemAssignmentCBData* pData = gslItemAssignments_CreateCBData(pEnt, pAssignment, pDef, NULL);
					TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemAssignmentCollectRewards", pEnt, gslItemAssignment_CollectRewards_CB, pData);

					if (bUniqueItemsInBags)
					{
						ea32Create(&eaPets);
						Entity_GetPetIDList(pEnt, &eaPets);				
					}

					AutoTrans_gslItemAssignment_tr_CollectRewards(pReturn, objServerType(), 
																	GLOBALTYPE_ENTITYPLAYER, uEntID, 
																	GLOBALTYPE_ENTITYSAVEDPET, &eaPets, 
																	uAssignmentID, &Rewards, &reason, pExtract);

					if (eaPets)
						ea32Destroy(&eaPets);

					if (pEnt->pPlayer->pItemAssignmentData)
						pEnt->pPlayer->pItemAssignmentData->uNextAutograntUpdateTime = 0;

					// check the unlock slot expressions
					gslItemAssignments_CheckExpressionSlots(pEnt, pCompletedDetails);
				}
				else
				{
					const char* pchBagFull = NULL;
					if (eFailBag != InvBagIDs_None)
					{
						NOCONST(InventoryBag)* pBag = inv_GetBag(CONTAINER_NOCONST(Entity, pEnt), (InvBagIDs)eFailBag, pExtract);
						const InvBagDef* pBagDef = invbag_trh_def(pBag);
						if (pBagDef)
						{
							pchBagFull = entTranslateDisplayMessage(pEnt, pBagDef->msgBagFull);
						}
					}
					if (pchBagFull && pchBagFull[0])
					{
						gslItemAssignments_NotifySendWithReason(pEnt, kNotifyType_ItemAssignmentFeedbackFailed, pDef, "ItemAssignments_CollectRewardsFailedReason", pchBagFull);
					}
					else
					{
						gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_CollectRewardsFailed");
					}
				}
				eaDestroyStruct(&eaRewardBags, parse_InventoryBag);
				StructDestroy(parse_Entity, pFakeEnt);
			}
			else
			{
				AutoTrans_gslItemAssignment_tr_RemoveAndDestroyActiveAssignment(NULL, objServerType(), 
																				GLOBALTYPE_ENTITYPLAYER, uEntID, 
																				uAssignmentID);
				gslItemAssignment_AwardCompletionExperience(pEnt, pDef, uAssignmentID);

				// check the unlock slot expressions
				gslItemAssignments_CheckExpressionSlots(pEnt, pCompletedDetails);

			}
		}
	}
}


// the server command function clients call to start a new assignment
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_PRIVATE;
void gslItemAssignments_StartNewAssignment(Entity* pEnt, const char* pchAssignmentDef, S32 iAssignmentSlot, ItemAssignmentSlots* pSlots)
{
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentDef);
	if (pEnt && pEnt->pPlayer && pDef && !pDef->bDisabled && pSlots)
	{
		bool bCanStart = false;
		
		if (pDef->bCanStartRemotely)
		{
			// Players can always start remote assignments
			bCanStart = true;
		}
		else
		{
			bCanStart = ItemAssignments_HasAssignment(pEnt, pDef, pEnt->pPlayer->pItemAssignmentData);
		}

		// if we care about iAssignmentSlot, check to make sure there are no conflicts.
		if (g_ItemAssignmentSettings.pStrictAssignmentSlots)
		{
			if (!ItemAssignments_IsValidNewItemAssignmentSlot(pEnt, g_ItemAssignmentSettings.pStrictAssignmentSlots, iAssignmentSlot))
			{	// todo: tell the client something has gone wrong.
				// ClientCmd_
				return;
			}
		}


		if (bCanStart && ItemAssignments_CheckRequirements(pEnt, pDef, pSlots))
		{
			U32 uCurrentTime = timeSecondsSince2000();
			ContainerID uEntID = entGetContainerID(pEnt);
			ItemAssignmentCBData* pData = gslItemAssignments_CreateCBData(pEnt, NULL, pDef, NULL);
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemAssignmentStartNew", pEnt, gslItemAssignment_StartNew_CB, pData);
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
			ItemChangeReason reason = {0};
			const char* pchMapMsgKey = zmapInfoGetDisplayNameMsgKey(NULL);
			U32 uDuration;
			inv_FillItemChangeReason(&reason, pEnt, "ItemAssign:Start", pDef->pchName);

			// get the proper duration for this assignment
			uDuration = ItemAssignments_CalculateDuration(pEnt, pDef, pSlots->eaSlots);

			AutoTrans_gslItemAssignments_tr_StartNew(pReturn, objServerType(), GLOBALTYPE_ENTITYPLAYER, uEntID, 
													pDef->pchName, pchMapMsgKey, iAssignmentSlot, pSlots, uCurrentTime, uDuration, &reason, pExtract);
		}
		else
		{
			gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_StartNewFailed");
		}
	}
}

// Mark a recently completed assignment as having been seen by the user
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentMarkCompletedAsRead) ACMD_HIDE;
void gslItemAssignments_MarkCompletedAsRead(Entity* pEnt, U32 uAssignmentID)
{
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	if (pPersistedData)
	{
		bool bDirty = false;
		S32 i;

		// Iterate over completed assignments and mark all that match uAssignmentID as read. 
		// This is required since there was a rare bug that allowed multiple completed assignments to have the same assignment ID
		for (i = eaSize(&pPersistedData->eaRecentlyCompletedAssignments)-1; i >= 0; i--)
		{
			ItemAssignmentCompletedDetails* pCompletedDetails = pPersistedData->eaRecentlyCompletedAssignments[i];
			if (pCompletedDetails->uAssignmentID == uAssignmentID)
			{
				// Mark the details as read by the user
				pCompletedDetails->bMarkedAsRead = true;
				bDirty = true;
			}
		}
		if (bDirty)
		{
			// Set dirty bit
			entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(RequestRemoteAssignments) ACMD_HIDE ACMD_PRIVATE;
void gslItemAssignments_RequestRemoteAssignments(Entity* pEnt)
{
	ClientCmd_gclItemAssignments_ReceiveRemoteAssignments(pEnt, s_pRemoteAssignments);
}

///////////////////////////////////////////////////////////////////////////////////////////
// MicroTransaction Commands
///////////////////////////////////////////////////////////////////////////////////////////

// Test function to remove a slotted item from an active assignment
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentRemoveSlottedItem) ACMD_HIDE;
void gslItemAssignments_RemoveSlottedItemFromActiveAssignment(Entity* pEnt, U32 uAssignmentID, U64 uItemID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);
	ItemAssignmentDef* pDef = pAssignment ? GET_REF(pAssignment->hDef) : NULL;
	S32 iNumTokens = gad_GetAttribInt(entity_GetGameAccount(pEnt), MicroTrans_GetItemAssignmentUnslotTokensGADKey());

	if (pEnt->pPlayer && pDef && pDef->bAllowItemUnslotting && iNumTokens > 0)
	{
		S32 i;
		for (i = eaSize(&pAssignment->eaSlottedItems)-1; i >= 0; i--)
		{
			ItemAssignmentSlottedItem* pSlottedItem = pAssignment->eaSlottedItems[i];
			if (pSlottedItem->uItemID == uItemID)
			{
				break;
			}
		}
		if (i >= 0)
		{
			ContainerID uEntID = entGetContainerID(pEnt);
			ItemAssignmentCBData* pData = gslItemAssignments_CreateCBData(pEnt, pAssignment, pDef, NULL);
			TransactionReturnVal* pReturn = LoggedTransactions_CreateManagedReturnValEnt("ItemAssignmentRemoveSlottedItem", pEnt, gslItemAssignment_RemoveSlottedItem_CB, pData);
			GameAccountDataExtract* pExtract = entity_GetCachedGameAccountDataExtract(pEnt);

			AutoTrans_gslItemAssignments_tr_RemoveSlottedItem(pReturn, objServerType(), 
															  GLOBALTYPE_ENTITYPLAYER, uEntID, 
															  GLOBALTYPE_GAMEACCOUNTDATA, pEnt->pPlayer->accountID,
															  uAssignmentID, uItemID, pExtract);
		}
		else
		{
			gslItemAssignments_NotifySendFailure(pEnt, pDef, "ItemAssignments_RemoveSlottedItemFailed");
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsCompleteNowByID) ACMD_HIDE;
void gslItemAssignments_CompleteNowByID(Entity* pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);
	ItemAssignmentDef* pDef = pAssignment ? GET_REF(pAssignment->hDef) : NULL;

	if (pDef && !pDef->bAllowItemUnslotting)
	{
		bool bUseToken = g_ItemAssignmentSettings.pExprForceCompleteNumericCost == NULL;
		gslItemAssignments_CompleteAssignment(pEnt, pAssignment, NULL, true, bUseToken);
	}
}

///////////////////////////////////////////////////////////////////////////////////////////
// Debug Commands
///////////////////////////////////////////////////////////////////////////////////////////

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceUpdate);
void gslItemAssignments_ForceUpdate(Entity* pEnt)
{
	s_iRefreshIndexOffset++;

	if (pEnt->pPlayer && pEnt->pPlayer->pItemAssignmentData)
	{
		pEnt->pPlayer->pItemAssignmentData->bDebugForceRequestPersonalAssignments = true;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteWithOutcome);
void gslItemAssignments_ForceCompleteWithOutcome(Entity* pEnt, U32 uAssignmentID, const char* pchOutcome)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (pAssignment)
	{
		gslItemAssignments_CompleteAssignment(pEnt, pAssignment, pchOutcome, true, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteByID);
void gslItemAssignments_ForceCompleteByID(Entity* pEnt, U32 uAssignmentID)
{
	ItemAssignment* pAssignment = ItemAssignment_EntityGetActiveAssignmentByID(pEnt, uAssignmentID);

	if (pAssignment)
	{
		gslItemAssignments_CompleteAssignment(pEnt, pAssignment, NULL, true, false);
	}
}

// Completes all assignments with the given name
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteByNameWithOutcome);
void gslItemAssignments_ForceCompleteByNameWithOutcome(Entity* pEnt, ACMD_NAMELIST("ItemAssignmentDef", REFDICTIONARY) const char* pchAssignmentName, const char* pchOutcome)
{
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	ItemAssignmentDef* pDef = ItemAssignment_DefFromName(pchAssignmentName);

	if (pPersistedData && pDef)
	{
		S32 i;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			ItemAssignment* pAssignment = pPersistedData->eaActiveAssignments[i];
			if (pDef == GET_REF(pAssignment->hDef))
			{
				gslItemAssignments_CompleteAssignment(pEnt, pAssignment, pchOutcome, true, false);
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteByName);
void gslItemAssignments_ForceCompleteByName(Entity* pEnt, ACMD_NAMELIST("ItemAssignmentDef", REFDICTIONARY) const char* pchAssignmentName)
{
	gslItemAssignments_ForceCompleteByNameWithOutcome(pEnt, pchAssignmentName, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteAllWithOutcome);
void gslItemAssignments_ForceCompleteAllWithOutcome(Entity* pEnt, const char* pchOutcome)
{
	ItemAssignmentPersistedData* pPersistedData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentPersistedData);
	if (pPersistedData)
	{
		S32 i;
		for (i = eaSize(&pPersistedData->eaActiveAssignments)-1; i >= 0; i--)
		{
			ItemAssignment* pAssignment = pPersistedData->eaActiveAssignments[i];
			gslItemAssignments_CompleteAssignment(pEnt, pAssignment, pchOutcome, true, false);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_SERVERCMD ACMD_NAME(ItemAssignmentsForceCompleteAll);
void gslItemAssignments_ForceCompleteAll(Entity* pEnt)
{
	gslItemAssignments_ForceCompleteAllWithOutcome(pEnt, NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsForceMap);
void gslItemAssignments_ForceMap(Entity* pEnt, ACMD_NAMELIST("ZoneMap", REFDICTIONARY) const char* pchOverrideMap)
{
	pchOverrideMap = allocFindString(pchOverrideMap);
	if (s_pchOverrideMap != pchOverrideMap)
	{
		s_pchOverrideMap = pchOverrideMap;
		g_bRebuildItemAssignmentTree = true;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsForceVolume);
void gslItemAssignments_ForceVolume(Entity* pEnt, const char* pchOverrideVolume)
{
	pchOverrideVolume = pchOverrideVolume && *pchOverrideVolume ? allocAddString(pchOverrideVolume) : NULL;
	if (s_pchOverrideVolume != pchOverrideVolume)
	{
		s_pchOverrideVolume = pchOverrideVolume;
		g_bRebuildItemAssignmentTree = true;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsForceAddAssignment);
void gslItemAssignment_ForceAddAssignment(CmdContext *pCmdContext, Entity* pEnt, ACMD_NAMELIST("ItemAssignmentDef", REFDICTIONARY) const char* pchForceAssignmment)
{
	ItemAssignmentDef *pDef = RefSystem_ReferentFromString(g_hItemAssignmentDict, pchForceAssignmment);
	if (pDef)
	{
		if (eaFind(&s_ppchForceAssignmentAdds, pDef->pchName) < 0)
		{
			eaPush(&s_ppchForceAssignmentAdds, pDef->pchName);
			g_bRebuildItemAssignmentTree = true;
		}
	}
	else if (pchForceAssignmment && *pchForceAssignmment)
	{
		FOR_EACH_IN_REFDICT(g_hItemAssignmentDict, ItemAssignmentDef, pAssignment);
		{
			const char *pchDisplayName = entTranslateDisplayMessage(pEnt, pAssignment->msgDisplayName);
			if (pchDisplayName && !stricmp(pchDisplayName, pchForceAssignmment))
			{
				if (eaFind(&s_ppchForceAssignmentAdds, pAssignment->pchName) < 0)
				{
					eaPush(&s_ppchForceAssignmentAdds, pAssignment->pchName);
					g_bRebuildItemAssignmentTree = true;
				}
				estrConcatf(pCmdContext->output_msg, "Adding %s", pAssignment->pchName);
			}
		}
		FOR_EACH_END;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsUnforceAddAssignments);
void gslItemAssignment_UnforceAddAssignments(Entity* pEnt)
{
	eaClear(&s_ppchForceAssignmentAdds);
	g_bRebuildItemAssignmentTree = true;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsResetCooldowns);
void gslItemAssignment_ResetCooldowns(Entity* pEnt)
{
	AutoTrans_gslItemAssignments_tr_ClearCompleteTimes(NULL, objServerType(), entGetType(pEnt), entGetContainerID(pEnt));
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsBypassFilters);
void gslItemAssignment_BypassFilters(Entity* pEnt, bool bUnfilterActive)
{
	ItemAssignmentPlayerData* pPlayerData = SAFE_MEMBER2(pEnt, pPlayer, pItemAssignmentData);

	if (!pPlayerData && SAFE_MEMBER(pEnt, pPlayer))
	{
		pPlayerData = ItemAssignmentPlayerData_Create();
		pEnt->pPlayer->pItemAssignmentData = pPlayerData;
	}

	if (pPlayerData)
	{
		pPlayerData->bUnfilterActive = true;
		pPlayerData->bUnfilterAllegiance = true;
		pPlayerData->bUnfilterMaximumLevel = true;
		pPlayerData->bUnfilterActive = bUnfilterActive;
		pPlayerData->bUnfilterNotRepeatable = true;
		pPlayerData->bUnfilterCooldown = true;
		pPlayerData->bUnfilterRequiredMission = true;
		pPlayerData->bUnfilterRequiredAssignment = true;
		pPlayerData->bUnfilterLocation = true;

		entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(ItemAssignmentsDumpRarityCounts);
const char *gslItemAssignment_DumpRarityCounts(Entity* pEnt)
{
	ItemAssignmentRefsInRarity **eaRefs = NULL;
	FILE *pFile = NULL;
	S32 i, j;

	if ((pFile = fopen("C:\\ItemAssignment_RarityCounts.csv", "w")) == NULL)
	{
		return "Error opening 'C:\\ItemAssignment_RarityCounts.csv'";
	}

	fprintf(pFile, "weight,assignment\n");

	gslItemAssignments_BuildRarityRefList(&eaRefs);

	for (i = 0; i < eaSize(&eaRefs); i++)
	{
		const char *pchWeightName = StaticDefineIntRevLookup(ItemAssignmentWeightTypeEnum, eaRefs[i]->eWeight);

		for (j = 0; j < eaSize(&eaRefs[i]->eaRefs); j++)
		{
			fprintf(pFile, "%s,%s\n", pchWeightName, REF_STRING_FROM_HANDLE(eaRefs[i]->eaRefs[j]->hDef));
		}
	}

	fclose(pFile);

	eaDestroyStruct(&eaRefs, parse_ItemAssignmentRefsInRarity);

	return "Saved to 'C:\\ItemAssignment_RarityCounts.csv'";
}

#include "AutoGen/gslItemAssignments_c_ast.c"
