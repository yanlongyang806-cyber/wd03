/***************************************************************************
*     Copyright (c) 2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Character.h"
#include "contact_common.h"
#include "Entity.h"
#include "EntityLib.h"
#include "entCritter.h"
#include "EString.h"
#include "Expression.h"
#include "GameAccountDataCommon.h"
#include "GameStringFormat.h"
#include "gslContact.h"
#include "LoggedTransactions.h"
#include "gslPowerStore.h"
#include "inventoryCommon.h"
#include "itemCommon.h"
#include "mission_common.h"
#include "OfficerCommon.h"
#include "Player.h"
#include "Powers.h"
#include "PowerTree.h"
#include "PowerTreeHelpers.h"
#include "PowerTreeTransactions.h"
#include "powerStoreCommon.h"
#include "SavedPetCommon.h"
#include "StringCache.h"
#include "StringFormat.h"
#include "AlgoPet.h"
#include "gslActivityLog.h"
#include "tradeCommon.h"

#include "AutoGen/PowerTree_h_ast.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

#define POWER_STORE_REQUIREMENTS_NOT_MET_MSG "Store.RequirementsNotMet"

static char s_pcVarPlayer[] = "Player";
static int s_hVarPlayer = 0;

static bool powerstore_CanBuyPower(Entity *pPlayerEnt, PowerStoreDef *pStoreDef, PowerStorePowerDef *pStorePower, char **estrDisplayText)
{
	PowerTreeDef *pTree = GET_REF(pStorePower->hTree);
	PTNodeDef *pNode = GET_REF(pStorePower->hNode);

	if (!pTree || !pNode){
		return false;
	}

	if (estrDisplayText) {
		estrClear(estrDisplayText);
	}

	// If the power has a CanBuy expression, run it
	if (pStorePower->pExprCanBuy) {
		MultiVal mvReturn = {0};

		exprContextSetSelfPtr(g_pPowerStoreContext, pPlayerEnt);
		exprContextSetSilentErrors(g_pPowerStoreContext, false);
		exprContextSetPointerVarPooledCached(g_pPowerStoreContext, allocAddString(s_pcVarPlayer), pPlayerEnt, parse_Entity, true, true, &s_hVarPlayer);
		exprEvaluate(pStorePower->pExprCanBuy, g_pPowerStoreContext, &mvReturn);

		if (!MultiValGetInt(&mvReturn, NULL)) {
			if (estrDisplayText) {
				entFormatGameDisplayMessage(pPlayerEnt, estrDisplayText, &pStorePower->cantBuyMessage, STRFMT_END);
			}
			return false;
		}
	}

	return true;
}

void powerstore_GetStorePowerInfo(Entity *pPlayerEnt, PowerStoreDef *pStoreDef, PowerStorePowerInfo ***peaPowerInfo)
{
	int i;

	if (pPlayerEnt && pStoreDef && peaPowerInfo) {

		for (i = 0; i < eaSize(&pStoreDef->eaInventory); i++) {

			PowerTreeDef *pTree = GET_REF(pStoreDef->eaInventory[i]->hTree);
			PTNodeDef *pNodeDef = GET_REF(pStoreDef->eaInventory[i]->hNode);
			ItemDef *pCurrencyDef = GET_REF(pStoreDef->hCurrency);
			AllegianceDef* pAllegiance = GET_REF(pPlayerEnt->hAllegiance);
			AllegianceDef* pSubAllegiance = GET_REF(pPlayerEnt->hSubAllegiance);
			AllegianceDef* pPreferredAllegiance = allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance);
			PowerStorePowerInfo *pPowerInfo;
			PowerStoreCostInfo *pCostInfo;

			if (!pTree || !pCurrencyDef) {
				continue;
			}

			pPowerInfo = StructCreate(parse_PowerStorePowerInfo);
			COPY_HANDLE(pPowerInfo->hTree, pStoreDef->eaInventory[i]->hTree);
			COPY_HANDLE(pPowerInfo->hNode, pStoreDef->eaInventory[i]->hNode);
			pPowerInfo->pcStoreName = pStoreDef->pcName;
			pPowerInfo->iIndex = i;
			pPowerInfo->iNodeRank = MAX(pStoreDef->eaInventory[i]->iNodeRank-1, 0); // convert to 0-based rank

			pCostInfo = StructCreate(parse_PowerStoreCostInfo);
			if (pStoreDef->eaInventory[i]->iValue)
			{
				pCostInfo->iCount = pStoreDef->eaInventory[i]->iValue;
			}
			else if (pAllegiance && pPreferredAllegiance && Officer_GetRankCount(pPreferredAllegiance))
			{
				OfficerRankDef* pRankDef = Officer_GetRankDefFromNode(pNodeDef, pAllegiance, pSubAllegiance);
				if (pRankDef)
				{
					pCostInfo->iCount = pRankDef->iTrainingCost;
				}
			}
			COPY_HANDLE(pCostInfo->hCurrency, pStoreDef->hCurrency);

			if (pCostInfo->iCount > inv_GetNumericItemValue(pPlayerEnt,pCurrencyDef->pchName)) {
				pCostInfo->bTooExpensive = true;
			}
			eaPush(&pPowerInfo->eaCostInfo, pCostInfo);

			if (!powerstore_CanBuyPower(pPlayerEnt, pStoreDef, pStoreDef->eaInventory[i], &pPowerInfo->pcRequirementsText)){
				pPowerInfo->bFailsRequirements = true;
			}

			eaPush(peaPowerInfo, pPowerInfo);
		}
	}
}

void powerstore_RefreshStorePowerInfo(Entity *pPlayerEnt, PowerStorePowerInfo ***peaPowerInfo)
{
	int i, j;
	for (i = 0; i < eaSize(peaPowerInfo); i++)
	{
		PowerStorePowerInfo *pPowerInfo = (*peaPowerInfo)[i];
		PowerStoreDef *pStoreDef = NULL;
		PowerStorePowerDef *pStorePower = NULL;

		if (pPowerInfo->eTrainerType == kPowerStoreTrainerType_FromStore)
		{
			pStoreDef = RefSystem_ReferentFromString(g_PowerStoreDictionary, pPowerInfo->pcStoreName);
			pStorePower = pStoreDef ? eaGet(&pStoreDef->eaInventory, pPowerInfo->iIndex) : NULL;
		}

		for (j = 0; j < eaSize(&pPowerInfo->eaCostInfo); j++){
			PowerStoreCostInfo *pCostInfo = pPowerInfo->eaCostInfo[j];
			ItemDef *pCurrencyDef = pCostInfo?GET_REF(pCostInfo->hCurrency):NULL;
			int iHave = 0;

			if (pCurrencyDef){
				if (pCurrencyDef->eType == kItemType_Numeric) {
					iHave = inv_GetNumericItemValue(pPlayerEnt, pCurrencyDef->pchName);
				} else {
					iHave = inv_ent_AllBagsCountItems(pPlayerEnt, pCurrencyDef->pchName);
				}
			}

			if (pCostInfo){
				if (pCostInfo->iCount > iHave) {
					pCostInfo->bTooExpensive = true;
				} else {
					pCostInfo->bTooExpensive = false;
				}
			}
		}

		if (pPowerInfo->eTrainerType == kPowerStoreTrainerType_FromStore &&
			(!pStorePower || !powerstore_CanBuyPower(pPlayerEnt, pStoreDef, pStorePower, &pPowerInfo->pcRequirementsText)))
		{
			pPowerInfo->bFailsRequirements = true;
		} 
		else 
		{
			pPowerInfo->bFailsRequirements = false;
		}
	}
}

static PowerStorePowerInfo* powerstore_CreatePowerStoreInfo(const char* pchStoreName, S32 i, 
															PowerTreeDef* pTreeDef, PTNodeDef *pNodeDef, S32 iRank, 
															const char* pchCostNumeric, S32 iCost,
															PowerStoreTrainerType eTrainerType)
{
	if (pTreeDef && pNodeDef)
	{
		PowerStorePowerInfo *pPowerInfo = StructCreate(parse_PowerStorePowerInfo);
		PowerStoreCostInfo *pCostInfo;

		SET_HANDLE_FROM_REFERENT(g_hPowerTreeDefDict, pTreeDef, pPowerInfo->hTree);
		SET_HANDLE_FROM_REFERENT(g_hPowerTreeNodeDefDict, pNodeDef, pPowerInfo->hNode);
		pPowerInfo->pcStoreName = pchStoreName;
		pPowerInfo->iIndex = i;
		pPowerInfo->iNodeRank = iRank;

		pCostInfo = StructCreate(parse_PowerStoreCostInfo);
		
		pCostInfo->iCount = iCost;
		SET_HANDLE_FROM_STRING(g_hItemDict, pchCostNumeric, pCostInfo->hCurrency);

		eaPush(&pPowerInfo->eaCostInfo, pCostInfo);

		pPowerInfo->bFailsRequirements = false;
		pPowerInfo->eTrainerType = eTrainerType;

		return pPowerInfo;
	}
	return NULL;
}

void powerstore_GetTrainerPowerInfoFromEntity(Entity *pEntSrc, Entity *pTrainer, PowerStorePowerInfo ***peaPowerInfo)
{
	AllegianceDef* pAllegiance = pEntSrc ? GET_REF(pEntSrc->hAllegiance) : NULL;
	AllegianceDef* pSubAllegiance = pEntSrc ? GET_REF(pEntSrc->hSubAllegiance) : NULL;
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEntSrc, pPlayer, pInteractInfo, pContactDialog);

	if (pContactDialog && pAllegiance && pTrainer && peaPowerInfo)
	{
		int i, iCount = 0;
		PTNodeDef **ppNodes = NULL;

		powertree_CharacterGetTrainerUnlockNodes(pEntSrc->pChar, &ppNodes);

		for (i = 0; i < eaSize(&ppNodes); i++) 
		{
			PowerStorePowerInfo* pInfo = NULL;
			PTNodeDef* pNodeDef = ppNodes[i];
			PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
			PowerStoreTrainerType eTrainerType = kPowerStoreTrainerType_FromEntity;
			const char* pchCostNumeric = NULL;
			S32 iCost = 0;

			if (pContactDialog->bIsOfficerTrainer)
			{
				OfficerRankDef* pRankDef = Officer_GetRankDefFromNode(pNodeDef,pAllegiance,pSubAllegiance);
				if (pRankDef)
				{
					pchCostNumeric = pRankDef->pchTrainingNumeric;
					iCost = pRankDef->iTrainingCost;
				}
			}
			pInfo = powerstore_CreatePowerStoreInfo(NULL,iCount,pTreeDef,pNodeDef,0,pchCostNumeric,iCost,eTrainerType);
			if (pInfo)
			{
				eaPush(peaPowerInfo, pInfo);
				iCount++;
			}
		}

		eaDestroy(&ppNodes);
	}
}

void powerstore_GetStorePowerInfoFromItem(Entity *pEntSrc, S32 iBagID, S32 iSlot, ItemDef* pItemDef, PowerStorePowerInfo ***peaPowerInfo, GameAccountDataExtract *pExtract)
{
	Item* pSrcItem = inv_GetItemFromBag(pEntSrc, iBagID, iSlot, pExtract);

	if (pEntSrc && pItemDef && pSrcItem && GET_REF(pSrcItem->hItem) == pItemDef && peaPowerInfo) 
	{
		int i, iCount = 0;
		PowerStoreTrainerType eTrainerType = kPowerStoreTrainerType_FromItem;
		PTNodeDefRef **ppEscrowNodes = NULL;

		if (pSrcItem->pSpecialProps && pSrcItem->pSpecialProps->pAlgoPet)
		{
			ppEscrowNodes = (PTNodeDefRef**)pSrcItem->pSpecialProps->pAlgoPet->ppEscrowNodes;
		}
		else
		{
			PetDef* pPetDef = GET_REF(pItemDef->hPetDef);
			if (pPetDef)
			{
				ppEscrowNodes = pPetDef->ppEscrowPowers;
			}
		}

		for (i = 0; i < eaSize(&ppEscrowNodes); i++) 
		{
			PTNodeDef *pNodeDef = GET_REF(ppEscrowNodes[i]->hNodeDef);
			PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
			PowerStorePowerInfo* pInfo = powerstore_CreatePowerStoreInfo(NULL,iCount,pTreeDef,pNodeDef,0,NULL,0,eTrainerType);

			if (pInfo)
			{
				eaPush(peaPowerInfo, pInfo);
				iCount++;
			}
		}
		for (i = 0; i < eaSize(&pItemDef->ppTrainableNodes); i++)
		{
			PTNodeDef *pNodeDef = GET_REF(pItemDef->ppTrainableNodes[i]->hNodeDef);
			PowerTreeDef* pTreeDef = powertree_TreeDefFromNodeDef(pNodeDef);
			S32 iRank = MAX(pItemDef->ppTrainableNodes[i]->iNodeRank-1, 0); // convert to 0-based rank
			PowerStorePowerInfo* pInfo = powerstore_CreatePowerStoreInfo(NULL,iCount,pTreeDef,pNodeDef,iRank,NULL,0,eTrainerType);

			if (pInfo)
			{
				eaPush(peaPowerInfo, pInfo);
				iCount++;
			}
		}
	}
}

typedef struct TrainEntCBData
{
	EntityRef erRef;
	ContainerID petID;
	const char *pchNewNode;
	PowerStoreTrainerType eTrainerType;
} TrainEntCBData;

static TrainEntCBData* TrainEnt_CreateCBData(Entity* pEnt, Entity *petEnt, const char *pchNewNode, PowerStoreTrainerType eTrainerType)
{
	TrainEntCBData *cbData = calloc(sizeof(TrainEntCBData), 1);

	cbData->erRef = entGetRef(pEnt);
	cbData->pchNewNode = strdup(pchNewNode);
	cbData->eTrainerType = eTrainerType;
	if (petEnt && entGetType(petEnt) == GLOBALTYPE_ENTITYSAVEDPET)
	{
		cbData->petID = entGetContainerID(petEnt);
	}
	return cbData;
}

static void TrainEnt_CB(TrainEntCBData* pData)
{
	Entity* pEnt = entFromEntityRefAnyPartition(pData->erRef);

	if (pData->eTrainerType != kPowerStoreTrainerType_FromStore)
	{
		contact_InteractEnd(pEnt, false);
	}
	if (pEnt != NULL && pData->petID)
	{
		Entity* petEnt = entity_GetSubEntity(entGetPartitionIdx(pEnt), pEnt, GLOBALTYPE_ENTITYSAVEDPET, pData->petID);
		if (petEnt != NULL)
		{
			gslActivity_AddPetTrainEntry(pEnt, petEnt, pData->pchNewNode);
		}
	}
	if (pData->pchNewNode != NULL)
	{
		free((void *)pData->pchNewNode);
	}
	if (pData != NULL)
	{
		free(pData);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_CATEGORY(Standard, Inventory) ACMD_PRIVATE;
void powerstore_TrainPet(Entity* pEnt, U32 uiContainerID, const char* pchNode, U32 uStorePowerIndex)
{
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	Entity* pPetEnt = entity_GetSubEntity(iPartitionIdx, pEnt, GLOBALTYPE_ENTITYSAVEDPET, uiContainerID);
	ContactDialog* pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	ContactDef* pContactDef = pContactDialog?GET_REF(pContactDialog->hContactDef):NULL;
	Entity* pEntSrc = pContactDialog ? entity_GetSubEntity(iPartitionIdx, pEnt, pContactDialog->iEntType, pContactDialog->iEntID) : NULL;
	PowerStorePowerInfo* pPowerInfo = pContactDialog ? eaGet(&pContactDialog->eaStorePowers, uStorePowerIndex) : NULL;
	PTNodeDef* pNewNodeDef = NULL;
	PowerTreeDef* pNewTreeDef = NULL;
	Item* pItem = NULL;
	ItemDef* pItemDef = NULL;
	PetDef* pPetDef = NULL;
	AllegianceDef *pAllegiance = NULL, *pPetAllegiance = NULL, *pSubAllegiance = NULL, *pPreferredAllegiance = NULL;
	const char* pchCostNumeric = NULL;
	S32 iCost = 0;
	S32 iNewNodeRank = 0;
	PowerStoreTrainerType eTrainerType = kPowerStoreTrainerType_FromStore;

	if (!pEnt || !pEnt->pPlayer || !pPetEnt || !pPetEnt->pChar || !pPetEnt->pCritter || !pPowerInfo)
	{
		return;
	}

	// If the pet is currently being traded, then it is not allowed to be trained
	if(trade_IsPetBeingTraded(pPetEnt, pEnt))
	{
		return;
	}
	if (pPowerInfo->eTrainerType == kPowerStoreTrainerType_FromItem)
	{
		Entity* pItemEnt = pEntSrc ? pEntSrc : pEnt;
		GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEnt);
		pItem = inv_GetItemFromBag(pItemEnt, pContactDialog->iItemBagID, pContactDialog->iItemSlot, pExtract);

		if (!pItem || pItem->id != pContactDialog->iItemID)
		{
			return;
		}
		pItemDef = GET_REF(pItem->hItem);
		pPetDef = pItemDef ? GET_REF(pItemDef->hPetDef) : NULL;
		if (!pPetDef && pContactDialog->bIsOfficerTrainer)
		{
			return;
		}
	}

	pAllegiance = GET_REF(pEnt->hAllegiance);
	if (pPetDef)
	{
		pPetAllegiance = GET_REF(pPetDef->hAllegiance);
		if (pAllegiance && pPetAllegiance && pAllegiance != pPetAllegiance)
		{
			pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
			if (!pSubAllegiance || pSubAllegiance != pPetAllegiance)
				return;
		}
	}

	if (pPowerInfo)
	{
		pNewTreeDef = GET_REF(pPowerInfo->hTree);
		pNewNodeDef = GET_REF(pPowerInfo->hNode);
		iNewNodeRank = pPowerInfo->iNodeRank;
		eTrainerType = pPowerInfo->eTrainerType;
	}
	if (pContactDef && pPowerInfo && pPowerInfo->eTrainerType == kPowerStoreTrainerType_FromStore)
	{
		S32 i;
		ItemDef *pCurrencyDef = NULL;
		PowerStoreDef* pPowerStoreDef = NULL;
		PowerStorePowerDef* pPowerStorePowerDef = NULL;
		
		// Find the power in the store's inventory
		for (i = 0; i < eaSize(&pContactDef->powerStores); i++) {
			PowerStoreDef *pCurrentStore = GET_REF(pContactDef->powerStores[i]->ref);
			if (!stricmp(pCurrentStore->pcName, pPowerInfo->pcStoreName)){
				pPowerStoreDef = pCurrentStore;
				pPowerStorePowerDef = eaGet(&pPowerStoreDef->eaInventory, uStorePowerIndex);
			}
		}

		if(!pPowerStoreDef || !pPowerStorePowerDef)
			return;

		if (!powerstore_CanBuyPower(pEnt, pPowerStoreDef, pPowerStorePowerDef, NULL))
			return;

		pCurrencyDef = GET_REF(pPowerStoreDef->hCurrency);
		if (pCurrencyDef) 
		{
			for (i = eaSize(&pPowerInfo->eaCostInfo)-1; i >= 0; i--)
			{
				if (GET_REF(pPowerInfo->eaCostInfo[i]->hCurrency) == pCurrencyDef)
				{
					iCost = pPowerInfo->eaCostInfo[i]->iCount;
					pchCostNumeric = pCurrencyDef->pchName;
					break;
				}
			}
		}
	}
	else 
	{
		PowerStoreCostInfo *pCostInfo = eaGet(&pPowerInfo->eaCostInfo,0);
		if (pCostInfo)
		{
			iCost = pCostInfo->iCount;
			pchCostNumeric = REF_STRING_FROM_HANDLE(pCostInfo->hCurrency);
		}
	}

	if (!pNewNodeDef || !pNewTreeDef)
	{
		return;
	}
	else
	{
		F32 fRefundPercent = 0.0f;
		U32 uiStartTime = timeSecondsSince2000();
		U32 uiEndTime = 0;
		U64 uiRemoveItemID = 0;
		CharacterTrainingType eTrainType = CharacterTrainingType_Give;
		bool bAlwaysDestroyItem = false;
		bool bDestroyItem = false;

		if (pContactDialog->bIsOfficerTrainer)
		{
			OfficerRankDef* pRankDef;
			pAllegiance = GET_REF(pEnt->hAllegiance);
			pSubAllegiance = GET_REF(pEnt->hSubAllegiance);
			pPreferredAllegiance = allegiance_GetOfficerPreference(pAllegiance, pSubAllegiance);

			if (!Officer_CanTrain(iPartitionIdx, pEnt, pEnt, uiContainerID, pchNode, pNewNodeDef->pchNameFull, false, false, pPreferredAllegiance))
			{
				return;
			}

			pRankDef = Officer_GetRankDefFromNode(pNewNodeDef, pAllegiance, pSubAllegiance);

			if (!pRankDef)
			{
				return;
			}

			fRefundPercent = pRankDef->fTrainingRefundPercent;
			uiEndTime = uiStartTime + pRankDef->uiTrainingTime;	
			eTrainType = CharacterTrainingType_ReplaceEscrow;
			bAlwaysDestroyItem = true;
		}
		else
		{
			uiEndTime = uiStartTime;
			if (pchNode && pchNode[0])
			{
				eTrainType = CharacterTrainingType_Replace;
			}
		}
		if (pItem)
		{
			uiRemoveItemID = pItem->id;
			if (pItemDef && (bAlwaysDestroyItem || pItemDef->bTrainingDestroysItem))
			{
				bDestroyItem = true;
			}
		}
		character_StartTraining(entGetPartitionIdx(pEnt),pEnt, pPetEnt,
								pchNode, pNewNodeDef->pchNameFull, iNewNodeRank, pchCostNumeric, iCost, 
								fRefundPercent, uiStartTime, uiEndTime, 
								uiRemoveItemID, bDestroyItem, true, eTrainType, TrainEnt_CB,
								TrainEnt_CreateCBData(pEnt, pPetEnt, pNewNodeDef->pchNameFull, eTrainerType));
	}
}