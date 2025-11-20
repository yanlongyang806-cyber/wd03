#include "wrContainerSubs.h"
#include "AutoGen/wrContainerSubs_c_ast.h"

#include "Entity.h"
#include "EntitySavedData.h"
#include "GameAccountDataCommon.h"
#include "GlobalTypes.h"
#include "gslAccountProxy.h"
#include "Player.h"
#include "ResourceInfo.h"

#include "AutoGen/GlobalTypes_h_ast.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

///////////////////////////////////////////////////////
// Subscription Requests using Entity / GAD

AUTO_ENUM;
typedef enum SubscriptionRequestType
{
	REQUESTTYPE_DISCOUNT,
	REQUESTTYPE_PURCHASE,
} SubscriptionRequestType;

AUTO_STRUCT;
typedef struct SubscriptionRequest
{
	U32 uSubRequestID; AST(KEY)
		U32 uCharacterID;
	U32 uAccountID;
	REF_TO(Entity) hEnt; AST(COPYDICT(ENTITYPLAYER))
	REF_TO(GameAccountData) hGAD; AST(COPYDICT(GAMEACCOUNTDATA))
	U32 uTimeStarted;

	U32 uRequestID;
	SubscriptionRequestType eRequestType;

	// Starts running after both Entity and GAD have been acquired
	// Only used for purchases, since catalog discount checks have no other delayed transactions
	bool bIsRunning; 
	bool bCompleted; // Awaiting cleanup
} SubscriptionRequest;

AUTO_STRUCT;
typedef struct SubscriptionRequestList
{
	U32 uID;
	EARRAY_OF(SubscriptionRequest) ppRequests;
} SubscriptionRequestList;

static EARRAY_OF(SubscriptionRequest) seaSubscriptionRequests = NULL;
static EARRAY_OF(SubscriptionRequest) seaSubscriptionCleanup = NULL;
static StashTable stGADSubscriptions = NULL; // Indexed by Account ID
static StashTable stEntSubscriptions = NULL; // Indexed by ent container ID

void wrCSub_Tick(void)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_FOREACH_BEGIN(seaSubscriptionCleanup, i);
	{
		// Also destroys the SubscriptionRequest object
		wrCSub_RemoveRequest(seaSubscriptionCleanup[i]->uSubRequestID);
	}
	EARRAY_FOREACH_END;
	eaClearFast(&seaSubscriptionCleanup);
	EARRAY_FOREACH_BEGIN(seaSubscriptionRequests, i);
	{
		// TODO(Theo) cleanup?
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}

bool wrCSub_VerifyVersion(SA_PARAM_NN_VALID Entity *pEnt)
{
	if (!pEnt->pSaved)
		return false;
	if (pEnt->pSaved->uFixupVersion != CURRENT_ENTITY_FIXUP_VERSION)
		return false;
	if (pEnt->pSaved->uGameSpecificFixupVersion < (U32) gameSpecificFixup_Version())
		return false;
	return true;
}

static void wrCSub_ExecuteRequest(SubscriptionRequest *request, Entity *pEnt, GameAccountData *pGAD)
{
	bool bComplete = false;

	PERFINFO_AUTO_START_FUNC();
	if (pEnt && pGAD)
		COPY_HANDLE(pEnt->pPlayer->pPlayerAccountData->hTempData, request->hGAD);
	if (request->eRequestType == REQUESTTYPE_DISCOUNT)
	{
		WebDiscountRequest *data = gslAPGetDiscountRequest(request->uRequestID);
		if (data)
			gslMTProducts_UserCheckProducts(pEnt, pGAD, data);
		bComplete = true;
	}
	else if (request->eRequestType == REQUESTTYPE_PURCHASE)
	{
		PurchaseRequestData *data = gslMTPurchase_GetRequest(request->uRequestID);
		if (data)
		{
			gslAP_WebCStorePurchase(pEnt, pGAD, data);
			request->bIsRunning = true;
		}
		else
			bComplete = true;
	}
	if (bComplete && request->uSubRequestID)
	{
		request->bCompleted = true;
		eaPush(&seaSubscriptionCleanup, request);
	}
	PERFINFO_AUTO_STOP();
}

#define SUBLIST_SIZE 10000
static SubscriptionRequestList *findSubList(U32 uID, StashTable *pTable, bool bCreate)
{
	SubscriptionRequestList *pList = NULL;
	if (!uID)
		return NULL;
	if (!*pTable)
		*pTable = stashTableCreateInt(SUBLIST_SIZE);
	if (!stashIntFindPointer(*pTable, uID, &pList) && bCreate)
	{
		pList = StructCreate(parse_SubscriptionRequestList);
		pList->uID = uID;
		assert(stashIntAddPointer(*pTable, uID, pList, false));
	}
	return pList;
}

static void wrCSub_ExistenceCB(TransactionReturnVal *pReturn, void *data)
{
	ContainerRefArray *pRefArray = NULL;

	PERFINFO_AUTO_START_FUNC();
	if (RemoteCommandCheck_DBCheckContainersExist(pReturn, &pRefArray) == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (eaSize(&pRefArray->containerRefs) > 0)
		{
			U32 uSubRequestID = (U32)(intptr_t) data;
			U32 idx = eaIndexedFindUsingInt(&seaSubscriptionRequests, uSubRequestID);
			if (idx != -1)
			{
				SubscriptionRequest *pRequest = eaRemove(&seaSubscriptionRequests, idx);
				if (pRequest) // Call the CB with no Entity/GAD to complete the request
					wrCSub_ExecuteRequest(pRequest, NULL, NULL);
			}
		}
		StructDestroy(parse_ContainerRefArray, pRefArray);
	}
	PERFINFO_AUTO_STOP();
}

// Returns true if it the Entity and GAD are already subscribed by the server
static bool wrCSub_AddRequest(SubscriptionRequest *pSubRequest, void *data)
{
	Entity *pEnt;
	GameAccountData *pGAD;
	char idBuf[128];

	PERFINFO_AUTO_START_FUNC();
	if (!seaSubscriptionRequests)
		eaIndexedEnable(&seaSubscriptionRequests, parse_SubscriptionRequest);
	eaIndexedAdd(&seaSubscriptionRequests, pSubRequest);

	if (pSubRequest->uCharacterID)
	{
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER),
			ContainerIDToString(pSubRequest->uCharacterID, idBuf), pSubRequest->hEnt);
	}

	if (pSubRequest->uAccountID)
	{
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_GAMEACCOUNTDATA),
			ContainerIDToString(pSubRequest->uAccountID, idBuf), pSubRequest->hGAD);
	}

	pEnt = GET_REF(pSubRequest->hEnt);
	pGAD = GET_REF(pSubRequest->hGAD);

	if ((!pSubRequest->uCharacterID || pEnt) && (!pSubRequest->uAccountID || pGAD))
	{
		wrCSub_ExecuteRequest(pSubRequest, pEnt, pGAD);
		PERFINFO_AUTO_STOP();
		return true;
	}
	else
	{
		ContainerRefArray containers = {0};
		ContainerRef *pConRef;
		SubscriptionRequestList *pEntList = findSubList(pSubRequest->uCharacterID, &stEntSubscriptions, true);
		SubscriptionRequestList *pGADList = findSubList(pSubRequest->uAccountID, &stGADSubscriptions, true);

		pSubRequest->uTimeStarted = timeSecondsSince2000();
		if (pEntList)
			eaPush(&pEntList->ppRequests, pSubRequest);
		if (pGADList)
			eaPush(&pGADList->ppRequests, pSubRequest);

		if (pSubRequest->uCharacterID)
		{
			pConRef = StructCreate(parse_ContainerRef);
			pConRef->containerType = GLOBALTYPE_ENTITYPLAYER;
			pConRef->containerID = pSubRequest->uCharacterID;
			eaPush(&containers.containerRefs, pConRef);
		}

		if (pSubRequest->uAccountID)
		{
			pConRef = StructCreate(parse_ContainerRef);
			pConRef->containerType = GLOBALTYPE_GAMEACCOUNTDATA;
			pConRef->containerID = pSubRequest->uAccountID;
			eaPush(&containers.containerRefs, pConRef);
		}

		RemoteCommand_DBCheckContainersExist(objCreateManagedReturnVal(wrCSub_ExistenceCB, (void*)(intptr_t) pSubRequest->uSubRequestID), 
			GLOBALTYPE_OBJECTDB, 0, &containers);
		StructDeInit(parse_ContainerRefArray, &containers);
		PERFINFO_AUTO_STOP();
		return false;
	}
}

static SubscriptionRequest *wrCSub_CreateRequest(U32 uRequestID, U32 uCharacterID, U32 uAccountID, SubscriptionRequestType eType)
{
	static U32 uID = 1;
	SubscriptionRequest *pSubRequest = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pSubRequest = StructCreate(parse_SubscriptionRequest);
	pSubRequest->uSubRequestID = uID++;
	pSubRequest->uAccountID = uAccountID;
	pSubRequest->uCharacterID = uCharacterID;
	pSubRequest->uRequestID = uRequestID;
	pSubRequest->eRequestType = eType;
	PERFINFO_AUTO_STOP();

	return pSubRequest;
}

void wrCSub_AddDiscountRequest(WebDiscountRequest *pDiscount)
{
	SubscriptionRequest *pRequest = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pRequest = wrCSub_CreateRequest(pDiscount->uRequestID, pDiscount->uCharacterID, 
		pDiscount->uAccountID, REQUESTTYPE_DISCOUNT);
	pDiscount->uSubRequestID = pRequest->uSubRequestID;
	// Pushed to remove queue in wrCSub_ExecuteRequest
	wrCSub_AddRequest(pRequest, pDiscount);
	PERFINFO_AUTO_STOP();
}

void wrCSub_AddPurchaseRequest(PurchaseRequestData *pPurchase)
{
	SubscriptionRequest *pRequest = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pRequest = wrCSub_CreateRequest(pPurchase->uRequestID, pPurchase->uCharacterID, 
		pPurchase->uAccountID, REQUESTTYPE_PURCHASE);
	pPurchase->uSubRequestID = pRequest->uSubRequestID;
	wrCSub_AddRequest(pRequest, pPurchase);
	PERFINFO_AUTO_STOP();
}

static SubscriptionRequest *wrCSub_FindRequest(U32 uSubRequestID)
{
	if (seaSubscriptionRequests)
		return eaIndexedGetUsingInt(&seaSubscriptionRequests, uSubRequestID);
	return NULL;
}

void wrCSub_RemoveRequest(U32 uSubRequestID)
{
	SubscriptionRequestList *pEntList;
	SubscriptionRequestList *pGADList;
	SubscriptionRequest *pRequest = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pRequest = wrCSub_FindRequest(uSubRequestID);

	if (!pRequest)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	pEntList = findSubList(pRequest->uCharacterID, &stEntSubscriptions, false);
	pGADList = findSubList(pRequest->uAccountID, &stGADSubscriptions, false);
	// Remove from Entity SubList
	if (pEntList)
	{
		eaFindAndRemove(&pEntList->ppRequests, pRequest);
		if (eaSize(&pEntList->ppRequests) == 0)
		{
			stashIntRemovePointer(stEntSubscriptions, pRequest->uCharacterID, NULL);
			StructDestroy(parse_SubscriptionRequestList, pEntList);
		}
	}

	// Remove from GAD SubList
	if (pGADList)
	{
		eaFindAndRemove(&pGADList->ppRequests, pRequest);
		if (eaSize(&pGADList->ppRequests) == 0)
		{
			stashIntRemovePointer(stGADSubscriptions, pRequest->uAccountID, NULL);
			StructDestroy(parse_SubscriptionRequestList, pGADList);
		}
	}

	eaFindAndRemove(&seaSubscriptionRequests, pRequest);
	StructDestroy(parse_SubscriptionRequest, pRequest);
	PERFINFO_AUTO_STOP();
}

void wrCSub_MarkPurchaseCompleted(PurchaseRequestData *pPurchase)
{
	SubscriptionRequest *pRequest = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pRequest= wrCSub_FindRequest(pPurchase->uSubRequestID);
	if (pRequest)
	{
		pRequest->bIsRunning = false;
		pRequest->bCompleted = true;
		eaPush(&seaSubscriptionCleanup, pRequest);
	}
	// Clear these because they should no longer be used
	pPurchase->pEnt = NULL;
	pPurchase->pGAD = NULL;
	PERFINFO_AUTO_STOP();
}

static void wrCSub_SubscriptionReceived(SubscriptionRequestList *pList)
{
	PERFINFO_AUTO_START_FUNC();
	EARRAY_FOREACH_BEGIN(pList->ppRequests, i);
	{
		SubscriptionRequest *pRequest = pList->ppRequests[i];
		if (!pRequest->bIsRunning && !pRequest->bCompleted)
		{
			Entity *pEnt = GET_REF(pRequest->hEnt);
			GameAccountData *pGAD = GET_REF(pRequest->hGAD);

			if ((!pRequest->uCharacterID || pEnt) && (!pRequest->uAccountID || pGAD))
			{
				wrCSub_ExecuteRequest(pRequest, pEnt, pGAD);
			}
		}
	}
	EARRAY_FOREACH_END;
	PERFINFO_AUTO_STOP();
}
void wrCSub_EntitySubscribed(Entity *pEnt)
{
	SubscriptionRequestList *pEntList = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pEntList = findSubList(entGetContainerID(pEnt), &stEntSubscriptions, false);
	if (pEntList)
		wrCSub_SubscriptionReceived(pEntList);
	PERFINFO_AUTO_STOP();
}
void wrCSub_GADSubscribed(GameAccountData *pGAD)
{
	SubscriptionRequestList *pGADList = NULL;
	
	PERFINFO_AUTO_START_FUNC();
	pGADList = findSubList(pGAD->iAccountID, &stGADSubscriptions, false);
	if (pGADList)
		wrCSub_SubscriptionReceived(pGADList);
	PERFINFO_AUTO_STOP();
}

#include "AutoGen/wrContainerSubs_c_ast.c"