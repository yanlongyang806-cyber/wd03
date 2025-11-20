#include "InternalSubs.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "earray.h"
#include "SubscriptionHistory.h"
#include "Product.h"
#include "AccountManagement.h"

/************************************************************************/
/* Global variables                                                     */
/************************************************************************/

static StashTable stInternalSubsByAccountIDAndSubID = NULL;

static StashTable stInternalSubsByAccountID = NULL;

void InternalSubs_DestroyContainers(void)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION);
	if (stInternalSubsByAccountIDAndSubID)
		stashTableClear(stInternalSubsByAccountIDAndSubID);
	if (stInternalSubsByAccountID)
		stashTableClear(stInternalSubsByAccountID);
}

static void destroyInternalSub(SA_PARAM_NN_VALID const InternalSubscription *pInternalSub);
void destroyInternalSubsByAccountID(U32 uAccountID)
{
	const InternalSubscription **ppSubs = findInternalSubsByAccountID(uAccountID);
	EARRAY_FOREACH_BEGIN(ppSubs, i);
	{
		if (ppSubs[i])
			destroyInternalSub(ppSubs[i]);
	}
	EARRAY_FOREACH_END;
	eaDestroy(&ppSubs);
}


/************************************************************************/
/* Local functions                                                      */
/************************************************************************/

// Creates a stash key
__forceinline static void makeStashKey(SA_PARAM_NN_VALID char **ppKey, U32 uAccountID, SA_PARAM_NN_VALID const char * pSubInternalName)
{
	estrPrintf(ppKey, "%d-%s", uAccountID, pSubInternalName);
}

// Add an internal sub to the stash tables
static void addInternalSubToStashTables(SA_PARAM_NN_VALID InternalSubscription *pInternalSub)
{
	char *pKey = NULL;
	StashElement pElement;

	if (!stInternalSubsByAccountIDAndSubID)
		stInternalSubsByAccountIDAndSubID = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);

	if (!stInternalSubsByAccountID)
		stInternalSubsByAccountID = stashTableCreateInt(100);

	makeStashKey(&pKey, pInternalSub->uAccountID, pInternalSub->pSubInternalName);	
	stashAddInt(stInternalSubsByAccountIDAndSubID, pKey, pInternalSub->uID, false);
	estrDestroy(&pKey);

	if (stashIntFindElement(stInternalSubsByAccountID, pInternalSub->uAccountID, &pElement))
	{
		INT_EARRAY eaContainerIDs = stashElementGetPointer(pElement);
		ea32PushUnique(&eaContainerIDs, pInternalSub->uID);
		stashElementSetPointer(pElement, eaContainerIDs);
	}
	else
	{
		INT_EARRAY eaContainerIDs = NULL;
		ea32Push(&eaContainerIDs, pInternalSub->uID);
		stashIntAddPointer(stInternalSubsByAccountID, pInternalSub->uAccountID, eaContainerIDs, false);
	}
}

// This triggers after an internal subscription is added
static void postAdd(SA_PARAM_NN_VALID Container *con, SA_PARAM_NN_VALID InternalSubscription *pInternalSub)
{
	addInternalSubToStashTables(pInternalSub);
}

// This triggers after an internal subscription is removed
static void postRemove(SA_PARAM_NN_VALID Container *con, SA_PARAM_NN_VALID InternalSubscription *pInternalSub)
{
	char *pKey = NULL;
	StashElement pElement;

	if (!stInternalSubsByAccountIDAndSubID)
		return;

	if (!stInternalSubsByAccountID)
		return;

	makeStashKey(&pKey, pInternalSub->uAccountID, pInternalSub->pSubInternalName);	
	stashRemoveInt(stInternalSubsByAccountIDAndSubID, pKey, NULL);
	estrDestroy(&pKey);

	if (stashIntFindElement(stInternalSubsByAccountID, pInternalSub->uAccountID, &pElement))
	{
		CONTAINERID_EARRAY eaContainerIDs = stashElementGetPointer(pElement);

		int i;
		for (i = 0; i < ea32Size(&eaContainerIDs); i++)
		{
			if (eaContainerIDs[i] == pInternalSub->uID)
			{
				ea32Remove(&eaContainerIDs, i);
				break;
			}
		}
		if (!ea32Size(&eaContainerIDs))
		{
			ea32Destroy(&eaContainerIDs);
			stashIntRemovePointer(stInternalSubsByAccountID, pInternalSub->uAccountID, NULL);
		}
		else
		{
			stashElementSetPointer(pElement, eaContainerIDs);
		}
	}
}

// Recreate the stash tables
static void recreateStashTables(void)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	U32 uSize;

	if (!stInternalSubsByAccountIDAndSubID)
		stInternalSubsByAccountIDAndSubID = stashTableCreateWithStringKeys(100, StashDeepCopyKeys);

	if (!stInternalSubsByAccountID)
		stInternalSubsByAccountID = stashTableCreateInt(100);

	uSize = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION);

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		InternalSubscription *pSub = (InternalSubscription *)currCon->containerData;
		addInternalSubToStashTables(pSub);
		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
}

// Destroy an internal sub
static void destroyInternalSub(SA_PARAM_NN_VALID const InternalSubscription *pInternalSub)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	const ProductContainer *pProduct;

	if (!verify(pInternalSub)) return;
	if (!verify(pInternalSub->uID)) return;

	PERFINFO_AUTO_START_FUNC();

	pProduct = findProductByID(pInternalSub->uProductID);

	if (devassert(pProduct))
	{
		SubscriptionHistoryEntryReason eReason = SHER_Cancelled;
		U32 uEnd = pInternalSub->uExpiration;

		if (pInternalSub->uExpiration && pInternalSub->uExpiration < timeSecondsSince2000())
			eReason = SHER_Expired;
		else
			uEnd = timeSecondsSince2000();

		if (devassert(uEnd))
		{
			accountArchiveSubscription(pInternalSub->uAccountID,
				pProduct->pInternalName,
				pInternalSub->pSubInternalName,
				NULL,
				pInternalSub->uCreated,
				uEnd,
				STS_Internal,
				eReason,
				0);
		}
	}

	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, pInternalSub->uID);

	PERFINFO_AUTO_STOP_FUNC();
}


/************************************************************************/
/* Modification functions                                               */
/************************************************************************/

// Create an internal subscription
bool internalSubCreate(SA_PARAM_NN_VALID AccountInfo * pAccount, SA_PARAM_NN_VALID const char * pSubInternalName, U32 uExpiration, U32 uProductID)
{
	NOCONST(InternalSubscription) *pInternalSub;

	if (!verify(pAccount)) return false;
	if (!verify(pSubInternalName && *pSubInternalName)) return false;
	if (!verify(uExpiration ? uExpiration > timeSecondsSince2000() : true)) return false;
	if (!verify(uProductID)) return false;

	PERFINFO_AUTO_START_FUNC();
	
	// Make sure they don't already have one.
	if (findInternalSub(pAccount->uID, pSubInternalName))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pInternalSub = StructCreateNoConst(parse_InternalSubscription);

	if (!devassert(pInternalSub))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pInternalSub->uAccountID = pAccount->uID;
	estrCopy2(&pInternalSub->pSubInternalName, pSubInternalName);
	pInternalSub->uExpiration = uExpiration;
	pInternalSub->uProductID = uProductID;
	pInternalSub->uCreated = timeSecondsSince2000();

	objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, pInternalSub);

	accountSetBilled(pAccount);
	PERFINFO_AUTO_STOP();

	return true;
}

// Remove an internal subscription
bool internalSubRemove(U32 uAccountID, SA_PARAM_NN_VALID const char * pSubInternalName)
{
	const InternalSubscription *pInternalSub = findInternalSub(uAccountID, pSubInternalName);

	// Make sure they already have one.
	if (!pInternalSub) return false;

	destroyInternalSub(pInternalSub);

	return true;
}

// Remove all internal subs given by a product
unsigned int internalSubRemoveByProduct(U32 uProductID)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	U32 uSize;
	const InternalSubscription **eaToRemove = NULL;
	unsigned int total = 0;
	int i;

	uSize = objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION);

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, &iter);
	currCon = objGetNextContainerFromIterator(&iter);
	while (currCon)
	{
		const InternalSubscription *pInternalSub = (const InternalSubscription *)currCon->containerData;

		if (!devassert(pInternalSub)) continue;
		
		if (pInternalSub->uProductID == uProductID)
		{
			eaPush(&eaToRemove, pInternalSub);
		}

		currCon = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);
	for (i = 0; i < eaSize(&eaToRemove); i++)
		destroyInternalSub(eaToRemove[i]);

	total = eaSize(&eaToRemove);

	eaDestroy(&eaToRemove);

	return total;
}

/************************************************************************/
/* Search functions                                                     */
/************************************************************************/

// Search for an internal sub
const InternalSubscription * findInternalSub(U32 uAccountID, const char * pSubInternalName)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	U32 uID;
	static char *pKey = NULL;

	makeStashKey(&pKey, uAccountID, pSubInternalName);

	if (stashFindInt(stInternalSubsByAccountIDAndSubID, pKey, &uID))
	{
		return findInternalSubByID(uID);
	}

	return NULL;
}

// Search for an internal sub by ID
const InternalSubscription * findInternalSubByID(U32 uID)
{
	// !!! Cannot call accountClearPermissionsCache because this function is called (directly or indirectly) from accountConstructPermissions

	Container *con = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, uID);

	if (con)
	{
		InternalSubscription *pInternalSub = con->containerData;

		if (pInternalSub->uExpiration && timeSecondsSince2000() > pInternalSub->uExpiration)
		{
			destroyInternalSub(pInternalSub);
			return NULL;
		}

		return pInternalSub;
	}

	return NULL;
}

// Search for internal subs by account ID (eaDestroy NOT eaDestroyStruct the return value!)
SA_RET_OP_VALID EARRAY_OF(const InternalSubscription) findInternalSubsByAccountID(U32 uAccountID)
{
	INT_EARRAY eaContainerIDs;
	if (stashIntFindPointer(stInternalSubsByAccountID, uAccountID, &eaContainerIDs))
	{
		int i;
		EARRAY_OF(const InternalSubscription) ret = NULL;

		for (i = 0; i < ea32Size(&eaContainerIDs); i++)
		{
			const InternalSubscription *pInternalSub = findInternalSubByID(eaContainerIDs[i]);
			if (pInternalSub) // May be NULL if it expired and was just now removed
			{
				eaPush(&ret, pInternalSub);
			}
		}

		return ret;
	}
	return NULL;
}


/************************************************************************/
/* Initialization                                                       */
/************************************************************************/

// Initialize the internal subscriptions
void initializeInternalSubscriptions(void)
{
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, parse_InternalSubscription, NULL, NULL, NULL, NULL, NULL);
	objRegisterContainerTypeAddCallback(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, postAdd);
	objRegisterContainerTypeRemoveCallback(GLOBALTYPE_ACCOUNTSERVER_INTERNAL_SUBSCRIPTION, postRemove);
	recreateStashTables();
}

#include "InternalSubs_h_ast.c"