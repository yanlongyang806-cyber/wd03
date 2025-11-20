/***************************************************************************



***************************************************************************/

#include "objDCC.h"
#include "logging.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "GlobalTypes.h"
#include "objTransactions.h"
#include "TransactionOutcomes.h"
#include "Alerts.h"
#include "Array.h"
#include "GlobalTypes_h_ast.h"

static U32 gDCCStartupTime = 0;
// Adds an artificial delay to the DCC. We will wait gCachedDeleteDelayMinutes before beginning to purge the DCC
U32 gCachedDeleteDelayMinutes = 0;
AUTO_CMD_INT(gCachedDeleteDelayMinutes, CachedDeleteDelayMinutes) ACMD_CMDLINE;

// Sets how many deleted characters to process per frame.
U32 gCachedDeleteRateLimit = 1;
AUTO_CMD_INT(gCachedDeleteRateLimit, CachedDeleteRateLimit) ACMD_CMDLINE;

// Sets how many deleted characters to process per frame.
U32 gCachedDeleteWalkLimit = 100;
AUTO_CMD_INT(gCachedDeleteWalkLimit, CachedDeleteWalkLimit) ACMD_CMDLINE;

static void CachedDeleteFinalize_CB(TransactionReturnVal *returnVal, ContainerRef *ref)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
	{
		ContainerStore *base = objFindContainerStoreFromType(ref->containerType);
		int i;
		char* errStr = NULL;

		for(i = 0; i < returnVal->iNumBaseTransactions; i++)
		{
			BaseTransactionReturnVal* baseRetVal = &returnVal->pBaseReturnVals[i];
			if(baseRetVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
				continue;
			else if(baseRetVal->eOutcome == TRANSACTION_OUTCOME_FAILURE)
			{
				errStr = baseRetVal->returnString;
				break;
			}
			
			devassertmsgf(0, "Unhandled transaction outcome %d", baseRetVal->eOutcome);
		}

		servLog(LOG_CONTAINER, "DeleteCacheExpirationFailure", "ContainerType %s ContainerID %u (%s)", GlobalTypeToName(ref->containerType), ref->containerID, errStr);
		ErrorOrAlert("OBJECTDB.DCCEXPIRATIONFAILURE", "Unable to expire %s[%u] from the DCC (%s)", GlobalTypeToName(ref->containerType), ref->containerID, errStr);
		// Removing failed Expiration from cleanup queue
		objFixDeletedCleanupTables(base, ref->containerID, false, false);
		break;
	}
	case TRANSACTION_OUTCOME_SUCCESS:
		servLog(LOG_CONTAINER, "DeleteCacheExpirationSuccess", "ContainerType %s ContainerID %u", GlobalTypeToName(ref->containerType), ref->containerID);
		break;
	}
	StructDestroy(parse_ContainerRef, ref);
}

U32 GetNextCachedDeleteExpireInterval(GlobalType eType)
{
	U32 now = timeSecondsSince2000();
	ContainerStore *store = objFindContainerStoreFromType(eType);
	DeletedContainerQueueEntry *entry;
	DeletedContainerQueueEntry **pentry;
	int queueIndex = 0;
	U32 destroyTime = 0;

	if (!store || !store->containerSchema)
	{
		return 0;
	}

	objLockContainerStoreDeleted_ReadOnly(store);
	if (!store->deletedContainerQueue)
	{
		objUnlockContainerStoreDeleted_ReadOnly(store);
		return 0;
	}

	pentry = (DeletedContainerQueueEntry**)arrayGetNextItem(store->deletedContainerQueue, &queueIndex);
	
	if (pentry && (entry = *pentry))
		destroyTime = dccGetDestroyTime(entry);

	objUnlockContainerStoreDeleted_ReadOnly(store);

	if (destroyTime > now)
		return destroyTime - now;
	else
		return 0;
}

void ExpireCachedDeletes(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	int i;
	U32 now = timeSecondsSince2000();

	if(gCachedDeleteDelayMinutes)
	{
		if(!gDCCStartupTime)
			gDCCStartupTime = now;

		if(now < gDCCStartupTime + (gCachedDeleteDelayMinutes * SECONDS_PER_MINUTE))
		{
			return;
		}
		else
		{
			gCachedDeleteDelayMinutes = 0;
		}
	}

	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		int queueIndex = 0;
		U32 countWalked = 0;
		U32 count = 0;
		DeletedContainerQueueEntry *entry;
		DeletedContainerQueueEntry **pentry;
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (!store || !store->containerSchema)
		{
			continue;
		}

		objLockContainerStoreDeleted_ReadOnly(store);
		if (!store->deletedContainerQueue)
		{
			objUnlockContainerStoreDeleted_ReadOnly(store);
			continue;
		}

		pentry = (DeletedContainerQueueEntry**)arrayGetNextItem(store->deletedContainerQueue, &queueIndex);
		while((count < gCachedDeleteRateLimit) && (countWalked < gCachedDeleteWalkLimit) && pentry && (entry = *pentry) && (now > dccGetDestroyTime(entry)))
		{
			objUnlockContainerStoreDeleted_ReadOnly(store);

			if(!entry->queuedForDelete)
			{
				int id = entry->containerID;
				Container *container = objGetDeletedContainerEx(i, id, true, false, true);

				if(container)
				{
					ContainerRef **ppRefs = NULL;
					ContainerRef *ref = StructCreate(parse_ContainerRef);
					bool recursive = entry->iDeletedTime != 1;

					ref->containerID = id;
					ref->containerType = i;
					
					objGetDependentContainers(i, &container, &ppRefs, recursive);
					if (!recursive) objUnlockContainer(&container);
					objRequestDependentContainersDestroy(objCreateManagedReturnVal(CachedDeleteFinalize_CB, ref), ref, ppRefs, objServerType(), objServerID());
					eaDestroyStruct(&ppRefs, parse_ContainerRef);
					entry->queuedForDelete = true;
					++count;
				}
				else
				{
					objFixDeletedCleanupTables(store, id, false, true);
					AssertOrAlert("OBJECTDB.DCC", "%s %u was not correctly removed from DCC queue.", GlobalTypeToName(i), id);
				}
			}

			countWalked++;
			objLockContainerStoreDeleted_ReadOnly(store);
			pentry = (DeletedContainerQueueEntry**)arrayGetNextItem(store->deletedContainerQueue, &queueIndex);
		}
		objUnlockContainerStoreDeleted_ReadOnly(store);
	}
}

