/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#ifndef NO_EDITORS

#include "CharacterAttribs.h"
#include "encounter_common.h"
#include "entCritter.h"
#include "CostumeCommonLoad.h"
#include "GameServerLib.h"
#include "MessageReport.h"
#include "oldencounter_common.h"
#include "Player.h"
#include "powers.h"
#include "powertree.h"
#include "ResourceSearch.h"
#include "rewardCommon.h"
#include "storeCommon.h"
#include "textparserinheritance.h"
#include "ResourceInfo.h"
#include "inventoryCommon.h"

#include "AutoGen/entCritter_h_ast.h"
#include "AutoGen/powers_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ResourceSearch_h_ast.h"


static void SearchCritterParentsRecursive(ResourceSearchResult *pResult, CritterDef *pCritterDef, int count)
{
	char *pcParentName = StructInherit_GetParentName(parse_CritterDef, pCritterDef);
	if (pcParentName) {
		CritterDef *pParentDef = (CritterDef*)RefSystem_ReferentFromString(g_hCritterDefDict, pcParentName);
		if (pParentDef) {
			SearchCritterParentsRecursive(pResult, pParentDef, count + 1);
			if (count > 1) {
				char buf[260];
				sprintf(buf, "%s (%d)", SEARCH_RELATION_PARENT, count);
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pcParentName, SEARCH_TYPE_CRITTER, buf));
			} else {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pcParentName, SEARCH_TYPE_CRITTER, SEARCH_RELATION_PARENT));
			}
		}
	}
}


static void SearchCritterChildrenRecursive(ResourceSearchResult *pResult, CritterDef *pCritterDef, int count)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("CritterDef");
	int i, size;

	size = eaSize(&pStruct->ppReferents);
	for(i=0; i<size; ++i) {
		CritterDef *pDef = (CritterDef*)pStruct->ppReferents[i];
		char *pcParentName = StructInherit_GetParentName(parse_CritterDef, pDef);
		if (pcParentName && (stricmp(pcParentName,pCritterDef->pchName) == 0)) {
			if (count > 1) {
				char buf[260];
				sprintf(buf, "%s (%d)", SEARCH_RELATION_CHILD, count);
				eaPush(&pResult->eaRows, createResourceSearchResultRow((char*)pDef->pchName, SEARCH_TYPE_CRITTER, buf));
			} else {
				eaPush(&pResult->eaRows, createResourceSearchResultRow((char*)pDef->pchName, SEARCH_TYPE_CRITTER, SEARCH_RELATION_CHILD));
			}

			SearchCritterChildrenRecursive(pResult, pDef, count + 1);
		}
	}
}


void SearchCritters(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	DictionaryEArrayStruct *pEncounters = resDictGetEArrayStruct("EncounterDef");
	DictionaryEArrayStruct *pEncounterTemplates = resDictGetEArrayStruct("EncounterTemplate");
	DictionaryEArrayStruct *pPowers = resDictGetEArrayStruct("PowerDef");
	CritterDef *pSelf = NULL;
	int i, j;
	static const char **ppIgnoreDicts;
	if (!ppIgnoreDicts)
	{
		eaPush(&ppIgnoreDicts, resDictGetName("EncounterDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("EncounterTemplate"));
		eaPush(&ppIgnoreDicts, resDictGetName("PowerDef"));
	}

	// Search parents & children
	pSelf = RefSystem_ReferentFromString("CritterDef", pcName);
	if (pSelf) {
		SearchCritterParentsRecursive(pResult, pSelf, 1);
		SearchCritterChildrenRecursive(pResult, pSelf, 1);
	}

	// Tally encounter defs
	for(i=eaSize(&pEncounters->ppReferents)-1; i>=0; --i) {
		EncounterDef *pEnc = pEncounters->ppReferents[i];
		bool bFound = false;
		for(j=eaSize(&pEnc->actors)-1; j>=0 && !bFound; --j) {
			OldActorInfo *pActorInfo = pEnc->actors[j]->details.info;
			if (pActorInfo) {
				CritterDef *pCritter = GET_REF(pActorInfo->critterDef);
				if (pCritter && (stricmp(pCritter->pchName, pcName) == 0)) {
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pEnc->name, SEARCH_TYPE_ENCOUNTER, SEARCH_RELATION_ACTOR));
				}
			}
		}
	}

	// Tally encounter templates
	for(i=eaSize(&pEncounterTemplates->ppReferents)-1; i>=0; --i) {
		EncounterTemplate *pTemplate = pEncounterTemplates->ppReferents[i];
		bool bFound = false;
		for(j=eaSize(&pTemplate->eaActors)-1; j>=0 && !bFound; --j) {
			EncounterActorProperties *pActor = pTemplate->eaActors[j];
			if (pActor && (pActor->critterProps.eCritterType == ActorCritterType_CritterDef)) {
				CritterDef *pCritter = GET_REF(pActor->critterProps.hCritterDef);
				if (pCritter && (stricmp(pCritter->pchName, pcName) == 0)) {
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pTemplate->pcName, SEARCH_TYPE_ENCOUNTER_TEMPLATE, SEARCH_RELATION_ACTOR));
				}
			}
		}
	}

	// Can't search encounter layers since they are not all loaded

	// Search Powers
	for(i=eaSize(&pPowers->ppReferents)-1; i>=0; --i) {
		PowerDef *pPower = pPowers->ppReferents[i];
		for(j=eaSize(&pPower->ppMods)-1; j>=0; --j) {
			AttribModDef *pMod = pPower->ppMods[j];
			if(pMod->offAttrib==kAttribType_EntCreate)
			{
				EntCreateParams *pParams = (EntCreateParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hCritter) && stricmp(REF_STRING_FROM_HANDLE(pParams->hCritter), pcName) == 0) {
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
				}
			}
		}
	}

	SearchUsageGeneric(pRequest,pResult, ppIgnoreDicts);
}


void SearchItems(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	DictionaryEArrayStruct *pRewards = resDictGetEArrayStruct("RewardTable");
	DictionaryEArrayStruct *pStores = resDictGetEArrayStruct("Store");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	int i, j;
	static const char **ppIgnoreDicts;
	if (!ppIgnoreDicts)
	{
		eaPush(&ppIgnoreDicts, resDictGetName("ItemDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("RewardTable"));
		eaPush(&ppIgnoreDicts, resDictGetName("Store"));
		eaPush(&ppIgnoreDicts, resDictGetName("CritterDef"));
	}

	// Search items
	for(i=eaSize(&pItems->ppReferents)-1; i>=0; --i) {
		ItemDef *pItem = pItems->ppReferents[i];
		ItemDef *pRecipe = GET_REF(pItem->hCraftRecipe);
		if (pRecipe) {
			if (stricmp(pRecipe->pchName, pcName) == 0) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pItem->pchName, SEARCH_TYPE_ITEM, SEARCH_RELATION_RECIPE_RESULT));
			} else if (strcmp(pItem->pchName, pcName) == 0) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pRecipe->pchName, SEARCH_TYPE_ITEM, SEARCH_RELATION_RECIPE));
			}
		}
		if (pItem->pCraft) {
			for(j=eaSize(&pItem->pCraft->ppPart)-1; j>=0; --j) {
				ItemDef *pComponent = GET_REF(pItem->pCraft->ppPart[j]->hItem);
				if (pComponent && (stricmp(pComponent->pchName, pcName) == 0)) {
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pItem->pchName, SEARCH_TYPE_ITEM, SEARCH_RELATION_COMPONENT));
					break;
				}
			}
		}
	}

	// Search Reward Tables
	for(i=eaSize(&pRewards->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewards->ppReferents[i];
		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			ItemDef *pItem = GET_REF(pEntry->hItemDef);
			if (pItem && (stricmp(pItem->pchName, pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pReward->pchName, SEARCH_TYPE_REWARD_TABLE, SEARCH_RELATION_REWARD_ITEM));
				break;
			}
		}
	}

	// Search Stores
	for(i=eaSize(&pStores->ppReferents)-1; i>=0; --i) {
		StoreDef *pStore = pStores->ppReferents[i];
		for(j=eaSize(&pStore->inventory)-1; j>=0; --j) {
			StoreItemDef *pEntry = pStore->inventory[j];
			ItemDef *pItem = GET_REF(pEntry->hItem);
			if (pItem && (stricmp(pItem->pchName, pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pStore->name, SEARCH_TYPE_STORE, SEARCH_RELATION_USES));
				break;
			}
		}
	}

	// Search Critters
	for(i=eaSize(&pCritters->ppReferents)-1;i>=0;--i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		for(j=eaSize(&pCritter->ppCritterItems)-1;j>=0;--j) {
			DefaultItemDef *pItemDef = pCritter->ppCritterItems[j];
			ItemDef *pItem = GET_REF(pItemDef->hItem);
			if(pItem && (stricmp(pItem->pchName, pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pCritter->pchName, SEARCH_TYPE_CRITTER, SEARCH_RELATION_USES));
			}
		}
	}

	SearchUsageGeneric(pRequest,pResult, ppIgnoreDicts);
}


void SearchItemPowers(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	DictionaryEArrayStruct *pItems = resDictGetEArrayStruct("ItemDef");
	DictionaryEArrayStruct *pRewards = resDictGetEArrayStruct("RewardTable");
	int i, j;
	static const char **ppIgnoreDicts;
	if (!ppIgnoreDicts)
	{
		eaPush(&ppIgnoreDicts, resDictGetName("ItemDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("RewardTable"));
	}


	// Search items
	for(i=eaSize(&pItems->ppReferents)-1; i>=0; --i) {
		ItemDef *pItem = pItems->ppReferents[i];
		for(j=eaSize(&pItem->ppItemPowerDefRefs)-1; j>=0; --j) {
			ItemPowerDef *pItemPower = GET_REF(pItem->ppItemPowerDefRefs[j]->hItemPowerDef);
			if (pItemPower && (stricmp(pItemPower->pchName, pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pItem->pchName, SEARCH_TYPE_ITEM, SEARCH_RELATION_ITEM_POWER));
			}
		}
	}

	// Search Reward Tables
	for(i=eaSize(&pRewards->ppReferents)-1; i>=0; --i) {
		RewardTable *pReward = pRewards->ppReferents[i];
		for(j=eaSize(&pReward->ppRewardEntry)-1; j>=0; --j) {
			RewardEntry *pEntry = pReward->ppRewardEntry[j];
			ItemPowerDef *pItemPower = GET_REF(pEntry->hItemPowerDef);
			if (pItemPower && (stricmp(pItemPower->pchName, pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pReward->pchName, SEARCH_TYPE_REWARD_TABLE, SEARCH_RELATION_REWARD_ITEMPOWER));
			}
		}
	}

	SearchUsageGeneric(pRequest,pResult, ppIgnoreDicts);
}


static void SearchPowerParentsRecursive(ResourceSearchResult *pResult, PowerDef *pPowerDef, int count)
{
	char *pcParentName = StructInherit_GetParentName(parse_PowerDef, pPowerDef);
	if (pcParentName) {
		PowerDef *pParentDef = (PowerDef*)RefSystem_ReferentFromString(g_hPowerDefDict, pcParentName);
		if (pParentDef) {
			SearchPowerParentsRecursive(pResult, pParentDef, count + 1);
			if (count > 1) {
				char buf[260];
				sprintf(buf, "%s (%d)", SEARCH_RELATION_PARENT, count);
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pcParentName, SEARCH_TYPE_POWER, buf));
			} else {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pcParentName, SEARCH_TYPE_POWER, SEARCH_RELATION_PARENT));
			}
		}
	}
}

static void SearchPowerChildrenRecursive(ResourceSearchResult *pResult, PowerDef *pPowerDef, int count)
{
	DictionaryEArrayStruct *pStruct = resDictGetEArrayStruct("PowerDef");
	int i, size;

	size = eaSize(&pStruct->ppReferents);
	for(i=0; i<size; ++i) {
		PowerDef *pDef = (PowerDef*)pStruct->ppReferents[i];
		char *pcParentName = StructInherit_GetParentName(parse_PowerDef, pDef);
		if (pcParentName && (stricmp(pcParentName,pPowerDef->pchName) == 0)) {
			if (count > 1) {
				char buf[260];
				sprintf(buf, "%s (%d)", SEARCH_RELATION_CHILD, count);
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pDef->pchName, SEARCH_TYPE_POWER, buf));
			} else {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pDef->pchName, SEARCH_TYPE_POWER, SEARCH_RELATION_CHILD));
			}

			SearchPowerChildrenRecursive(pResult, pDef, count + 1);
		}
	}
}

void SearchPowers(ResourceSearchRequest *pRequest, ResourceSearchResult *pResult)
{
	const char *pcType = pRequest->pcType;
	const char *pcName = pRequest->pcName;
	DictionaryEArrayStruct *pPowers = resDictGetEArrayStruct("PowerDef");
	DictionaryEArrayStruct *pCritters = resDictGetEArrayStruct("CritterDef");
	DictionaryEArrayStruct *pItemPowers = resDictGetEArrayStruct("ItemPowerDef");
	DictionaryEArrayStruct *pTrees = resDictGetEArrayStruct("PowerTreeDef");
	PowerDef *pSelf;
	int i, j, k, n;
	static const char **ppIgnoreDicts;
	if (!ppIgnoreDicts)
	{
		eaPush(&ppIgnoreDicts, resDictGetName("PowerDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("CritterDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("ItemPowerDef"));
		eaPush(&ppIgnoreDicts, resDictGetName("PowerTreeDef"));
	}

	// Search parents & children
	pSelf = RefSystem_ReferentFromString("PowerDef", pcName);
	if (pSelf) {
		SearchPowerParentsRecursive(pResult, pSelf, 1);
		SearchPowerChildrenRecursive(pResult, pSelf, 1);
	}

	// Search powers
	for(i=eaSize(&pPowers->ppReferents)-1; i>=0; --i) {
		PowerDef *pPower = pPowers->ppReferents[i];
		for(j=eaSize(&pPower->ppCombos)-1; j>=0; --j) {
			if (REF_STRING_FROM_HANDLE(pPower->ppCombos[j]->hPower) && stricmp(REF_STRING_FROM_HANDLE(pPower->ppCombos[j]->hPower), pcName) == 0) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, SEARCH_RELATION_COMBO));
				break;
			}
		}
		for(j=eaSize(&pPower->ppMods)-1; j>=0; --j) {
			AttribModDef *pMod = pPower->ppMods[j];
			if(pMod->offAttrib==kAttribType_ApplyPower)
			{
				ApplyPowerParams *pParams = (ApplyPowerParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_DamageTrigger)
			{
				DamageTriggerParams *pParams = (DamageTriggerParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_GrantPower)
			{
				GrantPowerParams *pParams = (GrantPowerParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_KillTrigger)
			{
				KillTriggerParams *pParams = (KillTriggerParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_RemovePower)
			{
				RemovePowerParams *pParams = (RemovePowerParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_TeleThrow)
			{
				TeleThrowParams *pParams = (TeleThrowParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDefFallback) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDefFallback), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_TriggerComplex)
			{
				TriggerComplexParams *pParams = (TriggerComplexParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}
			else if(pMod->offAttrib==kAttribType_TriggerSimple)
			{
				TriggerSimpleParams *pParams = (TriggerSimpleParams*)pMod->pParams;
				if(pParams && REF_STRING_FROM_HANDLE(pParams->hDef) && stricmp(REF_STRING_FROM_HANDLE(pParams->hDef), pcName) == 0)
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, StaticDefineIntRevLookup(AttribTypeEnum,pMod->offAttrib)));
			}

			if(pMod->pExpiration)
			{
				if(stricmp(REF_STRING_FROM_HANDLE(pMod->pExpiration->hDef), pcName) == 0) {
					eaPush(&pResult->eaRows, createResourceSearchResultRow(pPower->pchName, SEARCH_TYPE_POWER, SEARCH_RELATION_MODEXPIRATION));
				}
			}
		}
	}

	// Search critters
	for(i=eaSize(&pCritters->ppReferents)-1; i>=0; --i) {
		CritterDef *pCritter = pCritters->ppReferents[i];
		for(j=eaSize(&pCritter->ppPowerConfigs)-1; j>=0; --j) {
			CritterPowerConfig *pConfig = pCritter->ppPowerConfigs[j];
			if (REF_STRING_FROM_HANDLE(pConfig->hPower) && (stricmp(REF_STRING_FROM_HANDLE(pConfig->hPower), pcName) == 0)) {
				eaPush(&pResult->eaRows, createResourceSearchResultRow(pCritter->pchName, SEARCH_TYPE_CRITTER, SEARCH_RELATION_CRITTER_POWER));
				break;
			}
		}
	}

	// Search item powers
	for(i=eaSize(&pItemPowers->ppReferents)-1; i>=0; --i) {
		ItemPowerDef *pItemPower = pItemPowers->ppReferents[i];
		const char *pcPowerName = REF_STRING_FROM_HANDLE(pItemPower->hPower);
		if (pcPowerName && (stricmp(pcPowerName, pcName) == 0)) {
			eaPush(&pResult->eaRows, createResourceSearchResultRow(pItemPower->pchName, SEARCH_TYPE_ITEMPOWER, SEARCH_RELATION_ITEM_POWER));
		}
	}

	// Search power trees
	for(i=eaSize(&pTrees->ppReferents)-1; i>=0; --i) {
		PowerTreeDef *pTree = pTrees->ppReferents[i];
		bool bFound = false;
		for(j=eaSize(&pTree->ppGroups)-1; j>=0 && !bFound; --j) {
			PTGroupDef *pGroup = pTree->ppGroups[j];
			for(k=eaSize(&pGroup->ppNodes)-1; k>=0 && !bFound; --k) {
				PTNodeDef *pNode = pGroup->ppNodes[k];
				for(n=eaSize(&pNode->ppEnhancements)-1; n>=0 && !bFound; --n) {
					const char *pcPowerName = REF_STRING_FROM_HANDLE(pNode->ppEnhancements[n]->hPowerDef);
					if (pcPowerName && (stricmp(pcPowerName, pcName) == 0)) {
						eaPush(&pResult->eaRows, createResourceSearchResultRow(pTree->pchName, SEARCH_TYPE_POWER_TREE, SEARCH_RELATION_USES));
						bFound = true;
					}
				}
				for(n=eaSize(&pNode->ppRanks)-1; n>=0 && !bFound; --n) {
					const char *pcPowerName = REF_STRING_FROM_HANDLE(pNode->ppRanks[n]->hPowerDef);
					if (pcPowerName && (stricmp(pcPowerName, pcName) == 0)) {
						eaPush(&pResult->eaRows, createResourceSearchResultRow(pTree->pchName, SEARCH_TYPE_POWER_TREE, SEARCH_RELATION_USES));
						bFound = true;
					}
				}
			}

		}
	}

	// Can also be on a volume on a layer, but not all layers are available

	SearchUsageGeneric(pRequest,pResult, ppIgnoreDicts);
}

AUTO_RUN;
void gslRegisterCustomResourceSearch(void)
{
	registerCustomResourceSearchHandler(SEARCH_MODE_USAGE, SEARCH_TYPE_CRITTER, SearchCritters);
	registerCustomResourceSearchHandler(SEARCH_MODE_USAGE, SEARCH_TYPE_ITEM, SearchItems);
	registerCustomResourceSearchHandler(SEARCH_MODE_USAGE, SEARCH_TYPE_POWER, SearchPowers);
	registerCustomResourceSearchHandler(SEARCH_MODE_USAGE, SEARCH_TYPE_ITEMPOWER, SearchItemPowers);
}

AUTO_COMMAND ACMD_SERVERCMD;
void gslSearchResources(Entity *pEntity, ResourceSearchRequest *pRequest)
{
	if (pRequest) {

		ResourceSearchResult *pResult = handleResourceSearchRequest(pRequest);
		if (pResult)
		{
			ClientCmd_SendSearchResourcesResult(pEntity, pResult);
			StructDestroy(parse_ResourceSearchResult, pResult);
		}

	}
}

AUTO_COMMAND ACMD_NAME(FindMessageUsages) ACMD_SERVERCMD;
void cmdMsgFindUsages(Entity *pEntity, const char *pchPrefix) {
	ResourceSearchResult *pResult = StructAlloc(parse_ResourceSearchResult);
	ResourceSearchRequest *pRequest = StructAlloc(parse_ResourceSearchRequest);
	S32 i;
	S32 iRequest=0;

	pRequest->eSearchMode = SEARCH_MODE_USAGE;

	FOR_EACH_IN_REFDICT(gMessageDict, Message, pMsg)
	{
		if (pMsg) {
			if (strStartsWith(pMsg->pcMessageKey, pchPrefix)) {
				ResourceSearchResult *pLocalResult;

				printf("%d\r", iRequest);
				pRequest->iRequest = iRequest++;
				pRequest->pcName = (char*) pMsg->pcMessageKey; // It's ok to override const-ness here because we're just going to invoke a command
				pRequest->pcType = "Message";
				pRequest->pcSearchDetails = NULL;

				pLocalResult = handleResourceSearchRequest(pRequest);
				if (pLocalResult) {
					for (i=0; i < eaSize(&pLocalResult->eaRows); i++) {
						ResourceSearchResultRow *pRow = StructClone(parse_ResourceSearchResultRow, pLocalResult->eaRows[i]);
						StructFreeStringSafe(&pRow->pcExtraData);
						pRow->pcExtraData = StructAllocString(pRequest->pcName);
						eaPush(&pResult->eaRows, pRow);
					}
				}
				StructDestroy(parse_ResourceSearchResult, pLocalResult);
			}
		}
	}
	FOR_EACH_END;

	printf("Searched usages of %d keys\n", iRequest);
	msgReportResourcesResult(pResult);
	StructDestroy(parse_ResourceSearchResult, pResult);
}
#endif