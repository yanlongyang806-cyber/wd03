/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbOfflining.h"
#include "GlobalTypes.h"
#include "objIndex.h"
#include "error.h"
#include "objContainerIO.h"
#include "timing.h"
#include "ObjectDB.h"
#include "objTransactions.h"
#include "logging.h"
#include "AccountStub.h"
#include "dbContainerRestore.h"
#include "AuctionLotEnums.h"
#include "GlobalTypes_h_ast.h"

#include "AutoGen/AuctionLotEnums_h_ast.h"

#include "autogen/objIndex_c_ast.h"

#include "autogen/dbOfflining_c_ast.h"

char gOfflineConfigPath[MAX_PATH] = "defs/config/OfflineConfig.def";
AUTO_CMD_STRING(gOfflineConfigPath, OfflineConfigPath);

char gOfflineConfigBinPath[MAX_PATH] = "defs/OfflineConfig.bin";
AUTO_CMD_STRING(gOfflineConfigBinPath, OfflineConfigBinPath);

AUTO_STRUCT;
typedef struct OfflineConfig
{
	bool allowOffliningOfAccountSharedBank;
	bool lazyRestoreAccountSharedBank;
	bool allowPartialOffliningOfAccounts;	AST(DEFAULT(1))
} OfflineConfig;

OfflineConfig gOfflineConfig;

bool LazyRestoreSharedBank()
{
	return gOfflineConfig.lazyRestoreAccountSharedBank;
}

void LoadOfflineConfig()
{
	StructInit(parse_OfflineConfig, &gOfflineConfig);
	ParserLoadFiles(NULL, gOfflineConfigPath, gOfflineConfigBinPath, PARSER_OPTIONALFLAG, parse_OfflineConfig, &gOfflineConfig);
}

extern ObjectIndex *gAccountID_idx;

static int giTotalOfflineCharacters = 0;

static U32 giOutstandingAccountStubOperations = 0;
static U32 giContainersToOffline = 0;
static U32 giTotalEntityPlayersToOffline = 0;
static U32 giEntityPlayersOfflined = 0;
static U32 giOfflineFailures = 0;
static U32 giOfflineContainersToCleanup = 0;
static U32 giTotalOfflineContainersToCleanup = 0;
static U32 giContainersCleanedUp = 0;

ContainerID *gPlayersToRemove = NULL;

ContainerRef **gContainersToRemove = NULL;

static U32 shortenOffliningThresholdTests = 0; //Instead of multiplying the offlining thresholds by days, multiply by minutes
AUTO_CMD_INT(shortenOffliningThresholdTests,DebugShortenOffliningThresholdTests) ACMD_COMMANDLINE;

static U32 overrideOffliningTime = 0; //Use this SecsSince2000 time for offlining, instead of now
AUTO_CMD_INT(overrideOffliningTime,DebugOverrideOffliningTime) ACMD_COMMANDLINE;

static void OfflineEntityPlayerCB(TransactionReturnVal *returnVal, ContainerRef **ppRefs)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		{
			int i;
			int failureCase = -1;
			for(i = 0; i < returnVal->iNumBaseTransactions; ++i)
			{
				if(returnVal->pBaseReturnVals[i].eOutcome == TRANSACTION_OUTCOME_FAILURE)
				{
					failureCase = i;
					break;
				}
			}
			servLog(LOG_CONTAINER, "OfflineFailure", "ContainerType %s ContainerID %u Reason \"%s\"", GlobalTypeToName(ppRefs[0]->containerType), ppRefs[0]->containerID, failureCase == -1 ? "Unknown" : NULL_TO_EMPTY(returnVal->pBaseReturnVals[failureCase].returnString));
			giOfflineFailures++;
			break;
		}
	case TRANSACTION_OUTCOME_SUCCESS:
		servLog(LOG_CONTAINER, "OfflineSuccess", "ContainerType %s ContainerID %u", GlobalTypeToName(ppRefs[0]->containerType), ppRefs[0]->containerID);
		break;
	}
	giEntityPlayersOfflined++;
	eaDestroyStruct(&ppRefs, parse_ContainerRef);
}

static void OfflineAccountWideContainerCB(TransactionReturnVal *returnVal, ContainerRef *pRef)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		servLog(LOG_CONTAINER, "OfflineFailure", "ContainerType %s ContainerID %u", GlobalTypeToName(pRef->containerType), pRef->containerID);
		giOfflineFailures++;
		break;
	case TRANSACTION_OUTCOME_SUCCESS:
		servLog(LOG_CONTAINER, "OfflineSuccess", "ContainerType %s ContainerID %u", GlobalTypeToName(pRef->containerType), pRef->containerID);
		break;
	}
	StructDestroy(parse_ContainerRef, pRef);
}

static bool VerifyDependentContainersExist(GlobalType containerType, ContainerID containerID, ContainerRef **ppRefs)
{
	EARRAY_FOREACH_BEGIN(ppRefs, i);
	{
		// Don't offline an EntityPlayer with references to non-existent EntitySavedPets
		if(!objDoesContainerExist(ppRefs[i]->containerType, ppRefs[i]->containerID))
		{
			ErrorOrAlert("OBJECTDB.OFFLINING.FAILURE", "Trying to offline " CON_PRINTF_STR " but it is missing dependent container " CON_PRINTF_STR ".", CON_PRINTF_ARG(containerType, containerID), CON_PRINTF_ARG(ppRefs[i]->containerType, ppRefs[i]->containerID));
			return false;
		}
	}
	EARRAY_FOREACH_END;
	return true;
}

static void OfflineEntityPlayer(ContainerID conID)
{
	Container *container = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, conID, false, true, true);
	ContainerRef *pRef = StructCreate(parse_ContainerRef);
	ContainerRef **ppRefs = NULL;
	int freeContainerData = false;

	pRef->containerID = conID;
	pRef->containerType = GLOBALTYPE_ENTITYPLAYER;

	if(!container)
	{
		AssertOrAlert("OFFLINING", "Trying to offline nonexistent container.");
		giOfflineFailures++;
		return;
	}

	while (InterlockedIncrement(&container->updateLocked) > 1)
	{
		InterlockedDecrement(&container->updateLocked);
		Sleep(0);
	}

	if(!container->containerData && container->fileData)
	{
		objUnpackContainerEx(container->containerSchema, container, &container->containerData, true, false, true);
		freeContainerData = true;
	}

	if(!container->containerData)
	{
		AssertOrAlert("OFFLINING.UNPACK.FAILURE", "EntityPlayer[%u] has no containerData to scan for dependent containers. %s", conID, container->fileData ? "" : "It also has no fileData.");
		InterlockedDecrement(&container->updateLocked);
		objUnlockContainer(&container);
		giOfflineFailures++;
		return;
	}

	eaPush(&ppRefs, pRef);

	// This check MUST be non-recursive - otherwise it would unlock the container and NULL the pointer
	objGetDependentContainers(GLOBALTYPE_ENTITYPLAYER, &container, &ppRefs, false);

	if(VerifyDependentContainersExist(pRef->containerType, pRef->containerID, ppRefs))
	{
		++giOutstandingAccountStubOperations;
		giContainersToOffline += eaSize(&ppRefs);
		objRequestOfflineEntityPlayerAndDependents(objCreateManagedReturnVal(OfflineEntityPlayerCB, ppRefs), container->header->accountId, pRef->containerType, pRef->containerID, container->header, ppRefs, GetAppGlobalType(), GetAppGlobalID());
	}
	else
	{
		giOfflineFailures++;
	}

	if(freeContainerData)
	{
		void *containerData = container->containerData;
		container->containerData = NULL;

		objDeInitContainerObject(container->containerSchema, containerData);
		objDestroyContainerObject(container->containerSchema, containerData);
		containerData = NULL;
	}

	InterlockedDecrement(&container->updateLocked);
	objUnlockContainer(&container);
}

static void OfflineAccountWideContainer(ContainerRef *pRef)
{
	Container *container = objGetContainerEx(pRef->containerType, pRef->containerID, false, true, true);

	if(!container)
	{
		//Account wide containers are not required to exist, so this is allowed to fail.
		return;
	}

	++giOutstandingAccountStubOperations;
	++giContainersToOffline;
	objRequestOfflineAccountWideContainer(objCreateManagedReturnVal(OfflineAccountWideContainerCB, pRef), pRef->containerID, pRef->containerType, GetAppGlobalType(), GetAppGlobalID());

	objUnlockContainer(&container);
}

static void PrebuildAuctionLotStatusCB(Container *con, StashTable auctionLotStatus)
{
	void *conData = con->containerData;
	ContainerID ownerID = 0;
	int price = 0;
	int state = 0;

	if (!conData)
	{
		return;
	}

	objPathGetInt(".Ownerid", con->containerSchema->classParse, conData, &ownerID);

	if (!ownerID)
	{
		return;
	}

	objPathGetInt(".Price", con->containerSchema->classParse, conData, &price);
	objPathGetInt(".State", con->containerSchema->classParse, conData, &state);

	// If the lot represents an actual auction lot (not storage for item mail), and has a non-zero price, the corresponding player 
	if (state != ALS_Mailed || price != 0)
	{
		stashIntAddInt(auctionLotStatus, ownerID, 1, false);
	}
}

static StashTable PrebuildAuctionLotStatus(void)
{
	StashTable auctionLotStatus = stashTableCreateInt(1024);
	ForEachContainerOfType(GLOBALTYPE_AUCTIONLOT, PrebuildAuctionLotStatusCB, auctionLotStatus, true);
	return auctionLotStatus;
}

typedef struct PlayerToOffline
{
	ContainerID containerID;
	U32 level;
	U32 lastPlayed;
	U32 created;
} PlayerToOffline;

static bool ShouldEntityPlayerBeOfflined(PlayerToOffline *player, U32 iOfflineThreshold, U32 iLowLevelOfflineThreshold, StashTable auctionLotStatus)
{
	// If it's under the low level threshold...
	if(player->level <= gDatabaseConfig.iLowLevelThreshold)
	{
		// ...don't offline it if it has played or been created within the low-level threshold.
		if(player->lastPlayed >= iLowLevelOfflineThreshold || player->created >= iLowLevelOfflineThreshold)
		{
			return false;
		}
	}
	// Otherwise, ...
	else
	{
		// ...don't offline it if it has played or been created within the offline threshold.
		if(player->lastPlayed >= iOfflineThreshold || player->created >= iOfflineThreshold)
		{
			return false;
		}
	}

	// If it has active auction lots, don't offline it.
	if (stashIntFindInt(auctionLotStatus, player->containerID, NULL))
	{
		return false;
	}

	return true;
}

static void FlagAccountForOfflining(U32 accountID, PlayerToOffline **ppPlayersForAccount, ContainerID **ppPlayersToRemove, ContainerRef ***pppContainersToRemove, U32 iOfflineThreshold, U32 iLowLevelOfflineThreshold, StashTable auctionLotStatus)
{
	bool bOfflineSomeCharacters = false;
	bool bOfflineAllCharacters = true; // only used if allowPartialOffliningOfAccounts is true
	PlayerToOffline **localPlayersToRemove = NULL;

	EARRAY_FOREACH_BEGIN(ppPlayersForAccount, i);
	{
		if(!accountID)
		{
			bOfflineSomeCharacters = false;
			break;
		}

		if(ShouldEntityPlayerBeOfflined(ppPlayersForAccount[i], iOfflineThreshold, iLowLevelOfflineThreshold, auctionLotStatus))
		{
			eaPush(&localPlayersToRemove, ppPlayersForAccount[i]);
			bOfflineSomeCharacters = true;
		}
		else
		{
			bOfflineAllCharacters = false;
			if(!gOfflineConfig.allowPartialOffliningOfAccounts)
				break;
		}
	}
	EARRAY_FOREACH_END;

	if(bOfflineSomeCharacters)
	{
		if(gOfflineConfig.allowPartialOffliningOfAccounts || bOfflineAllCharacters)
		{
			EARRAY_FOREACH_BEGIN(localPlayersToRemove, i);
			{
				eaiPush(ppPlayersToRemove, localPlayersToRemove[i]->containerID);
			}
			EARRAY_FOREACH_END;
		}
		if(gOfflineConfig.allowOffliningOfAccountSharedBank && bOfflineAllCharacters)
		{
			ContainerRef *ref = StructCreate(parse_ContainerRef);
			ref->containerID = accountID;
			ref->containerType = GLOBALTYPE_ENTITYSHAREDBANK;
			eaPush(pppContainersToRemove, ref);
		}
	}

	if(localPlayersToRemove)
		eaDestroy(&localPlayersToRemove);
}

void CreateOffliningQueue(bool skipOfflining)
{
	ObjectIndexIterator iter = {0};
	ObjectIndexHeader *header = NULL;
	ContainerID *allContainers = NULL;
	PlayerToOffline **playersForAccount = NULL;
	ContainerID lastAccountID = 0;
	StashTable auctionLotStatus = NULL;
	U32 iTime = overrideOffliningTime ? overrideOffliningTime : timeSecondsSince2000();
	U32 iThresholdMultiplier = (shortenOffliningThresholdTests ? SECONDS_PER_MINUTE : SECONDS_PER_DAY);
	U32 iOfflineThreshold = iTime - (gDatabaseConfig.iOfflineThreshold * iThresholdMultiplier);
	U32 iLowLevelOfflineThreshold = iTime - (gDatabaseConfig.iLowLevelOfflineThreshold * iThresholdMultiplier);
	U32 iOfflineThrottle = gDatabaseConfig.iOfflineThrottle;

	PERFINFO_AUTO_START_FUNC();

	LoadOfflineConfig();

	if(skipOfflining)
		iOfflineThrottle = 0;

	giContainersToOffline = 0;
	giTotalEntityPlayersToOffline = 0;
	giOutstandingAccountStubOperations = 0;
	giEntityPlayersOfflined = 0;
	giOfflineFailures = 0;

	// Before doing anything else, prebuild a StashTable containing entries for player IDs that have outstanding auction lots
	auctionLotStatus = PrebuildAuctionLotStatus();

	// Now walk the index of players by account ID and build a list of all players, grouped by account
	objIndexObtainReadLock(gAccountID_idx);
	if(!objIndexGetIterator(gAccountID_idx, &iter, ITERATE_FORWARD))
	{
		objIndexReleaseReadLock(gAccountID_idx);
		PERFINFO_AUTO_STOP();
		return;
	}

	while ((header = (ObjectIndexHeader *)objIndexGetNext(&iter)))
	{
		ea32Push(&allContainers, header->containerId);
	}
	
	objIndexReleaseReadLock(gAccountID_idx);

	// Walk the containers, accumulating entries for each player container
	PERFINFO_AUTO_START("Finding Stale Accounts", 1);
	EARRAY_INT_CONST_FOREACH_BEGIN(allContainers, i, n);
	{
		Container *con = NULL;
		PlayerToOffline *player = NULL;
		U32 accountID = 0;

		if ((U32)eaiSize(&gPlayersToRemove) >= iOfflineThrottle)
		{
			break;
		}

		// Get and lock the container
		con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, allContainers[i], false, false, true);

		if (!con)
		{
			continue;
		}

		// Collect the level, last played time, created time, and account ID of the player
		player = callocStruct(PlayerToOffline);
		player->containerID = allContainers[i];
		player->level = con->header->level;
		player->lastPlayed = con->header->lastPlayedTime;
		player->created = con->header->createdTime;

		accountID = con->header->accountId;
		objUnlockContainer(&con);

		// If the container we just got was from a different account, process the existing list with the last account ID we had, then clear it
		if (eaSize(&playersForAccount) && accountID != lastAccountID)
		{
			FlagAccountForOfflining(lastAccountID, playersForAccount, &gPlayersToRemove, &gContainersToRemove, iOfflineThreshold, iLowLevelOfflineThreshold, auctionLotStatus);
			eaClearEx(&playersForAccount, NULL);
		}

		// Begin/continue building a list for this account
		eaPush(&playersForAccount, player);
		lastAccountID = accountID;
	}
	EARRAY_FOREACH_END;

	// If there are any remaining stragglers, process them as well
	if(eaSize(&playersForAccount) && ((U32)eaiSize(&gPlayersToRemove) < iOfflineThrottle))
	{
		FlagAccountForOfflining(lastAccountID, playersForAccount, &gPlayersToRemove, &gContainersToRemove, iOfflineThreshold, iLowLevelOfflineThreshold, auctionLotStatus);
	}

	eaDestroyEx(&playersForAccount, NULL);
	eaiDestroy(&allContainers);
	stashTableDestroy(auctionLotStatus);

	PERFINFO_AUTO_STOP();

	giTotalEntityPlayersToOffline = eaiSize(&gPlayersToRemove);
	PERFINFO_AUTO_STOP();
}

void ProcessOffliningQueue()
{
	int count = gDatabaseConfig.iOfflineFrameThrottle;

	if(!eaiSize(&gPlayersToRemove) && !eaSize(&gContainersToRemove))
		return;

	if(count <= 0)
		count = 1;

	PERFINFO_AUTO_START("Offlining Accounts", 1);

	while(eaiSize(&gPlayersToRemove) && count > 0)
	{
		ContainerID id = eaiPop(&gPlayersToRemove);
		OfflineEntityPlayer(id);
		--count;
	}

	while(eaSize(&gContainersToRemove) && count > 0)
	{
		ContainerRef *ref = eaPop(&gContainersToRemove);
		OfflineAccountWideContainer(ref);
		--count;
	}

	if(!eaiSize(&gPlayersToRemove))
		eaiDestroy(&gPlayersToRemove);

	if(gContainersToRemove && !eaSize(&gContainersToRemove))
		eaDestroy(&gContainersToRemove);

	PERFINFO_AUTO_STOP();
}

static void RemoveFromOfflineEntityPlayerCB(TransactionReturnVal *returnVal, ContainerRef **ppRefs)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		servLog(LOG_CONTAINER, "RemoveFromOfflineFailure", "ContainerType %s ContainerID %u", GlobalTypeToName(ppRefs[0]->containerType), ppRefs[0]->containerID);
		break;
	case TRANSACTION_OUTCOME_SUCCESS:
		servLog(LOG_CONTAINER, "RemoveFromOfflineSuccess", "ContainerType %s ContainerID %u", GlobalTypeToName(ppRefs[0]->containerType), ppRefs[0]->containerID);
		break;
	}
	giContainersCleanedUp++;
	eaDestroyStruct(&ppRefs, parse_ContainerRef);
}

static void RemoveFromOfflineAccountWideContainerCB(TransactionReturnVal *returnVal, ContainerRef *pRef)
{
	switch(returnVal->eOutcome)
	{
	case TRANSACTION_OUTCOME_FAILURE:
		servLog(LOG_CONTAINER, "RemoveFromOfflineFailure", "ContainerType %s ContainerID %u", GlobalTypeToName(pRef->containerType), pRef->containerID);
		break;
	case TRANSACTION_OUTCOME_SUCCESS:
		servLog(LOG_CONTAINER, "RemoveFromOfflineSuccess", "ContainerType %s ContainerID %u", GlobalTypeToName(pRef->containerType), pRef->containerID);
		break;
	}
	giContainersCleanedUp++;
	StructDestroy(parse_ContainerRef, pRef);
}

typedef struct OfflineHoggCleanupStruct
{
	ContainerRef *pRef;
	ContainerID accountId;
} OfflineHoggCleanupStruct;

static void RemoveFromOfflineEntityPlayer(OfflineHoggCleanupStruct *pCleanupRef)
{
	ContainerRef **ppRefs = NULL;
	eaPush(&ppRefs, pCleanupRef->pRef);
	GetOfflineDependentContainers(gContainerSource.offlinePath, pCleanupRef->pRef->containerType, pCleanupRef->pRef->containerID, &ppRefs);
	objRequestRemoveOfflineEntityPlayerAndDependents(objCreateManagedReturnVal(RemoveFromOfflineEntityPlayerCB, ppRefs), pCleanupRef->accountId, pCleanupRef->pRef->containerType, pCleanupRef->pRef->containerID, ppRefs, GetAppGlobalType(), GetAppGlobalID());
	giOfflineContainersToCleanup += eaSize(&ppRefs);
	++giOutstandingAccountStubOperations;
}

static void RemoveFromOfflineAccountWideContainer(OfflineHoggCleanupStruct *pCleanupRef)
{
	objRequestRemoveAccountWideContainerFromOffline(objCreateManagedReturnVal(RemoveFromOfflineAccountWideContainerCB, pCleanupRef->pRef), pCleanupRef->accountId, pCleanupRef->pRef->containerType, GetAppGlobalType(), GetAppGlobalID());
	++giOfflineContainersToCleanup;
	++giOutstandingAccountStubOperations;
}

U32 TotalEntityPlayersToOffline()
{
	return giTotalEntityPlayersToOffline;
}

U32 EntityPlayersLeftToOffline()
{
	return giContainersToOffline;
}

U32 OfflineFailures()
{
	return giOfflineFailures;
}

U32 EntityPlayersOfflined()
{
	return giEntityPlayersOfflined;
}

bool IsMovingToOfflineHoggDone()
{
	return (giOutstandingAccountStubOperations == 0 && giContainersToOffline == 0 && eaiSize(&gPlayersToRemove) == 0 && eaSize(&gContainersToRemove) == 0);
}

OfflineHoggCleanupStruct **gContainersToCleanup = NULL;

typedef struct OfflineCleanupRecord
{
	U32 iOfflineThrottle;
	OfflineHoggCleanupStruct ***pppContainersToCleanup;
} OfflineCleanupRecord;

bool ContinueCleanupEachAccountCB(OfflineCleanupRecord *record)
{
	return (U32)eaSize(record->pppContainersToCleanup) < record->iOfflineThrottle;
}

void QueueCleanupForEachAccountCB(Container *con, OfflineCleanupRecord *record)
{
	AccountStub *stub = con->containerData;
	if(stub)
	{
		EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, j);
		{
			if(stub->eaOfflineCharacters[j] && stub->eaOfflineCharacters[j]->restored)
			{
				OfflineHoggCleanupStruct *pCleanupRef = calloc(1, sizeof(OfflineHoggCleanupStruct));
				ContainerRef *pRef = StructCreate(parse_ContainerRef);
				pRef->containerID = stub->eaOfflineCharacters[j]->iContainerID;
				pRef->containerType = GLOBALTYPE_ENTITYPLAYER;
				pCleanupRef->accountId = stub->iAccountID;
				pCleanupRef->pRef = pRef;
				eaPush(record->pppContainersToCleanup, pCleanupRef);
			}
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(stub->eaOfflineAccountWideContainers, j);
		{
			if(stub->eaOfflineAccountWideContainers[j] && stub->eaOfflineAccountWideContainers[j]->restored)
			{
				OfflineHoggCleanupStruct *pCleanupRef = calloc(1, sizeof(OfflineHoggCleanupStruct));
				ContainerRef *pRef = StructCreate(parse_ContainerRef);
				pRef->containerID = stub->iAccountID;
				pRef->containerType = stub->eaOfflineAccountWideContainers[j]->containerType;
				pCleanupRef->accountId = stub->iAccountID;
				pCleanupRef->pRef = pRef;
				eaPush(record->pppContainersToCleanup, pCleanupRef);
			}
		}
		EARRAY_FOREACH_END;
	}
}

void QueueOfflineHoggCleanUp(bool skipOfflining)
{
	ContainerStore *accountStubStore = objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTSTUB);
	U32 iOfflineThrottle = gDatabaseConfig.iOfflineThrottle;
	OfflineCleanupRecord record = {0};
	if(!accountStubStore)
		return;

	PERFINFO_AUTO_START_FUNC();

	giOutstandingAccountStubOperations = 0;
	giOfflineContainersToCleanup = 0;
	giTotalOfflineContainersToCleanup = 0;
	giContainersCleanedUp = 0;

	if(skipOfflining)
		iOfflineThrottle = 0;

	PERFINFO_AUTO_START("Finding Restored EntityPlayers", 1);

	record.iOfflineThrottle = iOfflineThrottle;
	record.pppContainersToCleanup = &gContainersToCleanup;
	ForEachContainerOfTypeEx(GLOBALTYPE_ACCOUNTSTUB, QueueCleanupForEachAccountCB, ContinueCleanupEachAccountCB, &record, true, false);

	PERFINFO_AUTO_STOP();

	giTotalOfflineContainersToCleanup = eaSize(&gContainersToCleanup);

	PERFINFO_AUTO_STOP();
}

void ProcessCleanUpQueue()
{
	int count = gDatabaseConfig.iOfflineFrameThrottle;

	if(!eaSize(&gContainersToCleanup))
		return;

	if(count <= 0)
		count = 1;

	PERFINFO_AUTO_START("Cleaning up offline accounts", 1);

	while(eaSize(&gContainersToCleanup) && count > 0)
	{
		OfflineHoggCleanupStruct *offline = eaPop(&gContainersToCleanup);
		if(offline->pRef->containerType == GLOBALTYPE_ENTITYPLAYER)
			RemoveFromOfflineEntityPlayer(offline);
		else
			RemoveFromOfflineAccountWideContainer(offline);
		free(offline);
		--count;
	}

	if(!eaSize(&gContainersToCleanup))
		eaDestroy(&gContainersToCleanup);

	PERFINFO_AUTO_STOP();
}

U32 TotalOfflineContainersToCleanup()
{
	return giTotalOfflineContainersToCleanup;
}

U32 OfflineContainersLeftToCleanup()
{
	return giOfflineContainersToCleanup;
}

U32 ContainersCleanedUp()
{
	return giContainersCleanedUp;
}

bool IsCleaningUpOfflineHoggDone()
{
	return (giOutstandingAccountStubOperations == 0 && giOfflineContainersToCleanup == 0 && eaSize(&gContainersToCleanup) == 0);
}

int GetTotalOfflineCharacters()
{
	return giTotalOfflineCharacters;
}

void IncrementTotalOfflineCharacters()
{
	++giTotalOfflineCharacters;
}

void DecrementTotalOfflineCharacters()
{
	--giTotalOfflineCharacters;
}

void InitializeTotalOfflineCharactersCB(Container *con, void *userData)
{
	AccountStub *stub = con->containerData;

	EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, j);
	{
		if (stub->eaOfflineCharacters[j] && !stub->eaOfflineCharacters[j]->restored)
		{
			++giTotalOfflineCharacters;
		}
	}
	EARRAY_FOREACH_END;
}

void InitializeTotalOfflineCharacters()
{
	PERFINFO_AUTO_START_FUNC();
	giTotalOfflineCharacters = 0;
	ForEachContainerOfType(GLOBALTYPE_ACCOUNTSTUB, InitializeTotalOfflineCharactersCB, NULL, true);
	PERFINFO_AUTO_STOP();
}

void DecrementContainersToOffline(void)
{
	--giContainersToOffline;
}

void DecrementOutstandingAccountStubOperations(void)
{
	--giOutstandingAccountStubOperations;
}

void DecrementOfflineContainersToCleanup(void)
{
	--giOfflineContainersToCleanup;
}

#include "autogen/dbOfflining_c_ast.c"
#include "AutoGen/AuctionLotEnums_h_ast.c"
