#include "objBackupCache.h"
#include "earray.h"
#include "objContainer.h"
#include "GlobalTypes.h"
#include "textparser.h"
#include "StashTable.h"
#include "logging.h"
#include "timing.h"

#include "AutoGen/objBackupCache_h_ast.h"
#include "AutoGen/objBackupCache_c_ast.h"

AUTO_STRUCT;
typedef struct BackupDataNode
{
	U32 uID;
	void *data; NO_AST
} BackupDataNode;

typedef struct LRUCacheNode
{
	BackupDataNode data;

	struct LRUCacheNode *prev;
	struct LRUCacheNode *next;
} LRUCacheNode;

AUTO_STRUCT;
typedef struct LRUCacheIndex
{
	U32 uID; AST(KEY)
	LRUCacheNode *node; NO_AST
} LRUCacheIndex;

AUTO_STRUCT;
typedef struct LRUCacheStruct
{
	LRUCacheNode *pHead; NO_AST
	LRUCacheNode *pTail; NO_AST
	LRUCacheIndex **ppCacheIndex;
	int iCount;
	int iMaxCacheSize; AST(DEFAULT(100)) // arbitrary default of 100
	int iLRUMissCount;
} LRUCacheStruct;

AUTO_STRUCT;
typedef struct StashCacheStruct
{
	StashTable stCache;
} StashCacheStruct;

AUTO_STRUCT;
typedef struct BackupCacheStruct
{
	GlobalType eType; AST(KEY)
	
	BackupCacheType eCacheType;
	LRUCacheStruct *lruCache;
	StashCacheStruct *stashCache;
} BackupCacheStruct;

static BackupCacheStruct **sppRegisteredCaches = NULL;

static void LRUCacheInsert(BackupCacheStruct *cache, LRUCacheNode *node, bool bNewCache)
{
	assertmsgf(cache->lruCache, "GlobalType %s is not registered for LRU Cache.", GlobalTypeToName(cache->eType));
	if (bNewCache) // this is a new entry
	{
		ContainerStore *store = objFindContainerStoreFromType(cache->eType);
		while (cache->lruCache->iCount >= cache->lruCache->iMaxCacheSize)
		{
			LRUCacheNode *lruNode = cache->lruCache->pTail;
			int idx = eaIndexedFindUsingInt(&cache->lruCache->ppCacheIndex, lruNode->data.uID);
			LRUCacheIndex *lruIndex = cache->lruCache->ppCacheIndex[idx];
			assert(store && idx > -1);
			StructDestroyVoid(store->containerSchema->classParse, lruNode->data.data);
			cache->lruCache->pTail = lruNode->prev;
			if (lruNode->prev)
				lruNode->prev->next = NULL;
			eaRemove(&cache->lruCache->ppCacheIndex, idx);
			StructDestroy(parse_LRUCacheIndex, lruIndex);
			free(lruNode);
			cache->lruCache->iCount--;
		}
		cache->lruCache->iCount++;
		if (!cache->lruCache->pTail) // cache is currently empty - set the tail
			cache->lruCache->pTail = node;
	}
	else // this is an old entry that is being moved to the front
	{
		if (node == cache->lruCache->pHead)
			return;
		if (node == cache->lruCache->pTail && cache->lruCache->iCount > 1)
			cache->lruCache->pTail = node->prev; // update tail if old tail was just moved to the front

		if (node->prev)
			node->prev->next = node->next;
		if (node->next)
			node->next->prev = node->prev;
	}
	// Passed-in node is always the new head
	if (cache->lruCache->pHead)
		cache->lruCache->pHead->prev = node;
	node->next = cache->lruCache->pHead;
	node->prev = NULL;
	cache->lruCache->pHead = node;
}

static char sLRUMissLogPath[MAX_PATH] = "";
static bool sbTrackLRUMissDetails = false;

void LRUCache_EnableMissDetails(bool bEnable)
{
	sbTrackLRUMissDetails = bEnable;
}

void LRUCacheSetMissLogFile (const char *path)
{
	strcpy(sLRUMissLogPath, path);
}

static void LRUCacheAddNew (BackupCacheStruct *cache, U32 uID, void *dataCopy)
{
	LRUCacheNode *node = calloc(1, sizeof(LRUCacheNode));
	LRUCacheIndex *index = StructCreate(parse_LRUCacheIndex);

	assertmsgf(cache->lruCache, "GlobalType %s is not registered for LRU Cache.", GlobalTypeToName(cache->eType));
	index->uID = node->data.uID = uID;
	node->data.data = dataCopy;
	LRUCacheInsert(cache, node, true);
	index->node = node;
	eaIndexedAdd(&cache->lruCache->ppCacheIndex, index);

	if (sLRUMissLogPath[0])
	{
		if (sbTrackLRUMissDetails)
			filelog_printf(sLRUMissLogPath, "Cache Miss, %d\n", uID);
		cache->lruCache->iLRUMissCount++;
	}
}

void BackupCacheOncePerFrame(void)
{
	static U32 uLastHourlyTime = 0;
	if (sLRUMissLogPath[0])
	{
		U32 uTime = timeSecondsSince2000();
		
		if (!uLastHourlyTime)
			uLastHourlyTime = uTime;
		else if (uTime - uLastHourlyTime > 3600)
		{
			int i,size;
			size = eaSize(&sppRegisteredCaches);
			for (i=0; i<size; i++)
			{
				if (sppRegisteredCaches[i]->eCacheType == BACKUPCACHE_LRU)
				{
					printf("Cache Misses for [%s]: %d\n", GlobalTypeToName(sppRegisteredCaches[i]->eType), 
						sppRegisteredCaches[i]->lruCache->iLRUMissCount);
					filelog_printf(sLRUMissLogPath, "Cache Misses for [%s]: %d\n", GlobalTypeToName(sppRegisteredCaches[i]->eType), 
						sppRegisteredCaches[i]->lruCache->iLRUMissCount);
					sppRegisteredCaches[i]->lruCache->iLRUMissCount = 0;
				}
			}
			uLastHourlyTime = uTime;
		}
	}
}

void * BackupCacheGet(Container *con)
{
	if (sppRegisteredCaches)
	{
		BackupCacheStruct *cache = eaIndexedGetUsingInt(&sppRegisteredCaches, con->containerType);
		assert(cache);

		switch (cache->eCacheType)
		{
		case BACKUPCACHE_STASH:
			{
				BackupDataNode *node;
				assert(cache->stashCache && cache->stashCache->stCache);
				if (stashIntFindPointer(cache->stashCache->stCache, con->containerID, &node))
					return node->data;
				else
				{
					node = StructCreate(parse_BackupDataNode);
					node->uID = con->containerID;
					node->data = StructCloneVoid(con->containerSchema->classParse, con->containerData);
					stashIntAddPointer(cache->stashCache->stCache, node->uID, node, false);
					return node->data;
				}
			}
		xcase BACKUPCACHE_LRU:
			{
				void *dataCopy = NULL;
				assert(cache->lruCache);
				if (cache->lruCache->ppCacheIndex)
				{
					LRUCacheIndex *index = eaIndexedGetUsingInt(&cache->lruCache->ppCacheIndex, con->containerID);
					if (index)
					{
						LRUCacheInsert(cache, index->node, false);
						return index->node->data.data;
					}
				}
				dataCopy = StructCloneVoid(con->containerSchema->classParse, con->containerData);
				LRUCacheAddNew(cache, con->containerID, dataCopy);
				return dataCopy;
			}
		}
	}
	else
		assertmsgf(0, "GlobalType %s is not registered for backup caching.", GlobalTypeToName(con->containerType));
	return NULL;
}

void BackupCache_RegisterType(GlobalType eType, BackupCacheType eCacheType, int iSize)
{
	BackupCacheStruct *cache = NULL;
	if (!sppRegisteredCaches)
		eaIndexedEnable(&sppRegisteredCaches, parse_BackupCacheStruct);
	cache = eaIndexedGetUsingInt(&sppRegisteredCaches, eType);

	if (cache)
	{
		if (cache->eCacheType == eCacheType)
		{   // already using same type - update size and exit
			switch (cache->eCacheType)
			{
			case BACKUPCACHE_STASH: // update stash table size if it's currently smaller
				if (stashGetMaxSize(cache->stashCache->stCache) < (U32) iSize)
				{
					stashTableSetMinSize(cache->stashCache->stCache, iSize);
					printf("Stash Cache size increased to %d\n\n", iSize);
				}
				else
					printf("Stash Cache not resized because it is already larger than target size (%d)\n\n", iSize);
			xcase BACKUPCACHE_LRU: // update the cache size
				if (iSize > eaCapacity(&cache->lruCache->ppCacheIndex))
					eaSetCapacity(&cache->lruCache->ppCacheIndex, iSize);
				cache->lruCache->iMaxCacheSize = iSize;
				printf("LRU Cache Size increased to %d\n\n", iSize);
			}
			return; 
		}
		if (eCacheType == BACKUPCACHE_STASH)
		{
			if (cache->eCacheType == BACKUPCACHE_LRU)
			{   // This moves all LRU cache entries into the stash cache and frees LRU overhead memory
				LRUCacheNode *curNode = cache->lruCache->pHead, *nextNode;
				cache->stashCache = StructCreate(parse_StashCacheStruct);
				cache->stashCache->stCache = stashTableCreateInt(iSize);
				cache->eCacheType = eCacheType;

				while (curNode)
				{
					BackupDataNode *node = StructCreate(parse_BackupDataNode);
					node->uID = curNode->data.uID;
					node->data = curNode->data.data;
					stashIntAddPointer(cache->stashCache->stCache, node->uID, node, false);
					nextNode = curNode->next;
					free (curNode);
					curNode = nextNode;
				}
				StructDestroy(parse_LRUCacheStruct, cache->lruCache);
				cache->lruCache = NULL;
				return;
			}
		}
		else if (eCacheType == BACKUPCACHE_LRU)
		{
			if (cache->eCacheType == BACKUPCACHE_STASH)
			{   // This just destroys everything
				StashTableIterator iter = {0};
				StashElement elem;
				ContainerStore *conStore = objFindContainerStoreFromType(cache->eType);
				ParseTable *pt = conStore->containerSchema->classParse;

				assert(conStore && pt);
				cache->lruCache = StructCreate(parse_LRUCacheStruct);
				cache->lruCache->iMaxCacheSize = iSize;
				cache->eCacheType = eCacheType;
				eaIndexedEnable(&cache->lruCache->ppCacheIndex, parse_LRUCacheIndex);
				if (iSize)
					eaSetCapacity(&cache->lruCache->ppCacheIndex, iSize);

				stashGetIterator(cache->stashCache->stCache, &iter);
				while (stashGetNextElement(&iter, &elem) )
				{
					BackupDataNode *node = stashElementGetPointer(elem);
					StructDestroyVoid(pt, node->data);
				}
				stashTableDestroyStruct(cache->stashCache->stCache, NULL, parse_BackupDataNode);
				cache->stashCache->stCache = NULL; // so it isn't destroy below
				StructDestroy(parse_StashCacheStruct, cache->stashCache);
				cache->stashCache = NULL;
				return;
			}
		}
		assertmsgf(0, "Unsupported cache conversion from %d to %d.", cache->eCacheType, eCacheType);
	}
	else
	{
		cache = StructCreate(parse_BackupCacheStruct);
		switch (eCacheType)
		{
		case BACKUPCACHE_STASH:
			{
				cache->stashCache = StructCreate(parse_StashCacheStruct);
				cache->stashCache->stCache = stashTableCreateInt(iSize);
			}
		xcase BACKUPCACHE_LRU:
			{
				cache->lruCache = StructCreate(parse_LRUCacheStruct);
				cache->lruCache->iMaxCacheSize = iSize;
				eaIndexedEnable(&cache->lruCache->ppCacheIndex, parse_LRUCacheIndex);
				if (iSize)
					eaSetCapacity(&cache->lruCache->ppCacheIndex, iSize);
			}
		}
		cache->eType = eType;
		cache->eCacheType = eCacheType;
		eaIndexedAdd(&sppRegisteredCaches, cache);
	}
}

#include "AutoGen/objBackupCache_h_ast.c"
#include "AutoGen/objBackupCache_c_ast.c"