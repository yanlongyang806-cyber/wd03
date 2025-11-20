/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "aslPersistedStores.h"
#include "WorldLibEnums.h"
#include "itemCommon.h"
#include "mission_common.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "rand.h"
#include "storeCommon.h"
#include "storeCommon_h_ast.h"
#include "StringCache.h"
#include "AutoGen/aslPersistedStores_h_ast.h"
#include "AutoGen/aslPersistedStores_h_ast.c"
#include "AutoGen/storeCommon_h_ast.h"
#include "AutoGen/ObjectDB_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"

static bool s_bReceiveContainersSuccess = false;
static S32 s_iContainersRequested = 0;
static S32 s_iContainersReceived = 0;
static const char** s_ppchPendingStores = NULL;
static PersistedStoreRequest** s_eaRequests = NULL;

#define PERSISTED_STORES_DEFAULT_HASH_SIZE 50
#define PERSISTED_STORES_MAX_ITEMS_PER_RESTOCKDEF 100
#define PERSISTED_STORE_DEFAULT_UPDATE_TIME 10
#define PERSISTED_STORE_REQUEST_VERIFY_TIME 900

typedef struct PersistStoreCBData
{
	ContainerID uPlayerID;
	ContainerID uRequestID;
} PersistStoreCBData;

static PersistedStore* aslPersistedStores_FindByID(ContainerID uContainerID)
{
	Container* pContainer = objGetContainer(GLOBALTYPE_PERSISTEDSTORE, uContainerID);

	if (pContainer) 
	{
		return (PersistedStore*)pContainer->containerData;
	}
	return NULL;
}

static PersistedStore* aslPersistedStores_FindByDef(StoreDef* pDef)
{
	PersistedStore* pStore;
	ContainerIterator pContainerIter;

	if (!pDef || !pDef->bIsPersisted)
	{
		return NULL;
	}
	objInitContainerIteratorFromType(GLOBALTYPE_PERSISTEDSTORE, &pContainerIter);
	while (pStore = objGetNextObjectFromIterator(&pContainerIter))
	{
		if (pDef == GET_REF(pStore->hStoreDef))
		{
			break;
		}
	}
	objClearContainerIterator(&pContainerIter);
	return pStore;
}

static void aslPersistedStore_Create_CB(TransactionReturnVal* pReturn, char* pchName)
{
	S32 i = eaFind(&s_ppchPendingStores, allocFindString(pchName));
	if (i >= 0)
	{
		eaRemove(&s_ppchPendingStores, i);
	}
	SAFE_FREE(pchName);
}

static void aslPersistedStores_UpdateContainers(void)
{
	StoreDef* pStoreDef;
	PersistedStore* pStore;
	RefDictIterator pStoreDefIter;
	ContainerIterator pContainerIter;

	RefSystem_InitRefDictIterator("Store", &pStoreDefIter);
	while (pStoreDef = (StoreDef*)RefSystem_GetNextReferentFromIterator(&pStoreDefIter))
	{
		if (!pStoreDef->bIsPersisted)
		{
			continue;
		}

		objInitContainerIteratorFromType(GLOBALTYPE_PERSISTEDSTORE, &pContainerIter);

		while (pStore = objGetNextObjectFromIterator(&pContainerIter))
		{
			if (pStoreDef == GET_REF(pStore->hStoreDef))
			{
				break;
			}
		}
		objClearContainerIterator(&pContainerIter);
		if (!pStore && eaFind(&s_ppchPendingStores, pStoreDef->name) < 0)
		{
			NOCONST(PersistedStore)* pTempStore = StructCreateNoConst(parse_PersistedStore);
			SET_HANDLE_FROM_REFERENT("Store", pStoreDef, pTempStore->hStoreDef);
			
			// Add the store name to the list of pending stores
			eaPush(&s_ppchPendingStores, allocAddString(pStoreDef->name));

			// Create the persisted store container from the temporary store
			objRequestContainerCreate(
				objCreateManagedReturnVal(aslPersistedStore_Create_CB, strdup(pStoreDef->name)), 
				GLOBALTYPE_PERSISTEDSTORE, pTempStore,  
				objServerType(), objServerID());
			StructDestroyNoConst(parse_PersistedStore, pTempStore);
		}
	}

	//Remove old persisted stores that don't exist anymore
	objInitContainerIteratorFromType(GLOBALTYPE_PERSISTEDSTORE, &pContainerIter);
	while (pStore = objGetNextObjectFromIterator(&pContainerIter))
	{
		pStoreDef = GET_REF(pStore->hStoreDef);
		if (!pStoreDef || !pStoreDef->bIsPersisted)
		{
			pStore->bDirty = true;
			objRequestContainerDestroy(NULL, GLOBALTYPE_PERSISTEDSTORE, pStore->uContainerID, 
				objServerType(), objServerID());
		}
	}
	objClearContainerIterator(&pContainerIter);
}

static void aslPersistedStores_ContainerMove_CB(TransactionReturnVal* pReturn, void* pData)
{
	if (pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		loadupdate_printf("Failed to fetch %s container: %s.\n", 
			GlobalTypeToName(GLOBALTYPE_PERSISTEDSTORE),
			pReturn->pBaseReturnVals[0].returnString);
	}
	else if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (++s_iContainersReceived == s_iContainersRequested)
		{
			s_bReceiveContainersSuccess = true;
			aslPersistedStores_UpdateContainers();
		}
	}
}

void aslPersistedStores_Acquire_CB(TransactionReturnVal *pReturn, void* pData)
{
	ContainerList *pList;
	if (RemoteCommandCheck_dbAcquireContainers(pReturn, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		s_iContainersRequested = 0;
		while (eaiSize(&pList->eaiContainers) > 0) 
		{
			U32 conid = eaiPop(&pList->eaiContainers);
			objRequestContainerMove(objCreateManagedReturnVal(aslPersistedStores_ContainerMove_CB, pData), pList->type, conid, GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID());
			s_iContainersRequested++;
		}
		if (s_iContainersRequested == 0)
		{
			s_bReceiveContainersSuccess = true;
			aslPersistedStores_UpdateContainers();
		}
	}
}

void aslPersistedStores_Load(void)
{
	if (gConf.bEnablePersistedStores)
	{
		StoreCategories_Load();
		StoreHighlightCategories_Load();
		store_Load();

		RemoteCommand_dbAcquireContainers(objCreateManagedReturnVal(aslPersistedStores_Acquire_CB, NULL), 
			GLOBALTYPE_OBJECTDB, 0, objServerType(), objServerID(), GLOBALTYPE_PERSISTEDSTORE);
	}
}

static void aslPersistedStore_RequestUpdatePlayerData(PersistedStoreRequest* pRequest)
{
	U32 uNextCheckTime = 0;
	S32 i;
	for (i = eaSize(&pRequest->eaPlayers)-1; i >= 0; i--)
	{
		PersistedStorePlayerRequest* pPlayerInfo = pRequest->eaPlayers[i];
		if (!uNextCheckTime || pPlayerInfo->uNextCheckTime < uNextCheckTime)
		{
			uNextCheckTime = pPlayerInfo->uNextCheckTime;
		}
	}
	pRequest->uNextPlayerCheckTime = uNextCheckTime;
}

static PersistedStoreRequest* aslPersistedStore_CreateRequest(ContainerID uID)
{
	PersistedStoreRequest* pRequest = StructCreate(parse_PersistedStoreRequest);
	pRequest->uContainerID = uID;
	eaPush(&s_eaRequests, pRequest);
	return pRequest;
}

static PersistedStorePlayerRequest* aslPersistedStore_AddPlayerToRequest(PersistedStoreRequest* pRequest,
																		 ContainerID uPlayerID)
{
	PersistedStorePlayerRequest* pInfo = eaIndexedGetUsingInt(&pRequest->eaPlayers, uPlayerID);
	if (!pInfo)
	{
		pInfo = StructCreate(parse_PersistedStorePlayerRequest);
		pInfo->uNextCheckTime = timeSecondsSince2000() + PERSISTED_STORE_REQUEST_VERIFY_TIME;
		pInfo->uPlayerID = uPlayerID;
		eaIndexedEnable(&pRequest->eaPlayers, parse_PersistedStorePlayerRequest);
		eaPush(&pRequest->eaPlayers, pInfo);
		aslPersistedStore_RequestUpdatePlayerData(pRequest);
	}
	return pInfo;
}

static void aslPersistedStore_PlayerRemoveSingleRequest(U32 uPlayerID, S32 iRequestIdx)
{
	PersistedStoreRequest* pRequest = eaGet(&s_eaRequests, iRequestIdx);
	if (pRequest)
	{
		S32 iPlayerIdx = eaIndexedFindUsingInt(&pRequest->eaPlayers, uPlayerID);
		if (iPlayerIdx >= 0)
		{
			StructDestroy(parse_PersistedStorePlayerRequest, eaRemove(&pRequest->eaPlayers, iPlayerIdx));
		}
		if (eaSize(&pRequest->eaPlayers)==0)
		{
			StructDestroy(parse_PersistedStoreRequest, eaRemove(&s_eaRequests, iRequestIdx));
		}
		else if (iPlayerIdx >= 0)
		{
			aslPersistedStore_RequestUpdatePlayerData(pRequest);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
U32 aslPersistedStore_PlayerAddRequest(U32 uPlayerID, const char* pchStoreName, U32 bRemoveOtherRequests)
{
	StoreDef* pDef = store_DefFromName(pchStoreName);
	PersistedStore* pPersistStore = aslPersistedStores_FindByDef(pDef);
	PersistedStoreRequest* pRequest;
	S32 i;
	
	if (!pPersistStore)
	{
		return 0;
	}
	for (i = eaSize(&s_eaRequests)-1; i >= 0; i--)
	{
		pRequest = s_eaRequests[i];
		if (pRequest->uContainerID == pPersistStore->uContainerID)
		{
			aslPersistedStore_AddPlayerToRequest(pRequest, uPlayerID);
			if (!bRemoveOtherRequests)
			{
				break;
			}
		}
		else if (bRemoveOtherRequests)
		{
			aslPersistedStore_PlayerRemoveSingleRequest(uPlayerID, i);
		}
	}
	if (i < 0)
	{
		pRequest = aslPersistedStore_CreateRequest(pPersistStore->uContainerID);
		aslPersistedStore_AddPlayerToRequest(pRequest, uPlayerID);
	}
	return pPersistStore->uContainerID;
}

static void aslPersistedStore_PlayerRemoveRequest(U32 uPlayerID, PersistedStore* pPersistStore)
{
	S32 i;
	for (i = eaSize(&s_eaRequests)-1; i >= 0; i--)
	{
		PersistedStoreRequest* pRequest = s_eaRequests[i];
		if (!pPersistStore || pRequest->uContainerID == pPersistStore->uContainerID)
		{
			aslPersistedStore_PlayerRemoveSingleRequest(uPlayerID, i);
			if (pPersistStore)
			{
				return;
			}
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslPersistedStore_PlayerRemoveRequestByID(U32 uPlayerID, U32 uStoreID)
{
	PersistedStore* pPersistStore = aslPersistedStores_FindByID(uStoreID);
	if (pPersistStore)
	{
		aslPersistedStore_PlayerRemoveRequest(uPlayerID, pPersistStore);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslPersistedStore_PlayerRemoveRequestByName(U32 uPlayerID, const char* pchStoreName)
{
	StoreDef* pDef = store_DefFromName(pchStoreName);
	PersistedStore* pPersistStore = aslPersistedStores_FindByDef(pDef);
	if (pPersistStore)
	{
		aslPersistedStore_PlayerRemoveRequest(uPlayerID, pPersistStore);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void aslPersistedStore_PlayerRemoveAllRequests(U32 uPlayerID)
{
	aslPersistedStore_PlayerRemoveRequest(uPlayerID, NULL);
}

static void aslPersistedStore_Update_CB(TransactionReturnVal* pReturn, ContainerID* puID)
{
	PersistedStore* pStore = aslPersistedStores_FindByID(*puID);
	if (pStore)
	{
		pStore->bDirty = false;
	}
	SAFE_FREE(puID);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pPersistStore, ".Umaxid, .Eainventory");
void aslPersistedStore_trh_AddItem(ATH_ARG NOCONST(PersistedStore)* pPersistStore, 
								   StoreDef* pDef, StoreRestockDef* pStoreRestockDef,
								   U32 uCurrentTime, U32 uSeed)
{
	U32 uMinExpire = pStoreRestockDef->uExpireTimeMin;
	U32 uMaxExpire = pStoreRestockDef->uExpireTimeMax;
	NOCONST(PersistedStoreItem)* pStoreItem = StructCreateNoConst(parse_PersistedStoreItem);
	pStoreItem->pchRestockDef = (char*)allocAddString(pStoreRestockDef->pchName);
	pStoreItem->uSeed = uSeed;
	pStoreItem->uExpireTime = uCurrentTime + randomIntRange(uMinExpire, uMaxExpire);
	pStoreItem->uID = ++pPersistStore->uMaxID;
	pStoreItem->iRewardIndex = 0; // For now, just use the first item generated from the reward table
	eaPush(&pPersistStore->eaInventory, pStoreItem);
}

AUTO_TRANS_HELPER
ATR_LOCKS(pPersistStore, ".Eainventory");
S32 aslPersistedStore_trh_GetItemCount(ATH_ARG NOCONST(PersistedStore)* pPersistStore, 
									   StoreDef* pDef,
									   StoreRestockDef* pRestockDef)
{
	S32 i, iCount = 0;
	for (i = eaSize(&pPersistStore->eaInventory)-1; i >= 0; i--)
	{
		NOCONST(PersistedStoreItem)* pItem = pPersistStore->eaInventory[i];
		StoreRestockDef* pCurrRestockDef = PersistedStore_FindRestockDefByName(pDef, pItem->pchRestockDef);
		
		if (pCurrRestockDef == pRestockDef)
		{
			iCount++;
		}
	}
	return iCount;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pPersistStore, ".Earestockdata");
void aslPersistedStore_trh_CleanupRestockData(ATH_ARG NOCONST(PersistedStore)* pPersistStore, 
											  StoreDef* pDef, 
											  U32 uCurrentTime)
{
	S32 i, j;
	//Remove RestockData that doesn't exist in the def
	for (i = eaSize(&pPersistStore->eaRestockData)-1; i >= 0; i--)
	{
		for (j = eaSize(&pDef->eaRestockDefs)-1; j >= 0; j--)
		{
			if (pPersistStore->eaRestockData[i]->pchRestockDef == pDef->eaRestockDefs[j]->pchName)
			{
				break;
			}
		}
		if (j < 0)
		{
			StructDestroyNoConst(parse_PersistedStoreRestockData, eaRemove(&pPersistStore->eaRestockData, i));
		}
	}
	//Add RestockData that exists in the def but not in the store
	for (i = eaSize(&pDef->eaRestockDefs)-1; i >= 0; i--)
	{
		for (j = eaSize(&pPersistStore->eaRestockData)-1; j >= 0; j--)
		{
			if (pDef->eaRestockDefs[i]->pchName == pPersistStore->eaRestockData[j]->pchRestockDef)
			{
				pPersistStore->eaRestockData[j]->pRestockDef = pDef->eaRestockDefs[i];
				break;
			}
		}
		if (j < 0)
		{
			NOCONST(PersistedStoreRestockData)* pRestockData;
			pRestockData = StructCreateNoConst(parse_PersistedStoreRestockData);
			pRestockData->pRestockDef = pDef->eaRestockDefs[i];
			pRestockData->pchRestockDef = (char*)allocAddString(pRestockData->pRestockDef->pchName);
			eaPush(&pPersistStore->eaRestockData, pRestockData);
		}
	}
}

#define GET_NEXT_UPDATE_TIME(uCurr, uTime) uCurr ? MIN(uCurr, uTime) : uTime

AUTO_TRANS_HELPER
ATR_LOCKS(pPersistStore, ".Eainventory, .Earestockdata, .Uversion, .Unextupdatetime, .Hstoredef, .Umaxid");
bool aslPersistedStore_trh_Update(ATH_ARG NOCONST(PersistedStore)* pPersistStore, U32 uCurrentTime)
{
	S32 i;
	bool bChangedInventory = false;
	U32 uNextUpdateTime = 0;
	U32 uSeed = uCurrentTime;
	StoreDef* pDef = GET_REF(pPersistStore->hStoreDef);
	if (pDef)
	{
		aslPersistedStore_trh_CleanupRestockData(pPersistStore, pDef, uCurrentTime);

		//Expire existing items
		for (i = eaSize(&pPersistStore->eaInventory)-1; i >= 0; i--)
		{
			NOCONST(PersistedStoreItem)* pStoreItem = pPersistStore->eaInventory[i];
			StoreRestockDef* pRestockDef = PersistedStore_FindRestockDefByName(pDef, pStoreItem->pchRestockDef);

			if (pRestockDef && pRestockDef->uExpireTimeMin == 0)
			{
				continue;
			}
			if (!pRestockDef || uCurrentTime >= pStoreItem->uExpireTime)
			{
				StructDestroyNoConst(parse_PersistedStoreItem, eaRemove(&pPersistStore->eaInventory, i));
				bChangedInventory = true;
			}
		}
	
		//Replenish Items
		for (i = eaSize(&pPersistStore->eaRestockData)-1; i >= 0; i--)
		{
			NOCONST(PersistedStoreRestockData)* pRestockData = pPersistStore->eaRestockData[i];
			StoreRestockDef* pRestockDef = pRestockData->pRestockDef;
			U32 uMinReplenish = pRestockDef->uReplenishTimeMin;
			U32 uMaxReplenish = pRestockDef->uReplenishTimeMax;
			U32 uItemCount = aslPersistedStore_trh_GetItemCount(pPersistStore, pDef, pRestockDef);
			U32 uAbsMax = PERSISTED_STORES_MAX_ITEMS_PER_RESTOCKDEF;
			U32 uRestockMax = pRestockDef->uMaxItemCount ? pRestockDef->uMaxItemCount : uAbsMax;

			// Make sure that the store has the minimum number of items defined by the StoreRestockDef
			while (uItemCount < pRestockDef->uMinItemCount)
			{
				aslPersistedStore_trh_AddItem(pPersistStore, pDef, pRestockDef, uCurrentTime, uSeed++);
				bChangedInventory = true;
				uItemCount++;
			}
			// If the StoreRestockDef doesn't have a replenish time, then stop here
			if (!uMinReplenish)
			{
				continue;
			}
			// Periodically replenish items
			if (uItemCount < uRestockMax)
			{
				if (!pRestockData->uNextReplenishTime)
				{
					pRestockData->uNextReplenishTime = uCurrentTime + randomIntRange(uMinReplenish, uMaxReplenish);
				}
				if (uCurrentTime > pRestockData->uNextReplenishTime && pRestockDef->uExpireTimeMin > 0)
				{
					U32 uTimeDelta = uCurrentTime - pRestockData->uNextReplenishTime;
					U32 uMaxExpireTime = pRestockDef->uExpireTimeMax;
					if (uTimeDelta > uMaxExpireTime)
					{
						U32 uSmallestTimeInterval = 30;
						U32 uStartTime = (uCurrentTime - uMaxExpireTime);
						U32 uMin = MAX(uSmallestTimeInterval, uMinReplenish);
						U32 uMax = MAX(uMin+1, uMaxReplenish);
						pRestockData->uNextReplenishTime = uStartTime + randomIntRange(uMin, uMax);
					}
				}
				while (uCurrentTime > pRestockData->uNextReplenishTime)
				{
					U32 uRandomReplenish = randomIntRange(uMinReplenish, uMaxReplenish);
					
					aslPersistedStore_trh_AddItem(pPersistStore, pDef, pRestockDef, uCurrentTime, uSeed++);
					pRestockData->uNextReplenishTime += uRandomReplenish;
					bChangedInventory = true;

					if (++uItemCount >= uRestockMax)
					{
						break;
					}
				}
			}
			else
			{
				pRestockData->uNextReplenishTime = 0;
			}
			uNextUpdateTime = GET_NEXT_UPDATE_TIME(uNextUpdateTime, pRestockData->uNextReplenishTime);
		}
		//Update next expire time
		for (i = eaSize(&pPersistStore->eaInventory)-1; i >= 0; i--)
		{
			NOCONST(PersistedStoreItem)* pStoreItem = pPersistStore->eaInventory[i];
			uNextUpdateTime = GET_NEXT_UPDATE_TIME(uNextUpdateTime, pStoreItem->uExpireTime);
		}
		if (bChangedInventory)
		{
			pPersistStore->uVersion++;
		}
		if (uNextUpdateTime)
		{
			pPersistStore->uNextUpdateTime = uNextUpdateTime;
		}
		else
		{
			pPersistStore->uNextUpdateTime = uCurrentTime + PERSISTED_STORE_DEFAULT_UPDATE_TIME;
		}
		return true;
	}
	return false;
}

AUTO_TRANSACTION
ATR_LOCKS(pPersistStore, ".Eainventory, .Earestockdata, .Uversion, .Unextupdatetime, .Hstoredef, .Umaxid");
enumTransactionOutcome aslPersistedStore_tr_Update(ATR_ARGS, 
												   NOCONST(PersistedStore)* pPersistStore, 
												   U32 uCurrentTime)
{
	if (!aslPersistedStore_trh_Update(pPersistStore, uCurrentTime))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
	return TRANSACTION_OUTCOME_SUCCESS;
}

static void aslPersistedStore_Verify_CB(TransactionReturnVal* pReturn, PersistStoreCBData* pData)
{
	bool bSuccess = false;
	enumTransactionOutcome eOutcome;
	eOutcome = RemoteCommandCheck_gslPersistedStore_VerfiyPlayerRequest(pReturn, &bSuccess);
	if (eOutcome == TRANSACTION_OUTCOME_FAILURE || !bSuccess)
	{
		S32 i;
		for (i = eaSize(&s_eaRequests)-1; i >= 0; i--)
		{
			PersistedStoreRequest* pRequest = s_eaRequests[i];
			if (pRequest->uContainerID == pData->uRequestID)
			{
				aslPersistedStore_PlayerRemoveSingleRequest(pData->uPlayerID, i);
				break;
			}
		}
	}
	SAFE_FREE(pData);
}

void aslPersistedStores_Tick(void)
{
	if (!s_bReceiveContainersSuccess)
	{
		return;
	}
	else
	{
		U32 uCurrentTime = timeSecondsSince2000();
		S32 i, j;

		for (i = eaSize(&s_eaRequests)-1; i >= 0; i--)
		{
			PersistedStoreRequest* pRequest = s_eaRequests[i];
			PersistedStore* pPersistStore = aslPersistedStores_FindByID(pRequest->uContainerID);
			
			if (!pPersistStore)
			{
				continue;
			}
			if (!pPersistStore->bDirty)
			{
				if (uCurrentTime >= pPersistStore->uNextUpdateTime)
				{
					ContainerID* puID = calloc(1, sizeof(ContainerID));
					TransactionReturnVal* pReturn;
					*puID = pPersistStore->uContainerID;
					pReturn = objCreateManagedReturnVal(aslPersistedStore_Update_CB, puID);
					AutoTrans_aslPersistedStore_tr_Update(pReturn, GetAppGlobalType(), 
						GLOBALTYPE_PERSISTEDSTORE, pPersistStore->uContainerID, uCurrentTime);
					pPersistStore->bDirty = true;
				}	
			}
			// Make sure that if something goes drastically wrong and a player doesn't remove himself
			// from the request list, then have this fallback to periodically verify that the player
			// is still interacting with the persisted store.
			if (uCurrentTime >= pRequest->uNextPlayerCheckTime)
			{
				for (j = eaSize(&pRequest->eaPlayers)-1; j >= 0; j--)
				{
					PersistedStorePlayerRequest* pPlayerInfo = pRequest->eaPlayers[j];
					if (uCurrentTime >= pPlayerInfo->uNextCheckTime)
					{
						PersistStoreCBData* cbData = calloc(1, sizeof(PersistStoreCBData));
						TransactionReturnVal* pReturn;
						cbData->uPlayerID = pPlayerInfo->uPlayerID;
						cbData->uRequestID = pRequest->uContainerID;
						pReturn = objCreateManagedReturnVal(aslPersistedStore_Verify_CB, cbData);
						RemoteCommand_gslPersistedStore_VerfiyPlayerRequest(pReturn,
							GLOBALTYPE_ENTITYPLAYER, pPlayerInfo->uPlayerID, 
							pPlayerInfo->uPlayerID, REF_STRING_FROM_HANDLE(pPersistStore->hStoreDef));
						pPlayerInfo->uNextCheckTime = timeSecondsSince2000() + PERSISTED_STORE_REQUEST_VERIFY_TIME;
					}
				}
				aslPersistedStore_RequestUpdatePlayerData(pRequest);
			}
		}
	}
}