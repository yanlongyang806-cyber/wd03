/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "dbGenericDatabaseThreads.h"
#include "dbSubscribe.h"
#include "ObjectDB.h"

#include "accountnet.h"
#include "earray.h"
#include "error.h"
#include "file.h"
#include "FloatAverager.h"
#include "GlobalStateMachine.h"
#include "GlobalTypes.h"
#include "LoginCommon.h"
#include "logging.h"
#include "loggingEnums.h"
#include "MultiWorkerThread.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "ScratchStack.h"
#include "StashTable.h"
#include "structNet.h"
#include "ThreadSafeMemoryPool.h"
#include "timing.h"
#include "timing_profiler.h"
#include "utilitiesLib.h"
#include "wininclude.h"
#include "WorkerThread.h"
#include "zutils.h"

#include "dbSubscribe_c_ast.h"
#include "AutoGen/accountnet_h_ast.h"

#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

static bool dbEnableSubscriptionSendTrackers = false;
AUTO_CMD_INT(dbEnableSubscriptionSendTrackers, EnableSubscriptionSendTrackers) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB);

typedef struct SubscriptionSendTracker
{
	U32 fullSubscriptionSendCount;		// Total number of full subscription packets sent out
	U32 fullSubscriptionTargetCount;	// Number of recipients for full packets, some of which are multiplexed by the Transaction Server
	U32 diffSendCount;					// Total number of diff packets sent
	U32 diffSendTargetCount;			// Number of recipients for diff packets, some of which are multiplexed by the Transaction Server
	U32 removeSendCount;				// Total number of remove packets sent
	U32 removeSendTargetCount;			// Number of recipients for remove packets, some of which are multiplexed by the Transaction Server
	U32 unsubscribeCount;				// Total number of remove packets sent
} SubscriptionSendTracker;

SubscriptionSendTracker subscriptionSendTracker[GLOBALTYPE_MAX];

static void dbSubscribe_TrackFullPacket(GlobalType containerType, ContainerRef ***pppRefs)
{
	if(dbEnableSubscriptionSendTrackers)
	{
		InterlockedIncrement(&(subscriptionSendTracker[containerType].fullSubscriptionSendCount));
		InterlockedExchangeAdd(&(subscriptionSendTracker[containerType].fullSubscriptionTargetCount), pppRefs ? eaSize(pppRefs) : 1);
	}
}

static void dbSubscribe_TrackDiffPacket(GlobalType containerType, ContainerRef ***pppRefs)
{
	if(dbEnableSubscriptionSendTrackers)
	{
		InterlockedIncrement(&(subscriptionSendTracker[containerType].diffSendCount));
		InterlockedExchangeAdd(&(subscriptionSendTracker[containerType].diffSendTargetCount), pppRefs ? eaSize(pppRefs) : 1);
	}
}

static void dbSubscribe_TrackRemovePacket(GlobalType containerType, ContainerRef ***pppRefs)
{
	if(dbEnableSubscriptionSendTrackers)
	{
		InterlockedIncrement(&(subscriptionSendTracker[containerType].removeSendCount));
		InterlockedExchangeAdd(&(subscriptionSendTracker[containerType].removeSendTargetCount), pppRefs ? eaSize(pppRefs) : 1);
	}
}

static void dbSubscribe_TrackUnsubscribe(GlobalType containerType)
{
	if(dbEnableSubscriptionSendTrackers)
	{
		InterlockedIncrement(&(subscriptionSendTracker[containerType].unsubscribeCount));
	}
}

void LogSubscriptionSendTrackers(void)
{
	int i;

	for(i = 0; i < GLOBALTYPE_MAX; ++i)
	{
		if(subscriptionSendTracker[i].fullSubscriptionSendCount)
		{
			SubscriptionSendTracker *tracker = &subscriptionSendTracker[i];
			SERVLOG_PAIRS(LOG_OBJECTDB_PERF, "SubscriptionTracker", 
				("ContainerType", "%u", i)
				("ContainerTypeName", "%s", GlobalTypeToName(i))
				("diffSendCount", "%u", tracker->diffSendCount)
				("diffSendTargetCount", "%u", tracker->diffSendTargetCount)
				("fullSubscriptionSendCount", "%u", tracker->fullSubscriptionSendCount)
				("fullSubscriptionTargetCount", "%u", tracker->fullSubscriptionTargetCount)
				("removeSendCount", "%u", tracker->removeSendCount)
				("removeSendTargetCount", "%u", tracker->removeSendTargetCount)
				("unsubscribeCount", "%u", tracker->unsubscribeCount));
		}
	}
}

// Destroy MonitoredContainer structs that are free
static bool dbDestroyMonitoredContainers = true;
AUTO_CMD_INT(dbDestroyMonitoredContainers, DestroyMonitoredContainers) ACMD_CATEGORY(ObjectDB);

// Destroy MonitoringServers structs that are free
static bool dbDestroyMonitoringServers = true;
AUTO_CMD_INT(dbDestroyMonitoringServers, DestroyMonitoringServers) ACMD_CATEGORY(ObjectDB);

// Log each time a subscription is requested or released, including online subscriptions
static bool dbLogSubscriptionChanges = false;
AUTO_CMD_INT(dbLogSubscriptionChanges, LogSubscriptionChanges) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB);

// Always store information for displaying sub counts in profile data
static bool dbAlwaysPerfSubscriptions = true;
AUTO_CMD_INT(dbAlwaysPerfSubscriptions, AlwaysPerfSubscriptions) ACMD_CATEGORY(ObjectDB);

// Configure size of DBSubscriptionThread command queue
static int dbSubscriptionThreadQueueSize = 16384;
AUTO_CMD_INT(dbSubscriptionThreadQueueSize, SubscriptionThreadQueueSize) ACMD_CATEGORY(ObjectDB);

// DEBUG ONLY: sleep for one second when a sub request arrives
static bool dbDebugSleepOnSub = false;
AUTO_CMD_INT(dbDebugSleepOnSub, DebugSleepOnSub) ACMD_HIDE;

static MultiWorkerThreadManager *sSubscriptionThread = NULL;

// The type of SubscriptionAction to perform
AUTO_ENUM;
typedef enum SubscriptionActionType {
	// Use the already-built packet data to populate the cache
	SubAction_CacheFromPacket,

	// Use the packed container data to populate the packet and cache
	SubAction_CacheFromFileData,

	// Use the existing cache data to populate the packet
	SubAction_UseCache,

	// Free the attached CachedSubscriptionCopy object (no packet being sent)
	SubAction_FreeFromCache,

	// Other request that doesn't need cache data (i.e. not a container send)
	SubAction_OtherRequest,

	SubAction_Count,
} SubscriptionActionType;

typedef struct CachedSubscriptionCopy CachedSubscriptionCopy;

typedef struct SubscriptionAction {
	GlobalType con_type;
	ContainerID con_id;

	// The type of action being performed (see above)
	SubscriptionActionType action_type;

	// Each of the following sections are populated if needed per action type:

	// The subscription packet to send (NULL if SubAction_FreeFromCache)
	Packet *pak;
	int pak_data_idx;

	// The associated cached copy (NULL if SubAction_OtherRequest)
	CachedSubscriptionCopy *copy;

	// The container file data (only for SubAction_CacheFromFileData)
	struct {
		void *data;
		ParseTable *pti;
		int packed_len;
		int unpacked_len;
		int header_size;
	} file_data;
} SubscriptionAction;

TSMP_DEFINE(SubscriptionAction);

static CRITICAL_SECTION gSubscriptionTrackingCS;
static CRITICAL_SECTION gSubscriptionCacheCS;

// LRU cache of copied containers for subscription sends

// Max size of subscribed container copy cache for all container types
U32 sSubCacheBaseSize = 10000;
AUTO_CMD_INT(sSubCacheBaseSize, SubCacheBaseSize) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB) ACMD_CALLBACK(ChangeSubCacheBaseSize);

// Max size of subscribed container copy cache specifically for players
U32 sSubCachePlayerSize = 1000;
AUTO_CMD_INT(sSubCachePlayerSize, SubCachePlayerSize) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB) ACMD_CALLBACK(ChangeSubCachePlayerSize);

// Max size of subscribed container copy cache specifically for pets
U32 sSubCachePetSize = 100000;
AUTO_CMD_INT(sSubCachePetSize, SubCachePetSize) ACMD_AUTO_SETTING(ObjectDB, OBJECTDB) ACMD_CALLBACK(ChangeSubCachePetSize);

GlobalType GetCachedSubscriptionCopyType(CachedSubscriptionCopy *copy)
{
	return copy->type;
}

ContainerID GetCachedSubscriptionCopyID(CachedSubscriptionCopy *copy)
{
	return copy->id;
}

TSMP_DEFINE(CachedSubscriptionCopy);

static SubscriptionCache *sSubscriptionCache[GLOBALTYPE_MAXTYPES];

void ChangeSubCacheBaseSize(CMDARGS)
{
	GlobalType eType;
	EnterCriticalSection(&gSubscriptionCacheCS);
	for (eType = GLOBALTYPE_NONE; eType < GLOBALTYPE_MAXTYPES; ++eType)
	{
		if (eType == GLOBALTYPE_ENTITYPLAYER || eType == GLOBALTYPE_ENTITYSAVEDPET) continue;
		if (sSubscriptionCache[eType]) sSubscriptionCache[eType]->max = sSubCacheBaseSize;
	}
	LeaveCriticalSection(&gSubscriptionCacheCS);
}

void ChangeSubCachePlayerSize(CMDARGS)
{
	EnterCriticalSection(&gSubscriptionCacheCS);
	if (sSubscriptionCache[GLOBALTYPE_ENTITYPLAYER])
		sSubscriptionCache[GLOBALTYPE_ENTITYPLAYER]->max = sSubCachePlayerSize;
	LeaveCriticalSection(&gSubscriptionCacheCS);
}

void ChangeSubCachePetSize(CMDARGS)
{
	EnterCriticalSection(&gSubscriptionCacheCS);
	if (sSubscriptionCache[GLOBALTYPE_ENTITYSAVEDPET])
		sSubscriptionCache[GLOBALTYPE_ENTITYSAVEDPET]->max = sSubCachePetSize;
	LeaveCriticalSection(&gSubscriptionCacheCS);
}

static void __forceinline LinkBefore(CachedSubscriptionCopy *p1, CachedSubscriptionCopy *p2)
{
	p1->next = p2;
	p1->prev = p2->prev;
	p1->next->prev = p1;
	if (p1->prev) p1->prev->next = p1;
	p1->cache = p2->cache;
}

static void __forceinline LinkAfter(CachedSubscriptionCopy *p1, CachedSubscriptionCopy *p2)
{
	p1->prev = p2;
	p1->next = p2->next;
	p1->prev->next = p1;
	if (p1->next) p1->next->prev = p1;
	p1->cache = p2->cache;
}

static void __forceinline UnlinkCopy(CachedSubscriptionCopy *p1)
{
	if (p1->prev) p1->prev->next = p1->next;
	if (p1->next) p1->next->prev = p1->prev;
	p1->cache = NULL;
}

// ONLY called from the subscription thread or generic DB threads, or else everything goes all bad
void DestroyCachedSubscription(CachedSubscriptionCopy *copy)
{
	PERFINFO_AUTO_START_FUNC();
	free(copy->pak_data);
	TSMP_FREE(CachedSubscriptionCopy, copy);
	PERFINFO_AUTO_STOP();
}

static SubscriptionAction *DupSubscriptionAction(SubscriptionAction *action)
{
	SubscriptionAction *dup = NULL;
	
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(SubscriptionAction, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	dup = TSMP_ALLOC(SubscriptionAction);
	memcpy(dup, action, sizeof(SubscriptionAction));
	return dup;
}

static void CopyPacketContents(CachedSubscriptionCopy *copy, Packet *pak, int start)
{
	U32 total = 0;
	U8 *payload = NULL;

	total = pktGetSize(pak) - start;
	payload = pktGetEntirePayload(pak, NULL);

	copy->pak_data = malloc(total);
	memcpy(copy->pak_data, payload + start, total);
	copy->len = total;
}

static void *UnzipDataForSubscription(GlobalType type, ContainerID id, void **data, int packed_len, int unpacked_len, int header_size, ParseTable *pti)
{
	U8 *unzipped_data = NULL;
	void *con_data = StructCreateVoid(pti);
	char fakeFileName[MAX_PATH];

	// The presence of packed_len means the file data is actually zipped; if it's not there, the data is so small that it's just text
	if (packed_len)
	{
		unzipped_data = ScratchAlloc(unpacked_len);

		if (unzipData(unzipped_data, &unpacked_len, *data, packed_len))
		{
			log_printf(LOG_CONTAINER, "Failed to decompress " CON_PRINTF_STR, CON_PRINTF_ARG(type, id));
			ScratchFree(unzipped_data);
			unzipped_data = NULL;
		}
	}
	else
	{
		unzipped_data = *data;
	}

	if (unzipped_data)
	{
		sprintf(fakeFileName, "temp: " CON_PRINTF_STR, CON_PRINTF_ARG(type, id));
		ParserReadTextForFile(unzipped_data + header_size, fakeFileName, pti, con_data, PARSER_IGNORE_ALL_UNKNOWN);

		if (packed_len)
			ScratchFree(unzipped_data);
	}

	SAFE_FREE(*data);

	return con_data;
}

static void PopulateFullContainerSend(Packet *pak, void *data, ParseTable *pti)
{
	ParserSend(pti, pak, NULL, data, SENDDIFF_FLAG_FORCEPACKALL, TOK_PERSIST | TOK_SUBSCRIBE, 0, NULL);
}

static void ExecuteSubscriptionAction(SubscriptionAction *action)
{
	static PERFINFO_TYPE *conTypePerfs[GLOBALTYPE_MAXTYPES];
	static PERFINFO_TYPE *subActionPerfs[SubAction_Count];

	PERFINFO_AUTO_START_FUNC();

	if (dbDebugSleepOnSub)
		Sleep(1000);

	if (action->action_type != SubAction_OtherRequest)
		PERFINFO_AUTO_START_STATIC(GlobalTypeToName(action->con_type), &conTypePerfs[action->con_type], 1);

	PERFINFO_AUTO_START_STATIC(StaticDefineInt_FastIntToString(SubscriptionActionTypeEnum, action->action_type), &subActionPerfs[action->action_type], 1);
	switch (action->action_type)
	{
	case SubAction_UseCache:
		// The cache has data, so use it!
		assert(action->copy->pak_data);
		pktSendBytesRaw(action->pak, action->copy->pak_data, action->copy->len);
		break;
	case SubAction_CacheFromFileData:
		// We have to unzip the file data to do a ParserSend, and then populate the cache from the packet
		{
			// This call frees and NULLs action->file_data.data
			void *con_data = UnzipDataForSubscription(
				action->con_type,
				action->con_id,
				&action->file_data.data,
				action->file_data.packed_len,
				action->file_data.unpacked_len,
				action->file_data.header_size,
				action->file_data.pti);

			PopulateFullContainerSend(action->pak, con_data, action->file_data.pti);
			StructDestroyVoid(action->file_data.pti, con_data);
		}
		// Fall through intentionally so that the already-populated packet data gets copied to the cache entry
	case SubAction_CacheFromPacket:
		// Cache the portion of the packet starting from action->pak_data_idx
		CopyPacketContents(action->copy, action->pak, action->pak_data_idx);
		break;
	case SubAction_FreeFromCache:
		// Free the contents of the cache entry, then free the entry itself
		// Return when done, because this doesn't have an associated packet
		DestroyCachedSubscription(action->copy);
		PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_STATIC(action->action_type)
		PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_STATIC(action->con_type) <-- OK because this is not SubAction_OtherRequest
		PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_FUNC()
		return;
		// case SubAction_OtherRequest: this is a request that doesn't have a container to send, so its packet is already populated
	}

	SendPacketThroughTransactionServer(objLocalManager(), &action->pak);

	PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_STATIC(action->action_type)
	if (action->action_type != SubAction_OtherRequest)
		PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_STATIC(action->con_type)

	PERFINFO_AUTO_STOP(); // PERFINFO_AUTO_START_FUNC()
}

static void QueueSubscriptionThreadAction(SubscriptionAction *action)
{
	if (GenericDatabaseThreadIsActive())
	{
		ExecuteSubscriptionAction(action);
	}
	else
	{
		SubscriptionAction *action_copy = DupSubscriptionAction(action);
		mwtQueueInput(sSubscriptionThread, action_copy, true);
	}
}

static void QueueCachedSubCopyDestroy(GWTCmdPacket *packet, CachedSubscriptionCopy *copy)
{
	// If generic DB threads are active, we can't do the free inline, because there may
	// be outstanding requests that are counting on this cache entry
	//
	// Instead, we ask the main thread to queue another request to free this thing
	// separately. The copy is already out of the cache by this point, so we know that
	// it's safe to free.
	if (GenericDatabaseThreadIsActive())
	{
		if (packet)
		{
			QueueFreeSubscribeCacheCopyOnMainThread(packet, copy);
		}
		else
		{
			QueueFreeSubscribeCacheCopyOnGenericDatabaseThreads(copy);
		}
	}
	else
	{
		SubscriptionAction action = {0};

		action.con_type = copy->type;
		action.con_id = copy->id;

		action.action_type = SubAction_FreeFromCache;
		action.copy = copy;
		QueueSubscriptionThreadAction(&action);
	}
}

// must have gSubscriptionCacheCS here
static SubscriptionCache *GetOrCreateSubscriptionCache(GlobalType type)
{
	if (sSubscriptionCache[type])
		return sSubscriptionCache[type];

	sSubscriptionCache[type] = calloc(1, sizeof(SubscriptionCache));

	switch (type)
	{
	case GLOBALTYPE_ENTITYPLAYER:
		sSubscriptionCache[type]->max = sSubCachePlayerSize;
		break;
	case GLOBALTYPE_ENTITYSAVEDPET:
		sSubscriptionCache[type]->max = sSubCachePetSize;
		break;
	default:
		sSubscriptionCache[type]->max = sSubCacheBaseSize;
		break;
	}

	sSubscriptionCache[type]->lookupTable = stashTableCreateInt(sSubscriptionCache[type]->max);
	return sSubscriptionCache[type];
}

// must have gSubscriptionCacheCS here
static CachedSubscriptionCopy *GetCachedSubscription(SubscriptionCache *cache, ContainerID id)
{
	CachedSubscriptionCopy *copy = NULL;
	if (!cache) return NULL;
	stashIntFindPointer(cache->lookupTable, id, &copy);
	return copy;
}

// must have gSubscriptionCacheCS here
static void RemoveCachedSubscription(GWTCmdPacket *packet, CachedSubscriptionCopy *copy)
{
	SubscriptionCache *cache = copy->cache;
	stashIntRemovePointer(cache->lookupTable, copy->id, NULL);

	if (copy == cache->head)
		cache->head = copy->next;
	if (copy == cache->tail)
		cache->tail = copy->prev;

	UnlinkCopy(copy);
	--cache->count;

	QueueCachedSubCopyDestroy(packet, copy);
}

// must have gSubscriptionCacheCS here
static void PruneSubscriptionCache(GWTCmdPacket *packet, SubscriptionCache *cache)
{
	if (cache->count > cache->max)
		RemoveCachedSubscription(packet, cache->tail);
}

// must have gSubscriptionCacheCS here
static void AddCachedSubscription(GWTCmdPacket *packet, SubscriptionCache *cache, CachedSubscriptionCopy *copy)
{
	stashIntAddPointer(cache->lookupTable, copy->id, copy, true);

	if (cache->head)
	{
		LinkBefore(copy, cache->head);
	}
	else
	{
		cache->tail = copy;
		copy->cache = cache;
	}

	cache->head = copy;
	++cache->count;
	PruneSubscriptionCache(packet, cache);
}

// must have gSubscriptionCacheCS here
static void PokeCachedSubscription(CachedSubscriptionCopy *copy)
{
	SubscriptionCache *cache = copy->cache;

	if (copy == cache->head)
		return;

	if (copy == cache->tail)
		cache->tail = copy->prev;

	UnlinkCopy(copy);
	LinkBefore(copy, cache->head);
	cache->head = copy;
}

// Deal with various notifications caused by container updates

typedef struct SubscribePerf {
	char*			reason;
	PERFINFO_TYPE*	pi;
} SubscribePerf;

StashTable stSubscribeReasonToPerf;

AUTO_STRUCT;
typedef struct ContainerSubscription
{
	ContainerRef subscribedServer; AST(KEY)
	char *pSubscribedFields;
	int refCount;
	bool bSent;
	U32 msTimeStart; NO_AST
	SubscribePerf* perf; NO_AST
} ContainerSubscription;

typedef struct MonitoredContainer MonitoredContainer;

AUTO_STRUCT;
typedef struct MonitoredContainer
{
	U32 loginTime;
	ContainerRef containerOwner;
	ContainerRef monitoredContainer;
	ContainerSubscription **ppSubscriptions;
	int refCount;
} MonitoredContainer;

AUTO_STRUCT;
typedef struct MonitoredContainerType
{
	StashTable pMonitoredContainers; NO_AST
	ContainerSubscription **ppOnlineSubscriptions;
	int refCount;
} MonitoredContainerType;

AUTO_STRUCT;
typedef struct ContainerSubscriptionInverse
{
	ContainerRef subscribedContainer; AST(KEY)
		char *pSubscribedFields;
	int refCount;
	U32 msTimeStart; NO_AST
		SubscribePerf* perf; NO_AST
} ContainerSubscriptionInverse;

AUTO_STRUCT;
typedef struct MonitoringServer
{
	ContainerRef monitoringServer;
	ContainerSubscriptionInverse **ppSubscriptions;
	ContainerSubscriptionInverse **ppOnlineSubscriptions;
	int refCount;
} MonitoringServer;

AUTO_STRUCT;
typedef struct MonitoringServerType
{
	StashTable pMonitoringServers; //NO_AST ???
} MonitoringServerType;

TSMP_DEFINE(ContainerSubscription);
TSMP_DEFINE(ContainerSubscriptionInverse);
TSMP_DEFINE(MonitoredContainer);
TSMP_DEFINE(MonitoringServer);

MonitoredContainerType monitoredTypes[GLOBALTYPE_MAXTYPES];
MonitoringServerType monitoringTypes[GLOBALTYPE_MAXTYPES];

void DestroyMonitoredContainerStruct(MonitoredContainer *pMonitored)
{
	devassert(eaSize(&pMonitored->ppSubscriptions) == 0);
	StructDestroy(parse_MonitoredContainer, pMonitored);
}

void DestroyMonitoringServerStruct(MonitoringServer *pMonitoring)
{
	devassert(eaSize(&pMonitoring->ppSubscriptions) == 0);
	devassert(eaSize(&pMonitoring->ppOnlineSubscriptions) == 0);
	StructDestroy(parse_MonitoringServer, pMonitoring);
}

void CleanupMonitoredContainerStruct(MonitoredContainer **pMonitored)
{
	if(dbDestroyMonitoredContainers)
	{
		if(!pMonitored || !*pMonitored)
			return;

		if((*pMonitored)->refCount == 0 && (*pMonitored)->containerOwner.containerType == objServerType() && (*pMonitored)->containerOwner.containerID == objServerID())
		{
			stashIntRemovePointer(monitoredTypes[(*pMonitored)->monitoredContainer.containerType].pMonitoredContainers, (*pMonitored)->monitoredContainer.containerID,NULL);
			DestroyMonitoredContainerStruct(*pMonitored);
			(*pMonitored) = NULL;
		}
	}
}

void CleanupMonitoringServerStruct(MonitoringServer **pMonitoring)
{
	if(dbDestroyMonitoringServers)
	{
		if(!pMonitoring || !*pMonitoring)
			return;

		if(((*pMonitoring)->refCount == 0) && eaSize(&(*pMonitoring)->ppSubscriptions) == 0 && eaSize(&(*pMonitoring)->ppOnlineSubscriptions) == 0)
		{
			stashIntRemovePointer(monitoringTypes[(*pMonitoring)->monitoringServer.containerType].pMonitoringServers, (*pMonitoring)->monitoringServer.containerID,NULL);
			DestroyMonitoringServerStruct(*pMonitoring);
			(*pMonitoring) = NULL;
		}
	}
}

U32 GetSubscriptionRefCountByType(GlobalType eType)
{
	if(eType < 0 || eType > GLOBALTYPE_MAXTYPES)
		return 0;

	return monitoredTypes[eType].refCount;
}

U32 GetOnlineSubscriptionRefCountByType(GlobalType eType)
{
	if(eType < 0 || eType > GLOBALTYPE_MAXTYPES)
		return 0;

	return eaSize(&monitoredTypes[eType].ppOnlineSubscriptions);
}

// must have gSubscriptionTrackingCS here
MonitoredContainer *dbGetMonitoredContainer(GlobalType conType, ContainerID conID)
{
	MonitoredContainer *pMonitored;
	assert(conType > 0 && conType < GLOBALTYPE_MAXTYPES);
	if (!monitoredTypes[conType].pMonitoredContainers)
	{
		monitoredTypes[conType].pMonitoredContainers = stashTableCreateInt(512);
	}
	if (stashIntFindPointer(monitoredTypes[conType].pMonitoredContainers, conID, &pMonitored))
	{
		return pMonitored;
	}
	return NULL;
}

// must have gSubscriptionTrackingCS here
MonitoredContainer *dbAddOrGetMonitoredContainer(GlobalType conType, ContainerID conID)
{
	MonitoredContainer *pMonitored;
	assert(conType > 0 && conType < GLOBALTYPE_MAXTYPES);	
	if(pMonitored = dbGetMonitoredContainer(conType, conID))
		return pMonitored;

	pMonitored = StructCreate(parse_MonitoredContainer);
	eaIndexedEnable(&pMonitored->ppSubscriptions, parse_ContainerSubscription); // Needed because DB doesn't auto create indexed earrays for memory reasons
	pMonitored->monitoredContainer.containerType = conType;
	pMonitored->monitoredContainer.containerID = conID;
	if (GlobalTypeSchemaType(conType) == SCHEMATYPE_PERSISTED)
	{	
		pMonitored->containerOwner.containerType = objServerType();
		pMonitored->containerOwner.containerID = objServerID();
	}
	stashIntAddPointer(monitoredTypes[conType].pMonitoredContainers, conID, pMonitored, false);
	return pMonitored;
}

// must have gSubscriptionTrackingCS here
ContainerSubscription *dbFindOnlineContainersSubscription(GlobalType conType, ContainerSubscription *pSearch)
{
	int index = eaIndexedFind(&monitoredTypes[conType].ppOnlineSubscriptions, pSearch);
	if (index == -1)
	{
		return NULL;
	}
	return monitoredTypes[conType].ppOnlineSubscriptions[index];
}

// must have gSubscriptionTrackingCS here
ContainerSubscription *dbFindContainerSubscription(MonitoredContainer *pMonitored, ContainerSubscription *pSearch)
{
	int index = eaIndexedFind(&pMonitored->ppSubscriptions, pSearch);
	if (index == -1)
	{
		return NULL;
	}
	return pMonitored->ppSubscriptions[index];
}

// must have gSubscriptionTrackingCS here
MonitoringServer *dbGetMonitoringServer(GlobalType conType, ContainerID conID)
{
	MonitoringServer *pMonitoring;
	assert(conType > 0 && conType < GLOBALTYPE_MAXTYPES);
	if (!monitoringTypes[conType].pMonitoringServers)
	{
		monitoringTypes[conType].pMonitoringServers = stashTableCreateInt(512);
	}

	if (stashIntFindPointer(monitoringTypes[conType].pMonitoringServers, conID, &pMonitoring))
	{
		return pMonitoring;
	}
	return NULL;
}

// must have gSubscriptionTrackingCS here
MonitoringServer *dbAddOrGetMonitoringServer(GlobalType conType, ContainerID conID)
{
	MonitoringServer *pMonitoring;
	assert(conType > 0 && conType < GLOBALTYPE_MAXTYPES);
	
	if(pMonitoring = dbGetMonitoringServer(conType, conID))
		return pMonitoring;

	pMonitoring = StructCreate(parse_MonitoringServer);
	eaIndexedEnable(&pMonitoring->ppSubscriptions, parse_ContainerSubscriptionInverse); // Needed because DB doesn't auto create indexed earrays for memory reasons
	pMonitoring->monitoringServer.containerType = conType;
	pMonitoring->monitoringServer.containerID = conID;
	eaIndexedEnable(&pMonitoring->ppOnlineSubscriptions, parse_ContainerSubscriptionInverse); // Needed because DB doesn't auto create indexed earrays for memory reasons
	stashIntAddPointer(monitoringTypes[conType].pMonitoringServers, conID, pMonitoring, false);
	return pMonitoring;
}

// must have gSubscriptionTrackingCS here
int dbFindOnlineMonitoringContainersSubscription(MonitoringServer *pMonitoring, ContainerSubscriptionInverse *pSearch)
{
	int index = -1;
	index = eaIndexedFind(&pMonitoring->ppOnlineSubscriptions, pSearch);

	return index;
}

// must have gSubscriptionTrackingCS here
ContainerSubscriptionInverse *dbFindMonitoringContainerSubscription(MonitoringServer *pMonitoring, ContainerSubscriptionInverse *pSearch)
{
	int index = eaIndexedFind(&pMonitoring->ppSubscriptions, pSearch);
	if (index == -1)
	{
		return NULL;
	}
	return pMonitoring->ppSubscriptions[index];
}

static U32 gTooManySubscriptionsAlertMaxAlerts = 10;
AUTO_CMD_INT(gTooManySubscriptionsAlertMaxAlerts, TooManySubscriptionsAlertMaxAlerts) ACMD_CMDLINE;

static U32 gTooManySubscriptionsAlertThreshold = 10000;
AUTO_CMD_INT(gTooManySubscriptionsAlertThreshold, TooManySubscriptionsAlertThreshold) ACMD_CMDLINE;

static StashTable SubscriptionAlertTracking[GLOBALTYPE_MAXTYPES] = {0};

static CRITICAL_SECTION tooManySubscriptionsCS;

void CheckForTooManySubscriptions(GlobalType conType, ContainerID conID, U32 numberOfSubscriptions)
{
	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&tooManySubscriptionsCS);
	ATOMIC_INIT_END;
	if(gTooManySubscriptionsAlertThreshold && numberOfSubscriptions >= gTooManySubscriptionsAlertThreshold)
	{
		U32 count = 0;

		PERFINFO_AUTO_START_FUNC();
		EnterCriticalSection(&tooManySubscriptionsCS);

		if(!SubscriptionAlertTracking[conType])
			SubscriptionAlertTracking[conType] = stashTableCreateInt(100);

		if(!stashIntFindInt(SubscriptionAlertTracking[conType], conID, &count) || ((count != U32_MAX) && numberOfSubscriptions >= count + gTooManySubscriptionsAlertThreshold))
		{
			if(count >= gTooManySubscriptionsAlertThreshold * gTooManySubscriptionsAlertMaxAlerts)
			{
				ErrorOrCriticalAlert("OBJECTDB.TOOMANYSUBSCRIPTIONS", "%s[%u] has %u subscriptions. Last alert for this container.", GlobalTypeToName(conType), conID, numberOfSubscriptions);
				stashIntAddInt(SubscriptionAlertTracking[conType], conID, U32_MAX, true);
			}
			else
			{
				ErrorOrCriticalAlert("OBJECTDB.TOOMANYSUBSCRIPTIONS", "%s[%u] has %u subscriptions.", GlobalTypeToName(conType), conID, numberOfSubscriptions);
				stashIntAddInt(SubscriptionAlertTracking[conType], conID, numberOfSubscriptions, true);
			}
		}
		LeaveCriticalSection(&tooManySubscriptionsCS);
		PERFINFO_AUTO_STOP();
	}
}

static void PopulateFullContainerHeader(Packet *pak, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID)
{
	pktSendBits64(pak, 64, conType);
	pktSendBits64(pak, 64, conID);
	pktSendBits64(pak, 64, ownerType);
	pktSendBits64(pak, 64, ownerID);
}

static void PopulateContainerDiffPacket(Packet *pak, GlobalType conType, ContainerID conID, const char *diffString)
{
	pktSendBits(pak, 32, conType);
	pktSendBits(pak, 32, conID);
	pktSendString(pak, diffString);
}

static void PopulateRemoveContainerPacket(Packet *pak, GlobalType conType, ContainerID conID)
{
	pktSendBits(pak, 32, conType);
	pktSendBits(pak, 32, conID);
}

static void PopulateNonExistentContainerPacket(Packet *pak, GlobalType conType, ContainerID conID)
{
	pktSendBits(pak, 32, conType);
	pktSendBits(pak, 32, conID);
}

static void PopulateContainerOwnerPacket(Packet *pak, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID)
{
	pktSendBits(pak, 32, conType);
	pktSendBits(pak, 32, conID);
	pktSendBits(pak, 32, ownerType);
	pktSendBits(pak, 32, ownerID);
}

static void CopyZippedDataForSubscription(Container *con, ParseTable **pti, void **data, int *packed_len, int *unpacked_len, int *header_size)
{
	U32 size_to_use = 0;

	*pti = con->containerSchema->classParse;
	*packed_len = con->bytesCompressed;
	*unpacked_len = con->fileSize;
	*header_size = con->headerSize;

	size_to_use = con->bytesCompressed ? con->bytesCompressed : con->fileSize;
	*data = malloc(size_to_use);
	memcpy(*data, con->fileData, size_to_use);
}

static CachedSubscriptionCopy *CreateSubscriptionCopy(void)
{
	ATOMIC_INIT_BEGIN;
	TSMP_SMART_CREATE(CachedSubscriptionCopy, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ATOMIC_INIT_END;

	return TSMP_CALLOC(CachedSubscriptionCopy);
}

static CachedSubscriptionCopy *MakeSubscriptionCopy(GlobalType type, ContainerID id)
{
	CachedSubscriptionCopy *copy = CreateSubscriptionCopy();

	copy->created = timeSecondsSince2000();
	copy->type = type;
	copy->id = id;

	return copy;
}

static CachedSubscriptionCopy *GetOrCreateCachedSubscriptionCopy(GWTCmdPacket *packet, GlobalType type, ContainerID id)
{
	SubscriptionCache *cache = NULL;
	CachedSubscriptionCopy *copy = NULL;

	EnterCriticalSection(&gSubscriptionCacheCS);
	cache = GetOrCreateSubscriptionCache(type);
	copy = GetCachedSubscription(cache, id);

	if (copy)
	{
		PokeCachedSubscription(copy);
	}
	else
	{
		copy = MakeSubscriptionCopy(type, id);
		AddCachedSubscription(packet, cache, copy);
	}

	LeaveCriticalSection(&gSubscriptionCacheCS);
	return copy;
}

static void ExpireCachedSubscriptionCopy(GWTCmdPacket *packet, GlobalType type, ContainerID id)
{
	static PERFINFO_TYPE *perfs[GLOBALTYPE_MAXTYPES];
	SubscriptionCache *cache = NULL;
	CachedSubscriptionCopy *copy = NULL;

	PERFINFO_AUTO_START_FUNC();
	EnterCriticalSection(&gSubscriptionCacheCS);
	cache = GetOrCreateSubscriptionCache(type);
	copy = GetCachedSubscription(cache, id);

	if (copy)
	{
		START_MISC_COUNT_STATIC(0, GlobalTypeToName(type), &perfs[type]);
		STOP_MISC_COUNT(1);
		RemoveCachedSubscription(packet, copy);
	}

	LeaveCriticalSection(&gSubscriptionCacheCS);
	PERFINFO_AUTO_STOP();
}

// This function represents the core of subscription request handling
// A packet is constructed and passed into this function, possibly with a container
// The presence of the container indicates that we must send a full subscribe copy of the container
// We make this less harmful by caching the results of the send until the container changes
static void DispatchSubscriptionPacket(GWTCmdPacket *packet, Packet **pak, Container *con)
{
	SubscriptionAction action = {0};
	
	PERFINFO_AUTO_START_FUNC();

	// Steal the Packet pointer. It's all mine! You can't have it!
	action.pak = *pak;
	*pak = NULL;
	action.pak_data_idx = pktGetSize(action.pak);

	// By default, "other request"
	action.action_type = SubAction_OtherRequest;

	// If there's a container, do full subscribe copy stuff
	if (con)
	{
		static PERFINFO_TYPE *perfs[GLOBALTYPE_MAXTYPES];

		action.con_type = con->containerType;
		action.con_id = con->containerID;

		// Get the corresponding cache entry (creating it if it's not there)
		PERFINFO_AUTO_START_STATIC(GlobalTypeToName(action.con_type), &perfs[action.con_type], 1);
		action.copy = GetOrCreateCachedSubscriptionCopy(packet, action.con_type, action.con_id);
		assert(action.copy);

		// Figure out where to get the data to send
		if (action.copy->will_have_data)
		{
			// If there's already (or soon will be) data in the cache, use that
			ADD_MISC_COUNT(1, "Cache: hit");
			action.action_type = SubAction_UseCache;
		}
		else if (con->fileData)
		{
			// If there's file data, we can use that cheaply
			PERFINFO_AUTO_START("Cache: packed", 1);
			action.action_type = SubAction_CacheFromFileData;
			CopyZippedDataForSubscription(con, &action.file_data.pti, &action.file_data.data, &action.file_data.packed_len, &action.file_data.unpacked_len, &action.file_data.header_size);
			PERFINFO_AUTO_STOP();
		}
		else
		{
			// There's no file data, and there's no cached data
			// We have to ParserSend right here and we'll cache the results of that
			PERFINFO_AUTO_START("Cache: miss", 1);
			action.action_type = SubAction_CacheFromPacket;
			PopulateFullContainerSend(action.pak, con->containerData, con->containerSchema->classParse);
			PERFINFO_AUTO_STOP();
		}

		// See the comment on CachedSubscriptionCopy.will_have_data above. Setting this here is just a signal to subsequent calls of
		// DispatchSubscriptionPacket. Basically, it says, "this copy may or may not have actual data in it, but if you queue it in
		// a SubscriptionAction, it will have data by the time the SubscriptionThread executes that action."
		action.copy->will_have_data = true;
		PERFINFO_AUTO_STOP();
	}

	QueueSubscriptionThreadAction(&action);
	PERFINFO_AUTO_STOP();
}

static void SendFullContainerToMultipleRecipients(GWTCmdPacket *packet, Container *con, ContainerRef ***pppRecipients, GlobalType conType, ContainerID conID, GlobalType ownerType, GlobalType ownerID)
{
	Packet *pak;
	
	GetPacketToSendThroughTransactionServer_MultipleRecipients_CachedTracker(pak, objLocalManager(), pppRecipients, TRANSPACKETCMD_REMOTECOMMAND, "ReceiveContainerCopy");
	PopulateFullContainerHeader(pak, conType, conID, ownerType, ownerID);
	dbSubscribe_TrackFullPacket(conType, pppRecipients);
	DispatchSubscriptionPacket(packet, &pak, con);
}

static void SendFullContainerToRecipient(GWTCmdPacket *packet, Container *con, const GlobalType subscriberType, ContainerID subscriberID, GlobalType conType, ContainerID conID, GlobalType ownerType, GlobalType ownerID)
{
	Packet *pak;
	GetPacketToSendThroughTransactionServer_CachedTracker(pak, objLocalManager(), subscriberType, subscriberID, TRANSPACKETCMD_REMOTECOMMAND, "ReceiveContainerCopy", NULL, NULL, NULL);
	PopulateFullContainerHeader(pak, conType, conID, ownerType, ownerID);
	dbSubscribe_TrackFullPacket(conType, NULL);
	DispatchSubscriptionPacket(packet, &pak, con);
}

static void SendNonExistentContainerNotificationToRecipient(const GlobalType subscriberType, ContainerID subscriberID, GlobalType conType, ContainerID conID)
{
	Packet *pak;
	GetPacketToSendThroughTransactionServer_CachedTracker(pak, objLocalManager(), subscriberType, subscriberID, TRANSPACKETCMD_REMOTECOMMAND, "HandleNonExistentContainerNotification", NULL, NULL, NULL);
	PopulateNonExistentContainerPacket(pak, conType, conID);
	DispatchSubscriptionPacket(NULL, &pak, NULL);
}

static void SendContainerDiffToMultipleRecipients(ContainerRef ***pppRecipients, GlobalType conType, ContainerID conID, const char *diffString)
{
	Packet *pak;
	
	GetPacketToSendThroughTransactionServer_MultipleRecipients_CachedTracker(pak, objLocalManager(),  pppRecipients, TRANSPACKETCMD_REMOTECOMMAND, "HandleSubscribedContainerCopyChange");
	PopulateContainerDiffPacket(pak, conType, conID, diffString);
	dbSubscribe_TrackDiffPacket(conType, pppRecipients);
	DispatchSubscriptionPacket(NULL, &pak, NULL);
}

static void SendRemoveContainerToRecipient(GlobalType subscriberType, ContainerID subscriberID, GlobalType conType, ContainerID conID)
{
	Packet *pak;
	
	GetPacketToSendThroughTransactionServer_CachedTracker(pak, objLocalManager(),  subscriberType, subscriberID, TRANSPACKETCMD_REMOTECOMMAND, "HandleRemoveSubscribedContainer", NULL, NULL, NULL);
	PopulateRemoveContainerPacket(pak, conType, conID);
	dbSubscribe_TrackRemovePacket(conType, NULL);
	DispatchSubscriptionPacket(NULL, &pak, NULL);
}

static void SendOwnerChangeToMultipleRecipients(ContainerRef ***pppRecipients, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID)
{
	Packet *pak;
	
	GetPacketToSendThroughTransactionServer_MultipleRecipients_CachedTracker(pak, objLocalManager(), pppRecipients, TRANSPACKETCMD_REMOTECOMMAND, "HandleSubscribedContainerCopyOwnerChange");
	PopulateContainerOwnerPacket(pak, conType, conID, ownerType, ownerID);
	DispatchSubscriptionPacket(NULL, &pak, NULL);
}

static void SendDestroyContainerToMultipleRecipients(ContainerRef ***pppRecipients, GlobalType conType, ContainerID conID)
{
	Packet *pak;
	
	GetPacketToSendThroughTransactionServer_MultipleRecipients_CachedTracker(pak, objLocalManager(), pppRecipients, TRANSPACKETCMD_REMOTECOMMAND, "HandleSubscribedContainerCopyDestroy");
	PopulateRemoveContainerPacket(pak, conType, conID);
	DispatchSubscriptionPacket(NULL, &pak, NULL);
}

typedef struct SubscriptionHandlerData
{
	ContainerRef **ppRecipients;
	ContainerRef **ppFreshRecipients;
} SubscriptionHandlerData;

void dbContainerCreateNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID)
{
	U32 iNumberOfRecipients;
	int i;
	MonitoredContainer *pMonitored;
	Container *con;
	SubscriptionHandlerData *threadData = NULL;
	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START("SendingSubscriptionUpdates",1);
	con = objGetContainerEx(conType, conID, false, false, false);
	if (con)
		ExpireCachedSubscriptionCopy(packet, conType, conID);

	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbGetMonitoredContainer(conType, conID);

	if (!pMonitored)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("AddingIndividualSubscriptions", 1);
	for (i = 0; i < eaSize(&pMonitored->ppSubscriptions); i++)
	{
		ContainerRef *newRef = StructCreate(parse_ContainerRef);
		ContainerSubscription *conSub = pMonitored->ppSubscriptions[i];
		StructCopy(parse_ContainerRef, &conSub->subscribedServer, newRef, 0, 0, 0);
		eaPush(&threadData->ppRecipients, newRef);
	}
	PERFINFO_AUTO_STOP();
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if (iNumberOfRecipients = eaSize(&threadData->ppRecipients))
	{
		PERFINFO_AUTO_START("sendSubscribedContainerCopyCreate",1);
		if(con)
		{
			SendFullContainerToMultipleRecipients(packet, con, &threadData->ppRecipients, conType, conID, con->meta.containerOwnerType, con->meta.containerOwnerID);
		}
		PERFINFO_AUTO_STOP();
	}
	eaClearStruct(&threadData->ppRecipients, parse_ContainerRef);
	CheckForTooManySubscriptions(conType, conID, iNumberOfRecipients);
	PERFINFO_AUTO_STOP();
}

void dbContainerChangeNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID, char *diffString)
{
	U32 iNumberOfRecipients;
	U32 iNumberOfFreshRecipients;
	int i;
	MonitoredContainer *pMonitored;
	Container *con;
	SubscriptionHandlerData *threadData = NULL;
	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START("SendingSubscriptionUpdates",1);
	con = objGetContainerEx(conType, conID, false, false, false);
	if (con)
		ExpireCachedSubscriptionCopy(packet, conType, conID);

	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbGetMonitoredContainer(conType, conID);

	if (!pMonitored)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("AddingIndividualSubscriptions", 1);
	for (i = 0; i < eaSize(&pMonitored->ppSubscriptions); i++)
	{
		ContainerSubscription *conSub = pMonitored->ppSubscriptions[i];
		ContainerRef *newRef = StructCreate(parse_ContainerRef);
		StructCopy(parse_ContainerRef, &conSub->subscribedServer, newRef, 0, 0, 0);
		if(conSub->bSent)
		{
			eaPush(&threadData->ppRecipients, newRef);
		}
		else
		{
			eaPush(&threadData->ppFreshRecipients, newRef);
			if(con)
			{
				conSub->bSent = true;
				con->isSubscribed = true;
			}
		}
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("AddingOnlineSubscriptions", 1);
	if (pMonitored->containerOwner.containerType != objServerType() || pMonitored->containerOwner.containerID != objServerID())
	{
		// If this container is online update online subscriptions
		for (i = 0; i < eaSize(&monitoredTypes[conType].ppOnlineSubscriptions); i++)
		{
			ContainerRef *newRef = StructCreate(parse_ContainerRef);
			StructCopy(parse_ContainerRef, &monitoredTypes[conType].ppOnlineSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
			eaPush(&threadData->ppRecipients, newRef);
		}
	}
	PERFINFO_AUTO_STOP();
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if (iNumberOfRecipients = eaSize(&threadData->ppRecipients))
	{
		PERFINFO_AUTO_START("sendSubscribedContainerCopyChange",1);
		SendContainerDiffToMultipleRecipients(&threadData->ppRecipients, conType, conID, diffString);
		PERFINFO_AUTO_STOP();
	}
	eaClearStruct(&threadData->ppRecipients, parse_ContainerRef);

	if (iNumberOfFreshRecipients = eaSize(&threadData->ppFreshRecipients))
	{
		PERFINFO_AUTO_START("sendSubscribedContainerCopyChange",1);
		if(con)
		{
			SendFullContainerToMultipleRecipients(packet, con, &threadData->ppFreshRecipients, conType, conID, con->meta.containerOwnerType, con->meta.containerOwnerID);
		}
		PERFINFO_AUTO_STOP();
	}
	eaClearStruct(&threadData->ppFreshRecipients, parse_ContainerRef);
	CheckForTooManySubscriptions(conType, conID, iNumberOfRecipients + iNumberOfFreshRecipients);
	PERFINFO_AUTO_STOP();
}

void dbContainerOwnerChangeNotify(GWTCmdPacket *packet, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, bool getLock)
{
	U32 iNumberOfRecipients;
	bool bLoggingInOrOut = false;
	int i;
	MonitoredContainer *pMonitored;
	Container *con;
	GlobalType pMonitoredOwnerType = GLOBALTYPE_NONE;
	ContainerID pMonitoredOwnerID = 0;
	U32 pMonitoredLoginTime;
	SubscriptionHandlerData *threadData = NULL;
	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START_FUNC();
	con = objGetContainerEx(conType, conID, false, false, false);

	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbAddOrGetMonitoredContainer(conType, conID);

	if (!pMonitored || !con)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (ownerType == pMonitored->containerOwner.containerType && ownerID == pMonitored->containerOwner.containerID)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return; //no change
	}

	// This is counting total transfer, including between maps. Is this correct?
	if (ownerType == GLOBALTYPE_GAMESERVER)
	{
		CountAverager_ItHappened(gDatabaseState.DBUpdateTransferAverager);
	}

	pMonitoredOwnerType = pMonitored->containerOwner.containerType;
	pMonitoredOwnerID = pMonitored->containerOwner.containerID;
	pMonitoredLoginTime = pMonitored->loginTime;
	pMonitored = NULL;
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if (ownerType == objServerType() && (ownerID == objServerID() || ownerID == 0) && pMonitoredOwnerType != 0)
	{
		PERFINFO_AUTO_START("NotifyLoggedOut",1);
		dbContainerLog(LOG_LOGIN, conType, conID, getLock, "Logout", "ReturnedToDB");

		if (conType == GLOBALTYPE_ENTITYPLAYER && GSM_IsStateActiveOrPending(DBSTATE_MASTER_HANDLE_REQUESTS))
		{
			S32 iGuildID = 0;
			U32 uStartTime = pMonitoredLoginTime;
			PERFINFO_AUTO_START("NotifyLoggedOut:APLogout",1);

			if (!con->containerData)
				objUnpackContainer(con->containerSchema, con, ForceKeepLazyLoadFileData(con->containerType), false, false);

			if (uStartTime)
			{
				AccountLogoutNotification *pUpdate = StructCreate(parse_AccountLogoutNotification);
				char foundName[MAX_NAME_LEN] = "";
				char accountIDString[16];
				char playerLevelString[16];
				if (con)
				{
					if (objPathGetString(".pPlayer.accountID", con->containerSchema->classParse, con->containerData, SAFESTR(accountIDString)) &&
						objPathGetString(".pInventoryV2.ppLiteBags[Numeric].ppIndexedLiteSlots[Level].count", con->containerSchema->classParse, con->containerData, SAFESTR(playerLevelString)))
					{
						pUpdate->uAccountID = atoi(accountIDString);
						pUpdate->uLevel = atoi(playerLevelString);
						estrCopy2(&pUpdate->pProductName, GetProductName());
						estrCopy2(&pUpdate->pShardCategory, GetShardCategoryFromShardInfoString());
						pUpdate->uPlayTime = timeSecondsSince2000() - uStartTime;
						RemoteCommand_aslAPCmdLogoutNotification (GLOBALTYPE_ACCOUNTPROXYSERVER, 0, pUpdate);
					}
				}
				StructDestroy(parse_AccountLogoutNotification, pUpdate);
			}
			else
			{
				dbContainerLog(LOG_LOGIN, conType, conID, getLock, "PlayTime", "Error \"Playtime is empty\"");
			}
			PERFINFO_AUTO_STOP();
#ifndef USE_CHATRELAY
			RemoteCommand_ChatServerLogoutEx(GLOBALTYPE_CHATSERVER, 0, dbAccountIDFromID(conType, conID), conID);
#endif
			objPathGetInt(".pPlayer.pGuild.iGuildID", con->containerSchema->classParse, con->containerData, &iGuildID);
			if (iGuildID) {
				RemoteCommand_aslGuild_UpdateInfo(GLOBALTYPE_GUILDSERVER, 0, (U32)iGuildID, conID, 0, -1, "", "", "", 0, 0, "", "", false);
			}
		}
		bLoggingInOrOut = true;

		EnterCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_START("NotifyLoggedOut:RemoveSubscriptions",1);
		for (i = 0; i < eaSize(&monitoredTypes[conType].ppOnlineSubscriptions); i++)
		{
			ContainerSubscription *pSub = monitoredTypes[conType].ppOnlineSubscriptions[i];
			SendRemoveContainerToRecipient(pSub->subscribedServer.containerType, pSub->subscribedServer.containerID, conType, conID);
		}
		PERFINFO_AUTO_STOP();
		LeaveCriticalSection(&gSubscriptionTrackingCS);

		PERFINFO_AUTO_STOP();
	}
	else if (pMonitoredOwnerType == objServerType() && pMonitoredOwnerID == objServerID())
	{
		PERFINFO_AUTO_START("NotifyLoggedIn",1);
		// Either we're leaving here, or we're new
		dbContainerLog(LOG_LOGIN, conType, conID, getLock, "Login", "LeftDB");

		bLoggingInOrOut = true;

		PERFINFO_AUTO_START("NotifyLoggedIn:AddSubscriptions",1);

		EnterCriticalSection(&gSubscriptionTrackingCS);

		if (conType == GLOBALTYPE_ENTITYPLAYER)
		{
			pMonitored = dbAddOrGetMonitoredContainer(conType, conID);
			if(pMonitored)
				pMonitored->loginTime = timeSecondsSince2000();
		}

		for (i = 0; i < eaSize(&monitoredTypes[conType].ppOnlineSubscriptions); i++)
		{
			ContainerRef *newRef = StructCreate(parse_ContainerRef);
			StructCopy(parse_ContainerRef, &monitoredTypes[conType].ppOnlineSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
			eaPush(&threadData->ppFreshRecipients, newRef);
		}
		pMonitored = NULL;
		LeaveCriticalSection(&gSubscriptionTrackingCS);

		if (eaSize(&threadData->ppFreshRecipients))
		{
			SendFullContainerToMultipleRecipients(packet, con, &threadData->ppFreshRecipients, conType, conID, ownerType, ownerID);
		}
		eaClearStruct(&threadData->ppFreshRecipients, parse_ContainerRef);

		PERFINFO_AUTO_STOP();

		PERFINFO_AUTO_STOP();
	}

	EnterCriticalSection(&gSubscriptionTrackingCS);

	pMonitored = dbAddOrGetMonitoredContainer(conType, conID);
	if(!pMonitored)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("SendingSubscriptionUpdates",1);
	pMonitored->containerOwner.containerType = ownerType;
	pMonitored->containerOwner.containerID = ownerID;

	PERFINFO_AUTO_START("AddingIndividualSubscriptions", 1);
	for (i = 0; i < eaSize(&pMonitored->ppSubscriptions); i++)
	{
		ContainerRef *newRef = StructCreate(parse_ContainerRef);
		StructCopy(parse_ContainerRef, &pMonitored->ppSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
		eaPush(&threadData->ppRecipients, newRef);
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("AddingOnlineSubscriptions", 1);
	if ((pMonitored->containerOwner.containerType != objServerType() || pMonitored->containerOwner.containerID != objServerID())
		&& !bLoggingInOrOut)
	{
		// If this isn't an initial login (which is handled above), update servers with online subscriptions
		for (i = 0; i < eaSize(&monitoredTypes[conType].ppOnlineSubscriptions); i++)
		{
			ContainerRef *newRef = StructCreate(parse_ContainerRef);
			StructCopy(parse_ContainerRef, &monitoredTypes[conType].ppOnlineSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
			eaPush(&threadData->ppRecipients, newRef);
		}
	}
	PERFINFO_AUTO_STOP();

	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if (iNumberOfRecipients = eaSize(&threadData->ppRecipients))
	{
		PERFINFO_AUTO_START("sendSubscribedContainerCopyChange",1);
		SendOwnerChangeToMultipleRecipients(&threadData->ppRecipients, conType, conID, ownerType, ownerID);
		PERFINFO_AUTO_STOP();
		CheckForTooManySubscriptions(conType, conID, iNumberOfRecipients);
	}
	eaClearStruct(&threadData->ppRecipients, parse_ContainerRef);
	PERFINFO_AUTO_STOP();
	PERFINFO_AUTO_STOP();
}

AUTO_RUN;
void InitMonitoredTypes(void)
{
	int conType;
	for (conType = GLOBALTYPE_NONE + 1; conType < GLOBALTYPE_LAST; conType++)
	{
		eaIndexedEnable(&monitoredTypes[conType].ppOnlineSubscriptions, parse_ContainerSubscription); // Needed because DB doesn't auto create indexed earrays for memory reasons		
	}

	TSMP_SMART_CREATE(ContainerSubscription, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_ContainerSubscription, &TSMP_NAME(ContainerSubscription));

	TSMP_SMART_CREATE(ContainerSubscriptionInverse, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_ContainerSubscriptionInverse, &TSMP_NAME(ContainerSubscriptionInverse));

	TSMP_SMART_CREATE(MonitoredContainer, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_MonitoredContainer, &TSMP_NAME(MonitoredContainer));

	TSMP_SMART_CREATE(MonitoringServer, 1024, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_MonitoringServer, &TSMP_NAME(MonitoringServer));

	InitializeCriticalSection(&gSubscriptionTrackingCS);
	InitializeCriticalSection(&gSubscriptionCacheCS);
}

// Container subscription

// must have gSubscriptionTrackingCS here
static ContainerSubscription *AddContainerIndexedSubscription(MonitoredContainer *pMonitored, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, const char* reason)
{
	ContainerSubscription searchSubscription = {0};
	ContainerSubscription *pSubscription = NULL;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedServer.containerType = ownerType;
	searchSubscription.subscribedServer.containerID = ownerID;

	pSubscription = dbFindContainerSubscription(pMonitored, &searchSubscription);
	if (!pSubscription)
	{
		pSubscription = StructClone(parse_ContainerSubscription, &searchSubscription);
		assert(pSubscription);
		pSubscription->msTimeStart = timeGetTime();
		eaPush(&pMonitored->ppSubscriptions, pSubscription);
			
		if(dbAlwaysPerfSubscriptions || PERFINFO_RUN_CONDITIONS)
		{
			char *ownerAndReason = NULL;
			SubscribePerf *sp = NULL;
				
			estrStackCreate(&ownerAndReason);
			estrPrintf(&ownerAndReason, "%s:%s", GlobalTypeToName(ownerType), reason);
				
			if(!stSubscribeReasonToPerf){
				stSubscribeReasonToPerf = stashTableCreateWithStringKeys(1000, StashDefault);
			}else{
				stashFindPointer(stSubscribeReasonToPerf, ownerAndReason, &sp);
			}
				
			if(!sp){
				sp = callocStruct(SubscribePerf);
				sp->reason = strdup(ownerAndReason);
					
				if(!stashAddPointer(stSubscribeReasonToPerf, sp->reason, sp, false)){
					assert(0);
				}
			}

			estrDestroy(&ownerAndReason);

			pSubscription->perf = sp;
		}
	}

	pSubscription->refCount++;
	pMonitored->refCount++;
	monitoredTypes[conType].refCount++;
	PERFINFO_AUTO_STOP();
	return pSubscription;
}

// must have gSubscriptionTrackingCS here
static void AddServerIndexedSubscription(MonitoringServer *pMonitoring, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, const char* reason)
{
	ContainerSubscriptionInverse searchSubscription = {0};
	ContainerSubscriptionInverse *pSubscription = NULL;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedContainer.containerType = conType;
	searchSubscription.subscribedContainer.containerID = conID;

	pSubscription = dbFindMonitoringContainerSubscription(pMonitoring, &searchSubscription);
	if (!pSubscription)
	{
		pSubscription = StructClone(parse_ContainerSubscriptionInverse, &searchSubscription);
		assert(pSubscription);
		pSubscription->msTimeStart = timeGetTime();
		eaPush(&pMonitoring->ppSubscriptions, pSubscription);

		if(dbAlwaysPerfSubscriptions || PERFINFO_RUN_CONDITIONS)
		{
			char *ownerAndReason = NULL;
			SubscribePerf *sp = NULL;

			estrStackCreate(&ownerAndReason);
			estrPrintf(&ownerAndReason, "%s:%s", GlobalTypeToName(ownerType), reason);

			if(!stSubscribeReasonToPerf){
				stSubscribeReasonToPerf = stashTableCreateWithStringKeys(1000, StashDefault);
			}else{
				stashFindPointer(stSubscribeReasonToPerf, ownerAndReason, &sp);
			}

			if(!sp){
				sp = callocStruct(SubscribePerf);
				sp->reason = strdup(ownerAndReason);

				if(!stashAddPointer(stSubscribeReasonToPerf, sp->reason, sp, false)){
					assert(0);
				}
			}

			estrDestroy(&ownerAndReason);
			pSubscription->perf = sp;
		}
	}

	pSubscription->refCount++;
	pMonitoring->refCount++;
	PERFINFO_AUTO_STOP();
}

void dbSubscribeToContainer_CB(GWTCmdPacket *packet, SubscribeToContainerInfo *info)
{
	ContainerSubscription *conSub;
	Container *con;
	MonitoredContainer *pMonitored;
	MonitoringServer *pMonitoring;

	PERFINFO_AUTO_START("dbSubscribeToContainer", 1);
	con = objGetContainerEx(info->conType, info->conID, false, false, false);
	
	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbAddOrGetMonitoredContainer(info->conType, info->conID);
	pMonitoring = dbAddOrGetMonitoringServer(info->subscriberType, info->subscriberID);

	if(dbLogSubscriptionChanges)
	{
		log_printf(LOG_CONTAINER,
			"Subscribing to %s[%d] from %s[%d]: %s",
			GlobalTypeToName(info->conType),
			info->conID,
			GlobalTypeToName(info->subscriberType),
			info->subscriberID,
			info->reason);
	}

	conSub = AddContainerIndexedSubscription(pMonitored, info->conType, info->conID, info->subscriberType, info->subscriberID, info->reason);
	AddServerIndexedSubscription(pMonitoring, info->conType, info->conID, info->subscriberType, info->subscriberID, info->reason);

	if (con)
	{
		if(conSub)
			conSub->bSent = true;
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		con->isSubscribed = true;
		SendFullContainerToRecipient(packet, con, info->subscriberType, info->subscriberID, info->conType, info->conID, con->meta.containerOwnerType, con->meta.containerOwnerID);
	}
	else
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		SendNonExistentContainerNotificationToRecipient(info->subscriberType, info->subscriberID, info->conType, info->conID);
	}
	PERFINFO_AUTO_STOP();
}

void OVERRIDE_LATELINK_dbSubscribeToContainer(	GlobalType subscriberType,
												ContainerID subscriberID,
												GlobalType conType,
												ContainerID conID,
												const char* reason)
{
	SubscribeToContainerInfo info = {0};

	PERFINFO_AUTO_START_FUNC();

	if (conType <= 0 || conType >= GLOBALTYPE_MAXTYPES || !conID)
	{
		ErrorOrAlert("OBJECTDB.SUBSCRIPTIONERROR", CON_PRINTF_STR " has tried to subscribe to " CON_PRINTF_STR, CON_PRINTF_ARG(subscriberType, subscriberID), CON_PRINTF_ARG(conType, conID));
		PERFINFO_AUTO_STOP();
		return;
	}

	info.subscriberType = subscriberType;
	info.subscriberID = subscriberID;
	info.conType = conType;
	info.conID = conID;
	info.reason = strdup(reason);

	if (GenericDatabaseThreadIsActive())
	{
		QueueSubscribeToContainerOnGenericDatabaseThreads(&info);
	}
	else
	{
		dbSubscribeToContainer_CB(NULL, &info);
	}

	PERFINFO_AUTO_STOP();
}

// returns true if we find the subscription and have removed the last one
#define RemoveContainerIndexedSubscription(pMonitored, conType, conID, ownerType, ownerID, reason) RemoveContainerIndexedSubscriptionEx(pMonitored, conType, conID, ownerType, ownerID, 1, reason) 
// must have gSubscriptionTrackingCS here
static bool RemoveContainerIndexedSubscriptionEx(MonitoredContainer **pMonitored, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, int count, const char* reason)
{
	ContainerSubscription searchSubscription = {0};
	ContainerSubscription *pSubscription = NULL;
	int index;
	bool retVal = false;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedServer.containerType = ownerType;
	searchSubscription.subscribedServer.containerID = ownerID;

	index = eaIndexedFind(&(*pMonitored)->ppSubscriptions, &searchSubscription);
	if (index == -1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pSubscription = (*pMonitored)->ppSubscriptions[index];

	if(dbLogSubscriptionChanges)
	{
		log_printf(LOG_CONTAINER,
					"Unsubscribing after %1.2fs from %s[%d] from %s[%d]: %s",
					(F32)(timeGetTime() - pSubscription->msTimeStart) / 1000.f,
					GlobalTypeToName(conType),
					conID,
					GlobalTypeToName(ownerType),
					ownerID,
					reason);
	}
	
	if(dbAlwaysPerfSubscriptions || PERFINFO_RUN_CONDITIONS){
		U32 msTimeDelta = timeGetTime() - pSubscription->msTimeStart;
		
		if(msTimeDelta <= 100){
			START_MISC_COUNT(0, "<= 100ms");
		}
		else if(msTimeDelta <= 500){
			START_MISC_COUNT(0, "<= 500ms");
		}
		else if(msTimeDelta <= 1000){
			START_MISC_COUNT(0, "<= 1s");
		}
		else if(msTimeDelta <= 5000){
			START_MISC_COUNT(0, "<= 5s");
		}
		else if(msTimeDelta <= 10000){
			START_MISC_COUNT(0, "<= 10s");
		}
		else if(msTimeDelta <= 60000){
			START_MISC_COUNT(0, "<= 60s");
		}else{
			START_MISC_COUNT(0, "> 60s");
		}
		
		if(pSubscription->perf){
			START_MISC_COUNT_STATIC(0, pSubscription->perf->reason, &pSubscription->perf->pi);
			STOP_MISC_COUNT(1);
		}else{
			ADD_MISC_COUNT(1, "unknown reason");
		}
		
		STOP_MISC_COUNT(1);
	}

	pSubscription->refCount -= count;
	(*pMonitored)->refCount -= count;
	monitoredTypes[conType].refCount -= count;
	if(pSubscription->refCount == 0)
	{
		eaRemove(&(*pMonitored)->ppSubscriptions, index);
		StructDestroy(parse_ContainerSubscription, pSubscription);
		retVal = true;
	}
	CleanupMonitoredContainerStruct(pMonitored);
	PERFINFO_AUTO_STOP();
	return retVal;
}

#define RemoveServerIndexedSubscription(pMonitoring, conType, conID, ownerType, ownerID, reason) RemoveServerIndexedSubscriptionEx(pMonitoring, conType, conID, ownerType, ownerID, 1, reason) 
// must have gSubscriptionTrackingCS here
static bool RemoveServerIndexedSubscriptionEx(MonitoringServer **pMonitoring, GlobalType conType, ContainerID conID, GlobalType ownerType, ContainerID ownerID, int count, const char* reason)
{
	ContainerSubscriptionInverse searchSubscription = {0};
	ContainerSubscriptionInverse *pSubscription = NULL;
	int index;
	bool retVal = false;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedContainer.containerType = conType;
	searchSubscription.subscribedContainer.containerID = conID;

	index = eaIndexedFind(&(*pMonitoring)->ppSubscriptions, &searchSubscription);
	if (index == -1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	pSubscription = (*pMonitoring)->ppSubscriptions[index];

	pSubscription->refCount -= count;
	(*pMonitoring)->refCount -= count;
	if(pSubscription->refCount == 0)
	{
		eaRemove(&(*pMonitoring)->ppSubscriptions, index);
		StructDestroy(parse_ContainerSubscriptionInverse, pSubscription);
		retVal = true;
	}
	CleanupMonitoringServerStruct(pMonitoring);
	PERFINFO_AUTO_STOP();
	return retVal;
}

void dbUnsubscribeFromContainer_CB(SubscribeToContainerInfo *info)
{
	Container *con = NULL;
	bool bRemoved = false;
	MonitoredContainer *pMonitored = NULL;
	MonitoringServer *pMonitoring = NULL;

	PERFINFO_AUTO_START("dbUnsubscribeFromContainer", 1);
	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbGetMonitoredContainer(info->conType, info->conID);
	pMonitoring = dbGetMonitoringServer(info->subscriberType, info->subscriberID);

	if(!pMonitored || !pMonitoring)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	// this needs to do the Owner indexed remove before returning
	bRemoved = RemoveContainerIndexedSubscription(&pMonitored, info->conType, info->conID, info->subscriberType, info->subscriberID, info->reason);
	RemoveServerIndexedSubscription(&pMonitoring, info->conType, info->conID, info->subscriberType, info->subscriberID, info->reason);
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if(!bRemoved)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	con = objGetContainerEx(info->conType, info->conID, false, false, false);
	// We can now have subscriptions to non-existent containers
	if(con)
	{
		// WARNING! Don't try to actually read pMonitored here, it might not be valid anymore
		// If you need its contents, then you need to hold the critical section until here
		if(!pMonitored)
			con->isSubscribed = false;
		con->lastAccessed = timeSecondsSince2000();
	}

	//Don't send back the acknowledgement, the requesting server is responsible for handling it
	//SendRemoveContainerToRecipient(ownerType, ownerID, conType, conID);
	dbSubscribe_TrackUnsubscribe(info->conType);
	PERFINFO_AUTO_STOP();
}

void OVERRIDE_LATELINK_dbUnsubscribeFromContainer(	GlobalType subscriberType,
													ContainerID subscriberID,
													GlobalType conType,
													ContainerID conID,
													const char* reason)
{
	SubscribeToContainerInfo info = {0};

	PERFINFO_AUTO_START_FUNC();

	if (conType <= 0 || conType >= GLOBALTYPE_MAXTYPES || !conID)
	{
		ErrorOrAlert("OBJECTDB.SUBSCRIPTIONERROR", CON_PRINTF_STR " has tried to subscribe to " CON_PRINTF_STR, CON_PRINTF_ARG(subscriberType, subscriberID), CON_PRINTF_ARG(conType, conID));
		PERFINFO_AUTO_STOP();
		return;
	}

	info.subscriberType = subscriberType;
	info.subscriberID = subscriberID;
	info.conType = conType;
	info.conID = conID;
	info.reason = strdup(reason);

	if (GenericDatabaseThreadIsActive())
	{
		QueueUnsubscribeFromContainerOnGenericDatabaseThreads(&info);
	}
	else
	{
		dbUnsubscribeFromContainer_CB(&info);
	}

	PERFINFO_AUTO_STOP();
}

// must have gSubscriptionTrackingCS here
static void AddContainerIndexedOnlineSubscription(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	ContainerSubscription searchSubscription = {0};
	ContainerSubscription *pSubscription = NULL;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedServer.containerType = ownerType;
	searchSubscription.subscribedServer.containerID = ownerID;

	pSubscription = dbFindOnlineContainersSubscription(conType, &searchSubscription);

	if (!pSubscription)
	{
		pSubscription = StructClone(parse_ContainerSubscription, &searchSubscription);
		assert(pSubscription);
		pSubscription->msTimeStart = timeGetTime();
		eaPush(&monitoredTypes[conType].ppOnlineSubscriptions, pSubscription);
	}
	PERFINFO_AUTO_STOP();
}

// must have gSubscriptionTrackingCS here
static void AddServerIndexedOnlineSubscription(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	ContainerSubscriptionInverse searchSubscription = {0};
	ContainerSubscriptionInverse *pSubscription = NULL;
	MonitoringServer *pMonitoring;
	int index;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedContainer.containerType = conType;

	pMonitoring = dbAddOrGetMonitoringServer(ownerType, ownerID);

	index = dbFindOnlineMonitoringContainersSubscription(pMonitoring, &searchSubscription);

	if (index == -1)
	{
		pSubscription = StructClone(parse_ContainerSubscriptionInverse, &searchSubscription);
		assert(pSubscription);
		pSubscription->msTimeStart = timeGetTime();
		eaPush(&pMonitoring->ppOnlineSubscriptions, pSubscription);
	}
	PERFINFO_AUTO_STOP();
}

static void dbSubscribeToOnlineContainers_CB(Container *con, SubscribeToContainerInfo *info)
{
	if (con->meta.containerState == CONTAINERSTATE_DB_COPY)
	{
		SendFullContainerToRecipient(NULL, con, info->subscriberType, info->subscriberID, con->containerType, con->containerID, con->meta.containerOwnerType, con->meta.containerOwnerID);
	}
}

void OVERRIDE_LATELINK_dbSubscribeToOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{	
	SubscribeToContainerInfo info = {0};

	if (conType <= 0 || conType >= GLOBALTYPE_MAXTYPES)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if(dbLogSubscriptionChanges)
	{
		log_printf(LOG_CONTAINER,
			"Subscribing to all %s containers from %s[%d]",
			GlobalTypeToName(conType),
			GlobalTypeToName(ownerType),
			ownerID);
	}

	// Because this type of request sucks so much, it's not really sane to bother threading it; instead,
	// we'll just flush the threads and then block all activity until we're done sending everything
	FlushGenericDatabaseThreads(false);

	EnterCriticalSection(&gSubscriptionTrackingCS);
	AddContainerIndexedOnlineSubscription(ownerType, ownerID, conType);
	AddServerIndexedOnlineSubscription(ownerType, ownerID, conType);
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	info.subscriberType = ownerType;
	info.subscriberID = ownerID;

	ForEachContainerOfType(conType, dbSubscribeToOnlineContainers_CB, &info, false);
	PERFINFO_AUTO_STOP();
}

// returns true if we find the subscription and have removed the last one
// must have gSubscriptionTrackingCS here
static bool RemoveContainerIndexedOnlineSubscription(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	ContainerSubscription searchSubscription = {0};
	ContainerSubscription *pSubscription = NULL;
	int index;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedServer.containerType = ownerType;
	searchSubscription.subscribedServer.containerID = ownerID;

	index = eaIndexedFind(&monitoredTypes[conType].ppOnlineSubscriptions, &searchSubscription);
	if (index == -1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}
	pSubscription = monitoredTypes[conType].ppOnlineSubscriptions[index];

	if(dbLogSubscriptionChanges)
	{
		log_printf(LOG_CONTAINER,
			"Unsubscribing after %1.2fs from all %s containers from %s[%d]",
			(F32)(timeGetTime() - pSubscription->msTimeStart) / 1000.f,
			GlobalTypeToName(conType),
			GlobalTypeToName(ownerType),
			ownerID);
	}

	pSubscription->refCount--;
	if (pSubscription->refCount >= 1)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	eaRemove(&monitoredTypes[conType].ppOnlineSubscriptions, index);
	StructDestroy(parse_ContainerSubscription, pSubscription);
	PERFINFO_AUTO_STOP();
	return true;
}

// must have gSubscriptionTrackingCS here
static void RemoveServerIndexedOnlineSubscription(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	ContainerSubscriptionInverse searchSubscription = {0};
	ContainerSubscriptionInverse *pSubscription = NULL;
	MonitoringServer *pMonitoring;
	int index = -1;

	PERFINFO_AUTO_START_FUNC();
	searchSubscription.subscribedContainer.containerType = conType;

	pMonitoring = dbGetMonitoringServer(ownerType, ownerID);

	if(!pMonitoring)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	index = dbFindOnlineMonitoringContainersSubscription(pMonitoring, &searchSubscription);

	if(index >= 0)
	{
		pSubscription = pMonitoring->ppOnlineSubscriptions[index];
	}	
	else
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	pSubscription->refCount--;
	if(pSubscription->refCount >= 1)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	eaRemove(&pMonitoring->ppOnlineSubscriptions, index);
	StructDestroy(parse_ContainerSubscriptionInverse, pSubscription);
	CleanupMonitoringServerStruct(&pMonitoring);
	PERFINFO_AUTO_STOP();
}

static void dbUnsubscribeFromOnlineContainers_CB(Container *con, SubscribeToContainerInfo *info)
{
	if (con->meta.containerState == CONTAINERSTATE_DB_COPY)
	{
		SendRemoveContainerToRecipient(info->subscriberType, info->subscriberID, con->containerType, con->containerID);
	}
}

void OVERRIDE_LATELINK_dbUnsubscribeFromOnlineContainers(GlobalType ownerType, ContainerID ownerID, GlobalType conType)
{
	bool bRemoved;
	SubscribeToContainerInfo info = {0};

	if (conType <= 0 || conType >= GLOBALTYPE_MAXTYPES)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	// Because this type of request sucks so much, it's not really sane to bother threading it; instead,
	// we'll just flush the threads and then block all activity until we're done sending everything
	FlushGenericDatabaseThreads(false);
	
	EnterCriticalSection(&gSubscriptionTrackingCS);
	bRemoved = RemoveContainerIndexedOnlineSubscription(ownerType, ownerID, conType);
	RemoveServerIndexedOnlineSubscription(ownerType, ownerID, conType);
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if(!bRemoved)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	info.subscriberType = ownerType;
	info.subscriberID = ownerID;

	ForEachContainerOfType(conType, dbUnsubscribeFromOnlineContainers_CB, &info, false);
	PERFINFO_AUTO_STOP();
}

void dbUnsubscribeFromAllContainers(GlobalType ownerType, ContainerID ownerID)
{
	MonitoringServer *pMonitoring;
	EnterCriticalSection(&gSubscriptionTrackingCS);

	pMonitoring = dbGetMonitoringServer(ownerType, ownerID);
	if(!pMonitoring)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		return;
	}

	EARRAY_FOREACH_BEGIN(pMonitoring->ppOnlineSubscriptions, i);
	{
		RemoveContainerIndexedOnlineSubscription(ownerType, ownerID, pMonitoring->ppOnlineSubscriptions[i]->subscribedContainer.containerType);
	}
	EARRAY_FOREACH_END;

	eaClearStruct(&pMonitoring->ppOnlineSubscriptions, parse_ContainerSubscriptionInverse);

	EARRAY_FOREACH_BEGIN(pMonitoring->ppSubscriptions, i);
	{
		ContainerRef *pConRef = &pMonitoring->ppSubscriptions[i]->subscribedContainer;
		MonitoredContainer *pMonitored = dbGetMonitoredContainer(pConRef->containerType, pConRef->containerID);
		if(devassert(pMonitored))
			RemoveContainerIndexedSubscriptionEx(&pMonitored, pConRef->containerType, pConRef->containerID, ownerType, ownerID, pMonitoring->ppSubscriptions[i]->refCount, "Server went away");
	}
	EARRAY_FOREACH_END;

	eaClearStruct(&pMonitoring->ppSubscriptions, parse_ContainerSubscriptionInverse);
	pMonitoring->refCount = 0;
	CleanupMonitoringServerStruct(&pMonitoring);
	LeaveCriticalSection(&gSubscriptionTrackingCS);
}

// must have gSubscriptionTrackingCS here
static bool VerifySubscriptionCountsByServer(MonitoringServer *pMonitoring, GlobalType serverType, ContainerID serverID)
{
	bool bVerified = true;

	EARRAY_FOREACH_BEGIN(pMonitoring->ppOnlineSubscriptions, i);
	{
		//Verify online
		ContainerSubscription searchSubscription = {0};
		ContainerSubscription *pSubscription = NULL;
		searchSubscription.subscribedServer.containerType = serverType;
		searchSubscription.subscribedServer.containerID = serverID;

		pSubscription = dbFindOnlineContainersSubscription(pMonitoring->ppOnlineSubscriptions[i]->subscribedContainer.containerType, &searchSubscription);
		if(!devassert(pSubscription))
			bVerified = false;
	}
	EARRAY_FOREACH_END;

	EARRAY_FOREACH_BEGIN(pMonitoring->ppSubscriptions, i);
	{
		ContainerRef *pConRef = &pMonitoring->ppSubscriptions[i]->subscribedContainer;
		MonitoredContainer *pMonitored = dbGetMonitoredContainer(pConRef->containerType, pConRef->containerID);
		ContainerSubscription searchSubscription = {0};
		ContainerSubscription *pSubscription = NULL;
		searchSubscription.subscribedServer.containerType = serverType;
		searchSubscription.subscribedServer.containerID = serverID;

		if(!pMonitored)
		{
			bVerified = false;
			continue;
		}

		pSubscription = dbFindContainerSubscription(pMonitored, &searchSubscription);
		if(!devassert(pSubscription))
		{
			bVerified = false;
			continue;
		}

		if(!devassert(pSubscription->refCount == pMonitoring->ppSubscriptions[i]->refCount))
			bVerified = false;
	}
	EARRAY_FOREACH_END;
	return bVerified;
}

// must have gSubscriptionTrackingCS here
static bool VerifySubscriptionCountsByContainer(MonitoredContainer *pMonitored, GlobalType containerType, ContainerID conID)
{
	bool bVerified = true;

	EARRAY_FOREACH_BEGIN(pMonitored->ppSubscriptions, i);
	{
		ContainerRef *pConRef = &pMonitored->ppSubscriptions[i]->subscribedServer;
		MonitoringServer *pMonitoring = dbGetMonitoringServer(pConRef->containerType, pConRef->containerID);
		ContainerSubscriptionInverse searchSubscription = {0};
		ContainerSubscriptionInverse *pSubscription = NULL;
		searchSubscription.subscribedContainer.containerType = containerType;
		searchSubscription.subscribedContainer.containerID = conID;

		if(!pMonitoring)
		{
			bVerified = false;
			continue;
		}

		pSubscription = dbFindMonitoringContainerSubscription(pMonitoring, &searchSubscription);
		if(!devassert(pSubscription))
		{
			bVerified = false;
			continue;
		}

		if(!devassert(pSubscription->refCount == pMonitored->ppSubscriptions[i]->refCount))
			bVerified = false;
	}
	EARRAY_FOREACH_END;

	return bVerified;
}

// must have gSubscriptionTrackingCS here
static bool VerifyOnlineSubscriptionCountsByContainerType(GlobalType containerType, ContainerSubscription *pSubscription)
{
	bool bVerified = true;
	ContainerSubscriptionInverse *pSubscriptionInverse;
	MonitoringServer *pMonitoring;
	int index = -1;
	ContainerSubscriptionInverse searchSubscription = {0};

	searchSubscription.subscribedContainer.containerType = containerType;
	
	pMonitoring = dbGetMonitoringServer(pSubscription->subscribedServer.containerType, pSubscription->subscribedServer.containerID);
	if(!pMonitoring)
		return false;

	index = dbFindOnlineMonitoringContainersSubscription(pMonitoring, &searchSubscription);

	if(!devassert(index != -1))
		bVerified = false;

	pSubscriptionInverse = pMonitoring->ppOnlineSubscriptions[index];
	if(!devassert(pSubscriptionInverse))
		bVerified = false;

	return bVerified;
}

// THIS COMMAND IS NOT THREAD SAFE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
int CountContainerSubscriptionsByContainer(GlobalType containerType, ContainerID containerID)
{
	MonitoredContainer *pMonitored = dbGetMonitoredContainer(containerType, containerID);
	int total = 0;

	if(!pMonitored)
		return 0;

	return pMonitored->refCount;

	return total;
}

// THIS COMMAND IS NOT THREAD SAFE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
int CountContainerSubscriptionsByContainerType(GlobalType containerType)
{
	return monitoredTypes[containerType].refCount;
}

// THIS COMMAND IS NOT THREAD SAFE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
int CountContainerSubscriptionsByServer(GlobalType serverType, ContainerID serverID)
{
	MonitoringServer *pMonitoring = dbGetMonitoringServer(serverType, serverID);
	int total = 0;

	if(!pMonitoring)
		return 0;

	EARRAY_FOREACH_BEGIN(pMonitoring->ppSubscriptions, i);
	{
		total += pMonitoring->ppSubscriptions[i]->refCount;
	}
	EARRAY_FOREACH_END;

	return total;
}

// THIS COMMAND IS NOT THREAD SAFE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
int CountContainerSubscriptionsByServerType(GlobalType serverType)
{
	int total = 0;
	FOR_EACH_IN_STASHTABLE2(monitoringTypes[serverType].pMonitoringServers, elem);
	{
		ContainerID serverID = stashElementGetIntKey(elem);
		MonitoringServer *pMonitoring = stashElementGetPointer(elem);
		EARRAY_FOREACH_BEGIN(pMonitoring->ppSubscriptions, i);
		{
			total += pMonitoring->ppSubscriptions[i]->refCount;
		}
		EARRAY_FOREACH_END;
	}
	FOR_EACH_END;

	return total;
}

// THIS COMMAND IS NOT THREAD SAFE
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
bool VerifySubsciptionIndexesMatch()
{
	bool bVerified = true;
	ARRAY_FOREACH_BEGIN(monitoringTypes, serverType);
	{
		FOR_EACH_IN_STASHTABLE2(monitoringTypes[serverType].pMonitoringServers, elem);
		{
			ContainerID serverID = stashElementGetIntKey(elem);
			MonitoringServer *pMonitoring = stashElementGetPointer(elem);
			if(!devassert(VerifySubscriptionCountsByServer(pMonitoring, serverType, serverID)))
				bVerified = false;
		}
		FOR_EACH_END;
	}
	EARRAY_FOREACH_END;

	ARRAY_FOREACH_BEGIN(monitoredTypes, containerType);
	{
		FOR_EACH_IN_STASHTABLE2(monitoredTypes[containerType].pMonitoredContainers, elem);
		{
			ContainerID conID = stashElementGetIntKey(elem);
			MonitoredContainer *pMonitored = stashElementGetPointer(elem);
			if(!devassert(VerifySubscriptionCountsByContainer(pMonitored, containerType, conID)))
				bVerified = false;
		}
		FOR_EACH_END;

		EARRAY_FOREACH_BEGIN(monitoredTypes[containerType].ppOnlineSubscriptions, i);
		{
			if(!devassert(VerifyOnlineSubscriptionCountsByContainerType(containerType, monitoredTypes[containerType].ppOnlineSubscriptions[i])))
				bVerified = false;
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	return bVerified;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
const char *ContainerSubscriptionReport(U32 eType, ContainerID containerID)
{
	static char *result = NULL;
	MonitoredContainer *pMonitored;

	if(eType == GLOBALTYPE_NONE)
		return NULL;

	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbGetMonitoredContainer(eType, containerID);
	if(!pMonitored)
		return NULL;

	estrPrintf(&result, CON_PRINTF_STR " subscriptions:\n", CON_PRINTF_ARG(eType, containerID));

	EARRAY_FOREACH_BEGIN(pMonitored->ppSubscriptions, i);
	{
		ContainerRef *conRef = &pMonitored->ppSubscriptions[i]->subscribedServer;
		estrConcatf(&result, "\t" CON_PRINTF_STR ": %d\n", CON_PRINTF_ARG(conRef->containerType, conRef->containerID), pMonitored->ppSubscriptions[i]->refCount);
	}
	EARRAY_FOREACH_END;
	LeaveCriticalSection(&gSubscriptionTrackingCS);
	return result;
}

// This command writes out a report of all container subscriptions. Reasonably fast, but exercise caution running on live shards.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB, debug) ACMD_ACCESSLEVEL(9);
bool FullContainerSubscriptionReport()
{
	FILE *pFile = fopen("C:\\SubscriptionDump.txt", "wt");
	if(!pFile)
		return false;

	EnterCriticalSection(&gSubscriptionTrackingCS);
	ARRAY_FOREACH_BEGIN(monitoredTypes, containerType);
	{
		EARRAY_FOREACH_BEGIN(monitoredTypes[containerType].ppOnlineSubscriptions, i);
		{
			ContainerRef *conRef = &monitoredTypes[containerType].ppOnlineSubscriptions[i]->subscribedServer;
			fprintf(pFile, "%s\t%u\t%s\t%u\t%u\n", GlobalTypeToName(containerType), 0, GlobalTypeToName(conRef->containerType), conRef->containerID, 0);
		}
		EARRAY_FOREACH_END;

		FOR_EACH_IN_STASHTABLE2(monitoredTypes[containerType].pMonitoredContainers, elem);
		{
			ContainerID conID = stashElementGetIntKey(elem);
			MonitoredContainer *pMonitored = stashElementGetPointer(elem);
			EARRAY_FOREACH_BEGIN(pMonitored->ppSubscriptions, i);
			{
				if(pMonitored->ppSubscriptions[i]->refCount)
				{
					ContainerRef *conRef = &pMonitored->ppSubscriptions[i]->subscribedServer;

					fprintf(pFile, "%s\t%u\t%s\t%u\t%u\n", GlobalTypeToName(containerType), conID, GlobalTypeToName(conRef->containerType), conRef->containerID, pMonitored->ppSubscriptions[i]->refCount);
				}
			}
			EARRAY_FOREACH_END;
		}
		FOR_EACH_END;
	}
	EARRAY_FOREACH_END;
	LeaveCriticalSection(&gSubscriptionTrackingCS);

	fclose(pFile);
	return true;
}

void QueueGuildCleanup(GWTCmdPacket *packet, ContainerID guildID, ContainerID playerID)
{
	if(GenericDatabaseThreadIsActive() && OnBackgroundGenericDatabaseThread())
	{
		GuildCleanupInfo info = {0};
		info.guildID = guildID;
		info.playerID = playerID;
		assertmsg(packet, "If we are on a background thread, there must be a packet.");
		QueueGuildCleanupOnMainThread(packet, &info);
	}
	else
	{
		PerformGuildCleanup(guildID, playerID);
	}
}

void QueueChatServerCleanup(GWTCmdPacket *packet, ContainerID accountID, ContainerID playerID)
{
	if(GenericDatabaseThreadIsActive() && OnBackgroundGenericDatabaseThread())
	{
		ChatServerCleanupInfo info = {0};
		info.accountID = accountID;
		info.playerID = playerID;
		assertmsg(packet, "If we are on a background thread, there must be a packet.");
		QueueChatServerCleanupOnMainThread(packet, &info);
	}
	else
	{
		PerformChatServerCleanup(accountID, playerID);
	}
}

void PermanentCleanupOfContainer(GWTCmdPacket *packet, GlobalType conType, ContainerID conID)
{
	if (conType == GLOBALTYPE_ENTITYPLAYER) {
		Container *pCon = objGetContainer(conType, conID);
		if (pCon) {
			char pcFoundName[MAX_NAME_LEN];
			if (objPathGetString(".pPlayer.pGuild.iGuildID", pCon->containerSchema->classParse, pCon->containerData, SAFESTR(pcFoundName))) {
				U32 iGuildID = atoi(pcFoundName);
				if (iGuildID) {
					QueueGuildCleanup(packet, iGuildID, conID);
				}
			}

			// Look for other characters for this account ID and forward deletion to GCS if necessary
			if (objPathGetString(".pPlayer.accountID", pCon->containerSchema->classParse, pCon->containerData, SAFESTR(pcFoundName)))
			{
				U32 iAccountID = atoi(pcFoundName);
				QueueChatServerCleanup(packet, iAccountID, conID);
			}
		}
	}
}

void dbContainerDeleteNotify(GWTCmdPacket *packet, GlobalType conType, U32 conID, bool doPermanentCleanup)
{
	U32 iNumberOfRecipients;
	int i;
	MonitoredContainer *pMonitored;
	Container *con;
	SubscriptionHandlerData *threadData = NULL;
	STATIC_THREAD_ALLOC(threadData);

	PERFINFO_AUTO_START("SendingSubscriptionUpdates",1);
	con = objGetContainerEx(conType, conID, false, false, false);
	if (con)
		ExpireCachedSubscriptionCopy(packet, conType, conID);

	EnterCriticalSection(&gSubscriptionTrackingCS);
	pMonitored = dbAddOrGetMonitoredContainer(conType, conID);

	if (!pMonitored || !con)
	{
		LeaveCriticalSection(&gSubscriptionTrackingCS);
		PERFINFO_AUTO_STOP();
		return;
	}

	PERFINFO_AUTO_START("AddingIndividualSubscriptions", 1);
	for (i = 0; i < eaSize(&pMonitored->ppSubscriptions); i++)
	{
		ContainerRef *newRef = StructCreate(parse_ContainerRef);
		StructCopy(parse_ContainerRef, &pMonitored->ppSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
		eaPush(&threadData->ppRecipients, newRef);
		pMonitored->ppSubscriptions[i]->bSent = false;
	}
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("AddingOnlineSubscriptions", 1);
	if (pMonitored->containerOwner.containerType != objServerType() || pMonitored->containerOwner.containerID != objServerID())
	{
		// If this container is online update online subscriptions
		for (i = 0; i < eaSize(&monitoredTypes[conType].ppOnlineSubscriptions); i++)
		{
			ContainerRef *newRef = StructCreate(parse_ContainerRef);
			StructCopy(parse_ContainerRef, &monitoredTypes[conType].ppOnlineSubscriptions[i]->subscribedServer, newRef, 0, 0, 0);
			eaPush(&threadData->ppRecipients, newRef);
		}
	}
	PERFINFO_AUTO_STOP();

	LeaveCriticalSection(&gSubscriptionTrackingCS);

	if (iNumberOfRecipients = eaSize(&threadData->ppRecipients))
	{
		PERFINFO_AUTO_START("sendSubscribedContainerCopyDestroy",1);
		SendDestroyContainerToMultipleRecipients(&threadData->ppRecipients, conType, conID);
		PERFINFO_AUTO_STOP();

		CheckForTooManySubscriptions(conType, conID, iNumberOfRecipients);
	}

	eaClearStruct(&threadData->ppRecipients, parse_ContainerRef);
	PERFINFO_AUTO_STOP();

	if(doPermanentCleanup)
	{
		if (gDatabaseState.databaseType > DBTYPE_INVALID)
		{	//Suck.
			PermanentCleanupOfContainer(packet, conType, conID);
		}
	}
}

void mwt_DBSubscriptionThreadAction(SubscriptionAction *action)
{
	ExecuteSubscriptionAction(action);
	TSMP_FREE(SubscriptionAction, action);
}

// No longer called anywhere, but I've left it in as an example of MWT cleanup
void DestroyDBSubscriptionThread(void)
{
	if (!sSubscriptionThread)
		return;

	// This will block until the WT queue flushes
	while (!mwtInputQueueIsEmpty(sSubscriptionThread))
		Sleep(0);
	mwtSleepUntilDone(sSubscriptionThread);
	mwtDestroy(sSubscriptionThread);
	sSubscriptionThread = NULL;
}

void CreateAndStartDBSubscriptionThread(void)
{
	if (GenericDatabaseThreadIsActive()) return;
	if (sSubscriptionThread) return;

	// You see that this is a MultiWorkerThread. I know what you're thinking. "Why not have multiple subscription threads?"
	//
	// I'll tell you why not. Because it will break everything.
	//
	// The first obvious problem is that subscription requests are currently assumed to be serviced in the order they're received by the
	// DB. Being smart people, we might intuit that this guarantee is only required with respect to each individual container.
	// Unfortunately, we don't really have a good way* to enforce ordering on an individual container basis.
	//
	// Additionally, the CachedSubscriptionCopy object that stores the cached data for a given object has some very special lifecycle
	// rules. (Read the comment above the CachedSubscriptionCopy definition for details.) Because of this, if there are multiple threads,
	// we cannot guarantee that one thread doesn't free a CachedSubscriptionCopy while another thread executes a request that depends on it.
	//
	// *Conceivably, we could deal with both issues by using GenericWorkerThread, which has a concept of a per-object ordering on thread
	// operations (i.e. one thread can't start an action on object X if another thread is currently doing a different action on X).
	// However, this change needs to work with ST release6 right now, where GenericWorkerThreads don't exist.
	sSubscriptionThread = mwtCreate(MAX(dbSubscriptionThreadQueueSize, 2), 0, 1, NULL, NULL, mwt_DBSubscriptionThreadAction, NULL, "DBSubscriptionThread");
}

#include "dbSubscribe_c_ast.c"
