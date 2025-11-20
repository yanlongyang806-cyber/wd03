/***************************************************************************



***************************************************************************/

#include "objContainer.h"
#include "logging.h"
#include "objContainerIO.h"
#include "objIndex.h"
#include "genericlist.h"
#include "ThreadSafeMemoryPool.h"
#include "Array.h"


#include "EString.h"
#include "tokenstore.h"
#include "tokenstore_inline.h"
#include "stashtable.h"
#include "timing.h"
#include "file.h"
#include "timing.h"
#include "winInclude.h"
#include "Expression.h"
#include "cmdparse.h"
#include "UtilitiesLib.h"
#include "mutex.h"
#include "GlobalTypes_h_ast.h"
#include "GenericWorkerThread.h"

#include "autogen/objcontainer_h_ast.h"
#include "autogen/objcontainer_c_ast.h"

static U32 gCurrentlyLockedContainerStores = 0;
U32 objGetCurrentlyLockedContainerStoreCount(void)
{
	return gCurrentlyLockedContainerStores;
}

void dcqIndexUpdate(DeletedContainerQueueEntry* item, int index);
void dcqPush(ContainerStore *base, DeletedContainerQueueEntry *entry);
void dcqRemove(ContainerStore *base, DeletedContainerQueueEntry *entry);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

static bool sbCrashOnPlayerLeaveMap = false;
AUTO_CMD_INT(sbCrashOnPlayerLeaveMap, CrashOnPlayerLeaveMap) ACMD_CATEGORY(debug);

static int gUnloadOnCachedDelete = true;
AUTO_CMD_INT(gUnloadOnCachedDelete, UnloadOnCachedDelete) ACMD_CATEGORY(debug);

static int gKeepFileDataOnUnpack = true;
AUTO_CMD_INT(gKeepFileDataOnUnpack, KeepFileDataOnUnpack) ACMD_CATEGORY(debug);

ContainerID gBaseContainerID = 0;
ContainerID gMaxContainerID = 0;
ContainerID gContainerIDAlertThreshold = 0;
bool gAllowChangeOfBaseContainerID = false;
AUTO_CMD_INT(gAllowChangeOfBaseContainerID, AllowChangeOfBaseContainerID) ACMD_CATEGORY(ObjectDB);

AUTO_COMMAND ACMD_COMMANDLINE;
void SetBaseContainerIDs(ContainerID baseContainerID, ContainerID maxContainerID)
{
	ATOMIC_INIT_BEGIN;
	U32 difference;
	
	// If they are both set, make sure that max is bigger than base
	assert(!baseContainerID || !maxContainerID || maxContainerID > baseContainerID);

	gBaseContainerID = baseContainerID;
	gMaxContainerID = maxContainerID;
	if(gMaxContainerID)
	{
		// Only setup for alerts if there is a max set
		difference = gMaxContainerID - gBaseContainerID;
		gContainerIDAlertThreshold = gBaseContainerID + 3*(difference/4);
	}
	ATOMIC_INIT_END;
}

ContainerRepository gContainerRepository;

CommitContainerStateCallback stateChangeCB;

int ForceKeepLazyLoadFileDataWithStore(ContainerStore *store)
{
	assert(store);
	return store && store->lazyLoad && gKeepFileDataOnUnpack;
}

int ForceKeepLazyLoadFileData(GlobalType containerType)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	if(!store)
		return false;

	return ForceKeepLazyLoadFileDataWithStore(store);
}

void objRegisterGlobalStateCommitCallback(CommitContainerStateCallback cb)
{
	stateChangeCB = cb;
}

void objInitContainerObject(ContainerSchema *classDef, void *object)
{
	if (classDef->initializeCallback)
	{
		classDef->initializeCallback(classDef,object);
	}
}

void objDeInitContainerObject(ContainerSchema *classDef, void *object)
{
	if (classDef->deInitCallback)
	{
		classDef->deInitCallback(classDef,object);
	}
	else
	{
		StructDeInitVoid(classDef->classParse,object);
	}

}

void objDestroyContainerObjectEx(ContainerSchema *classDef, void *object, const char* file, int line)
{
	if (classDef->destroyCallback)
	{
		classDef->destroyCallback(classDef,object, file, line);
	}
	else
	{
		StructDestroyVoid(classDef->classParse,object);
	}
}
void *objCreateContainerObject(ContainerSchema *classDef, char *pComment)
{
	if (classDef->createCallback)
	{
		return classDef->createCallback(classDef);
	}
	else
	{
		return objCreateTempContainerObject(classDef, pComment);
	}
}

// These don't use the normal creation callbacks, because they are not long lasting

void objDestroyTempContainerObject(ContainerSchema *classDef, void *object)
{
	StructDestroyVoid(classDef->classParse,object);
}
void *objCreateTempContainerObject(ContainerSchema *classDef, char *pComment)
{
	void *object;
	object = StructCreateWithComment(classDef->classParse, pComment);
	return object;
}

static ThreadSafeMemoryPool ContainerPool;
TSMP_DEFINE(TrackedContainer);

AUTO_RUN;
void objContainerPoolInit(void)
{
	threadSafeMemoryPoolInit(&ContainerPool, 256, sizeof(Container), "Container");
}

static void InitTrackedContainersInRepository(void)
{
	TSMP_SMART_CREATE(TrackedContainer, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_TrackedContainer, &TSMP_NAME(TrackedContainer));

	objLockGlobalContainerRepository();
	gContainerRepository.modifiedContainers.lookupContainerByRef = stashTableCreateFixedSize(256, sizeof(ContainerRef));
	gContainerRepository.modifiedEntityPlayers.lookupContainerByRef = stashTableCreateFixedSize(256, sizeof(ContainerRef));
	gContainerRepository.modifiedEntitySavedPets.lookupContainerByRef = stashTableCreateFixedSize(256, sizeof(ContainerRef));
	gContainerRepository.removedContainersLocking.lookupContainerByRef = stashTableCreateFixedSize(256, sizeof(ContainerRef));
	objUnlockGlobalContainerRepository();
}


Container *objCreateContainer(ContainerSchema *schema)
{
	Container *object;
	object = threadSafeMemoryPoolCalloc(&ContainerPool);
	object->containerSchema = schema;
	object->containerType = schema->containerType;
	return object;
}

bool gbEnableObjectLocks = false;
void enableObjectLocks(bool enable)
{
	gbEnableObjectLocks = enable;
}

// This should only be called from the main thread.
ObjectLock *objGetObjectLock(GlobalType containerType, ContainerID containerID)
{
	ContainerStore *store = objFindOrCreateContainerStoreFromType(containerType);
	ObjectLock *objectLock = NULL;
	assert(store); //objFindOrCreateContainerStore only returns NULL if the containerstore does not exist and there is no schema for the type.
	objLockContainerStore_ReadOnly(store);
	if(!stashIntFindPointer(store->objectLocks, containerID, &objectLock))
	{
		objUnlockContainerStore_ReadOnly(store);
		objLockContainerStore_Write(store);
		if(!stashIntFindPointer(store->objectLocks, containerID, &objectLock))
		{
			objectLock = initializeObjectLock(&store->lockedContainerCount);
			SetObjectLockContainerRef(objectLock, containerType, containerID);
			stashIntAddPointer(store->objectLocks, containerID, objectLock, true);
		}
		objUnlockContainerStore_Write(store);
		return objectLock;
	}
	objUnlockContainerStore_ReadOnly(store);
	return objectLock;
}

void objDestroyContainerEx(Container *object, const char* file, int line)
{
	if (sbCrashOnPlayerLeaveMap && object->containerType == GLOBALTYPE_ENTITYPLAYER)
	{
		assertmsg(0, "Asserting because of CrashOnPlayerLeaveMap");
	}

	PERFINFO_AUTO_START("objDestroyContainer",1);
	if (object->isTemporary)
	{
		objDestroyTempContainerObject(object->containerSchema,object->containerData);
	}
	else
	{
		if (object->savedUpdate)
		{
			objHandleDestroyDuringSave(object);
		}

		objDeInitContainerObject(object->containerSchema,object->containerData);
		objDestroyContainerObjectEx(object->containerSchema,object->containerData, file, line);
	}

	DestroyObjectIndexHeader(&object->header);

	SAFE_FREE(object->rawHeader);
	SAFE_FREE(object->fileData);
	threadSafeMemoryPoolFree(&ContainerPool, object);
	PERFINFO_AUTO_STOP();
}

// If you pass cachedpaths to this function, it must be an empty EArray
// It will be filled with the resolved ObjectPaths corresponding to each ObjectPathOperation, possibly including NULLs for unresolved paths
bool objUpdateIndexRemove(Container *object, ObjectPathOperation **paths, ObjectPath ***cachedpaths)
{
	ContainerStore *store;
	if (object->isTemporary)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (store = objFindContainerStoreFromType(object->containerType))
	{
		ObjectPath **ppPathArr = NULL;

		if(!cachedpaths)
		{
			eaCreate(&ppPathArr);
			eaSetCapacity(&ppPathArr, eaSize(&paths));
		}

		EARRAY_FOREACH_BEGIN(paths, i);
		{
			ObjectPath *path = NULL;
			ParserResolvePathEx(paths[i]->pathEString, store->containerSchema->classParse, NULL, NULL, NULL, NULL, NULL, &path, NULL, NULL, NULL, OBJPATHFLAG_INCREASEREFCOUNT);
			eaPush(cachedpaths?cachedpaths:&ppPathArr, path);
		}
		EARRAY_FOREACH_END;

		EARRAY_FOREACH_BEGIN(store->indices, i);
		{
			if (objIndexPathsAffected(store->indices[i], cachedpaths?(*cachedpaths):ppPathArr))
			{
				objIndexObtainWriteLock(store->indices[i]);
				if(store->requiresHeaders)
					objIndexRemove(store->indices[i], object->header);
				else
					objIndexRemove(store->indices[i], object->containerData);
			}
		}
		EARRAY_FOREACH_END;

		if(!cachedpaths)
		{
			eaDestroyEx(&ppPathArr, ObjectPathDestroy);
		}
	}
	PERFINFO_AUTO_STOP();
	return true;
}

// This is not safe to use on the ObjectDB due to threading
void *objContainerDataFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata)
{
	if (store->requiresHeaders)
	{
		ObjectIndexHeader *header = (ObjectIndexHeader*) idxdata;
		Container *con = objGetContainer(store->containerSchema->containerType, header->containerId);

		return con->containerData;
	}
	else
	{
		return idxdata;
	}
}

ObjectIndexHeader *objHeaderFromIndexData(ContainerStore *store, ObjectHeaderOrData *indexData)
{
	if (store->requiresHeaders)
		return (ObjectIndexHeader*)indexData;
	else
		return NULL;
}

U32 objContainerIDFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata)
{
	if (store->requiresHeaders)
	{
		ObjectIndexHeader *header = (ObjectIndexHeader*) idxdata;
		return header->containerId;
	}
	else
	{
		int resultID;
		if (objGetKeyInt(store->containerSchema->classParse,idxdata,&resultID))
			return resultID;
		else
			return 0;
	}
}

U32 objVirtualShardIDFromIndexData(ContainerStore *store, ObjectHeaderOrData *idxdata)
{
	if (store->requiresHeaders)
	{
		ObjectIndexHeader *header = (ObjectIndexHeader*) idxdata;
		return header->virtualShardId;
	}
	else
	{
//fixme switch on different container types?
		assert(0); //if you hit this talk to awerner or jpanttaja for now, will be fixed
	}
}

void *objIndexGetNextContainerData(ObjectIndexIterator *iter, ContainerStore* store)
{
	ObjectHeaderOrData *indexData = objIndexGetNext(iter);

	if (!indexData)
		return NULL;

	return objContainerDataFromIndexData(store, indexData);
}

void *objIndexGetNextMatchContainerData(ObjectIndexIterator *iter, ObjectIndexKey *key, int match, ContainerStore *store)
{
	ObjectHeaderOrData *indexData = objIndexGetNextMatch(iter, key, match);

	if (!indexData)
		return NULL;

	return objContainerDataFromIndexData(store, indexData);
}

bool objIndexGetContainerData(ObjectIndex *oi, ObjectIndexKey *key, S64 n, ContainerStore *store, void **strfound)
{
	ObjectHeaderOrData *indexData = NULL;
	bool retval;

	retval = objIndexGet(oi, key, n, &indexData);

	if(indexData)
		*strfound = objContainerDataFromIndexData(store, indexData);

	return retval;
}

static __forceinline bool PopulateExtraData(ContainerSchema *schema, void *containerData, ObjectIndexHeaderField field, const char **dataPointer)
{
	bool ok = true;
	const char *extraPath;
	char tempStr[MAX_NAME_LEN] = "";
	extraPath = objIndexGetExtraDataPath(field);
	if(extraPath)
	{
		ok &= objPathGetString(extraPath, schema->classParse, containerData, SAFESTR(tempStr));
		if(ok)
			*dataPointer = strdup(tempStr);
	}

	return ok;
}

int objGenerateHeaderEx(void *containerData, ObjectIndexHeader **outputheader, ContainerSchema *schema)
{
	ObjectIndexHeader_NoConst *header_noConst = (ObjectIndexHeader_NoConst *)*outputheader;
	char tempStr[MAX_NAME_LEN];
	int ok = true;

	if(!*outputheader)
	{
		*outputheader = calloc(1, sizeof(*header_noConst));
		header_noConst = (ObjectIndexHeader_NoConst *)*outputheader;
	}
	else
	{
		// OBJ_HEADER_PUB_ACCOUNTNAME
		SAFE_FREE(header_noConst->pubAccountName);
		// OBJ_HEADER_PRIV_ACCOUNTNAME
		SAFE_FREE(header_noConst->privAccountName);
		// OBJ_HEADER_SAVEDNAME
		SAFE_FREE(header_noConst->savedName);
		// OBJ_HEADER_EXTRA_DATA_1
		SAFE_FREE(header_noConst->extraData1);
		// OBJ_HEADER_EXTRA_DATA_2
		SAFE_FREE(header_noConst->extraData2);
		// OBJ_HEADER_EXTRA_DATA_3
		SAFE_FREE(header_noConst->extraData3);
		// OBJ_HEADER_EXTRA_DATA_4
		SAFE_FREE(header_noConst->extraData4);
		// OBJ_HEADER_EXTRA_DATA_5
		SAFE_FREE(header_noConst->extraData5);
	}

	// OBJ_HEADER_CONTAINERID
	ok &= objPathGetInt(".myContainerId", schema->classParse, containerData, &header_noConst->containerId);
	// OBJ_HEADER_ACCOUNTID
	ok &= objPathGetInt(".pPlayer.accountId", schema->classParse, containerData, &header_noConst->accountId);
	// OBJ_HEADER_CREATEDTIME
	ok &= objPathGetInt(".pPlayer.iCreatedTime", schema->classParse, containerData, &header_noConst->createdTime);
	// OBJ_HEADER_LEVEL
	{
		//Try both v1 and v2 level locations.
		bool bLevelFound = objPathGetInt(".pInventoryV2.ppLiteBags[Numeric].ppIndexedLiteSlots[Level].count", schema->classParse, containerData, &header_noConst->level);

		if (!bLevelFound)
			bLevelFound = objPathGetInt(".pInventory.ppInventorybags[Numeric].ppIndexedInventorySlots[Level].pItem.NumericValue", schema->classParse, containerData, &header_noConst->level);
		ok &= bLevelFound;
	}
	// OBJ_HEADER_FIXUP_VERSION
	ok &= objPathGetInt(".pSaved.fixupVersion", schema->classParse, containerData, &header_noConst->fixupVersion);
	// OBJ_HEADER_LAST_PLAYED_TIME
	ok &= objPathGetInt(".pPlayer.iLastPlayedTime", schema->classParse, containerData, &header_noConst->lastPlayedTime);
	// OBJ_HEADER_PUB_ACCOUNTNAME
	ok &= objPathGetString(".pPlayer.publicAccountName", schema->classParse, containerData, SAFESTR(tempStr));
	header_noConst->pubAccountName = strdup(tempStr);
	// OBJ_HEADER_PRIV_ACCOUNTNAME
	ok &= objPathGetString(".pPlayer.privateAccountName", schema->classParse, containerData, SAFESTR(tempStr));
	header_noConst->privAccountName = strdup(tempStr);
	// OBJ_HEADER_SAVEDNAME
	ok &= objPathGetString(".pSaved.savedName", schema->classParse, containerData, SAFESTR(tempStr));
	header_noConst->savedName = strdup(tempStr);
	// OBJ_HEADER_VIRTUAL_SHARD_ID
	ok &= objPathGetInt(".pPlayer.iVirtualShardId", schema->classParse, containerData, &header_noConst->virtualShardId);
	//devassertmsg(ok, "Failed to create header data");

	// Failing to populate extra data should not cause the overall header generation to fail
	// OBJ_HEADER_EXTRA_DATA_1
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_1, &header_noConst->extraData1);
	// OBJ_HEADER_EXTRA_DATA_2
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_2, &header_noConst->extraData2);
	// OBJ_HEADER_EXTRA_DATA_3
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_3, &header_noConst->extraData3);
	// OBJ_HEADER_EXTRA_DATA_4
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_4, &header_noConst->extraData4);
	// OBJ_HEADER_EXTRA_DATA_5
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_5, &header_noConst->extraData5);

	header_noConst->extraDataOutOfDate = 0;

	*outputheader = (ObjectIndexHeader *)header_noConst;

	return ok;
}

void objGenerateHeaderExtraData(void *containerData, ObjectIndexHeader **outputheader, ContainerSchema *schema)
{
	ObjectIndexHeader_NoConst *header_noConst = (ObjectIndexHeader_NoConst *)*outputheader;

	assertmsg(*outputheader, "Trying to create extra data when the header does not already exist.");

	// OBJ_HEADER_EXTRA_DATA_1
	SAFE_FREE(header_noConst->extraData1);
	// OBJ_HEADER_EXTRA_DATA_2
	SAFE_FREE(header_noConst->extraData2);
	// OBJ_HEADER_EXTRA_DATA_3
	SAFE_FREE(header_noConst->extraData3);
	// OBJ_HEADER_EXTRA_DATA_4
	SAFE_FREE(header_noConst->extraData4);
	// OBJ_HEADER_EXTRA_DATA_5
	SAFE_FREE(header_noConst->extraData5);

	// OBJ_HEADER_EXTRA_DATA_1
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_1, &header_noConst->extraData1);
	// OBJ_HEADER_EXTRA_DATA_2
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_2, &header_noConst->extraData2);
	// OBJ_HEADER_EXTRA_DATA_3
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_3, &header_noConst->extraData3);
	// OBJ_HEADER_EXTRA_DATA_4
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_4, &header_noConst->extraData4);
	// OBJ_HEADER_EXTRA_DATA_5
	PopulateExtraData(schema, containerData, OBJ_HEADER_EXTRA_DATA_5, &header_noConst->extraData5);

	header_noConst->extraDataOutOfDate = 0;

	*outputheader = (ObjectIndexHeader *)header_noConst;
}

void objVerifyHeader(Container *container, void *containerData, ContainerSchema *schema)
{
	ObjectIndexHeader *verifyHeader = callocStruct(ObjectIndexHeader);
	ObjectIndexHeader *oldHeader = container->header;

	devassert(oldHeader);

	objGenerateHeaderEx(containerData, &verifyHeader, schema);

	// OBJ_HEADER_CONTAINERID
	devassert(verifyHeader->containerId == container->header->containerId);
	// OBJ_HEADER_ACCOUNTID
	devassert(verifyHeader->accountId == container->header->accountId);
	// OBJ_HEADER_CREATEDTIME
	devassert(verifyHeader->createdTime == container->header->createdTime);
	// OBJ_HEADER_LEVEL
	devassert(verifyHeader->level == container->header->level);
	// OBJ_HEADER_FIXUP_VERSION
	devassert(verifyHeader->fixupVersion == container->header->fixupVersion);
	// OBJ_HEADER_LAST_PLAYED_TIME
	devassert(verifyHeader->lastPlayedTime == container->header->lastPlayedTime);
	// OBJ_HEADER_PUB_ACCOUNTNAME
	devassert(!stricmp(verifyHeader->pubAccountName, container->header->pubAccountName));
	// OBJ_HEADER_PRIV_ACCOUNTNAME
	devassert(!stricmp(verifyHeader->privAccountName, container->header->privAccountName));
	// OBJ_HEADER_SAVEDNAME
	devassert(!stricmp(verifyHeader->savedName, container->header->savedName));
	// OBJ_HEADER_EXTRA_DATA_1
	devassert(!stricmp(verifyHeader->extraData1, container->header->extraData1));
	// OBJ_HEADER_EXTRA_DATA_2
	devassert(!stricmp(verifyHeader->extraData2, container->header->extraData2));
	// OBJ_HEADER_EXTRA_DATA_3
	devassert(!stricmp(verifyHeader->extraData3, container->header->extraData3));
	// OBJ_HEADER_EXTRA_DATA_4
	devassert(!stricmp(verifyHeader->extraData4, container->header->extraData4));
	// OBJ_HEADER_EXTRA_DATA_5
	devassert(!stricmp(verifyHeader->extraData5, container->header->extraData5));
	// OBJ_HEADER_VIRTUAL_SHARD_ID
	devassert(verifyHeader->virtualShardId == container->header->virtualShardId);

	free(verifyHeader);
}

// If you pass cachedpaths to this function, it must be an already-populated array of ObjectPaths
// The ObjectPath at (*cachedpaths)[i], for example, must correspond to the resolved path of the operation paths[i]
// As a side effect, cachedpaths will be cleared and the ObjectPaths destroyed after this function
bool objUpdateIndexInsert(Container *object, ObjectPathOperation **paths, ObjectPath ***cachedpaths)
{
	ContainerStore *store;
	int reparsedHeader = false;
	if (object->isTemporary)
		return false;

	PERFINFO_AUTO_START_FUNC();

	if (store = objFindContainerStoreFromType(object->containerType))
	{
		ObjectPath **pathArr = NULL;

		if(!cachedpaths)
		{
			eaCreate(&pathArr);
			eaSetCapacity(&pathArr, eaSize(&paths));

			EARRAY_FOREACH_BEGIN(paths, i);
			{
				ObjectPath *path = NULL;
				ParserResolvePathEx(paths[i]->pathEString, store->containerSchema->classParse, NULL, NULL, NULL, NULL, NULL, &path, NULL, NULL, NULL, OBJPATHFLAG_INCREASEREFCOUNT);
				eaPush(&pathArr, path);
			}
			EARRAY_FOREACH_END;
		}

		EARRAY_FOREACH_BEGIN(store->indices, i);
		{
			//pathsAffected assumes that the paths being compared have the same rootTpi.
			if (objIndexPathsAffected(store->indices[i], cachedpaths?(*cachedpaths):pathArr))
			{
				if (store->requiresHeaders)
				{
					if(!reparsedHeader)
					{
						objGenerateHeader(object, store->containerSchema);
						reparsedHeader = true;
					}
					objIndexInsert(store->indices[i], object->header);
				}
				else
					objIndexInsert(store->indices[i], object->containerData);
				objIndexReleaseWriteLock(store->indices[i]);
			}
		}
		EARRAY_FOREACH_END;

		//much need to destroy the ObjectPaths since Kelvin is bad.
		eaClearEx(cachedpaths?cachedpaths:&pathArr, ObjectPathDestroy);

		if(!cachedpaths)
		{
			eaDestroy(&pathArr);
		}
	}
	PERFINFO_AUTO_STOP();
	return true;
}

int objUpdateApplyOperations(Container *object, ObjectPathOperation **pathOperations)
{
	int i, j, indices = 0, success = 1;
	int *indicesAffected = NULL;
	ParseTable *table_in = object->containerSchema->classParse;
	void *structptr_in = object->containerData;
	ContainerStore *store = objFindContainerStoreFromType(object->containerType);
	bool reparsedHeader = false;

	if (!store)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	indices = eaSize(&store->indices);

	if (indices && !object->isTemporary)
	{
		eaiStackCreate(&indicesAffected, indices);
		for (j = 0; j < indices; ++j)
			eaiPush(&indicesAffected, 0);
	}

	for (i = 0; i < eaSize(&pathOperations) && success; ++i)
	{
		ObjectPathOperation *op = pathOperations[i];
		ObjectPath *path = NULL;
		ParseTable *table = NULL;
		int column = 0;
		void *structptr = NULL;
		int index = 0;
		int pathFlags = OBJPATHFLAG_DONTLOOKUPROOTPATH | OBJPATHFLAG_INCREASEREFCOUNT;

		success = ParserResolvePathEx(op->pathEString, table_in, structptr_in, &table, &column, &structptr, &index, &path, NULL, NULL, NULL, pathFlags);

		if (!success)
			goto next;

		if (!object->isTemporary)
		{
			for (j = 0; j < indices; ++j)
			{
				ObjectIndex *oi = store->indices[j];

				if (indicesAffected[j]) continue;
				if (!objIndexPathAffected(oi, path)) continue;

				PERFINFO_AUTO_START("Remove from Index", 1);
				objIndexObtainWriteLock(oi);
				if (store->requiresHeaders)
					objIndexRemove(oi, object->header);
				else
					objIndexRemove(oi, structptr_in);

				indicesAffected[j] = 1;
				PERFINFO_AUTO_STOP();
			}
		}

		success = objPathApplySingleOperation(table, column, structptr, index, op->op, op->valueEString, op->quotedValue, NULL);

	next:
		if (path) ObjectPathDestroy(path);
	}

	if (!object->isTemporary)
	{
		for (j = 0; j < indices; ++j)
		{
			ObjectIndex *oi = store->indices[j];

			if (!indicesAffected[j]) continue;

			PERFINFO_AUTO_START("Insert into Index", 1);
			if (store->requiresHeaders)
			{
				if(!reparsedHeader)
				{
					objGenerateHeader(object, store->containerSchema);
					reparsedHeader = true;
				}
				objIndexInsert(oi, object->header);
			}
			else
				objIndexInsert(oi, object->containerData);
			objIndexReleaseWriteLock(oi);
			PERFINFO_AUTO_STOP();
		}
	}

	eaiDestroy(&indicesAffected);

	PERFINFO_AUTO_STOP();
	return success;
}

int objModifyContainer(Container *object, char *diffString)
{
	ContainerStore *store = ((!object->isTemporary)?objFindContainerStoreFromType(object->containerType):NULL);
	int success = 1;
	static ObjectPathOperation **pathOperations = 0;
	static ObjectPath **cachedpaths = 0;
	if (!object || !diffString)
	{
		return 0;
	}
	PERFINFO_AUTO_START("objModifyContainer",1);

	if (!objPathParseOperations(object->containerSchema->classParse,diffString,&pathOperations))
	{
		eaClearEx(&pathOperations, DestroyObjectPathOperation);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	objContainerPrepareForModify(object);	
	objContainerCommitNotify(object, pathOperations, true);

	if (!objUpdateApplyOperations(object, pathOperations))
		success = 0;

	if (success)
	{
		objContainerCommitNotify(object, pathOperations, false);
		objContainerMarkModified(object);
	}

	eaClearEx(&pathOperations, DestroyObjectPathOperation);

	PERFINFO_AUTO_STOP();
	return success;
}


bool objExportContainer(GlobalType containerType, U32 containerID, const char *outputDir, char *outputName, char **resultString)
{
	Container *container = objGetContainerEx(containerType, containerID, true, false, true);
	if (container)
	{
		char fileName[MAX_PATH];
		if (outputName)
			sprintf(fileName,"%s/%s.%s.con",outputDir,outputName,GlobalTypeToName(containerType));
		else
			sprintf(fileName,"%s/%d.%s.con",outputDir,containerID,GlobalTypeToName(containerType));

		if (!ParserWriteTextFile(fileName,container->containerSchema->classParse,container->containerData,TOK_PERSIST,0))
		{		
			if (resultString) estrPrintf(resultString, "Failed to write container %s[%d]: Can't Write Data", GlobalTypeToName(container->containerType), container->containerID);
			objUnlockContainer(&container);
			return false;
		}

		if (resultString) estrPrintf(resultString, "%s", fileName);
		objUnlockContainer(&container);
		return true;
	}
	else
	{
		if (resultString) estrPrintf(resultString, "Failed to write container %s[%d]: Container does not exist", GlobalTypeToName(containerType), containerID);	
		return false;
	}
}

void objContainerCallCommitCallbacks(Container *object, ObjectPathOperation *pathHolder, bool bPreCommit, CommitCallbackStruct **commitCallbacks, void *** alreadyCalled, QueuedContainerCommitStruct *** queuedCommits)
{
	int i, j;
	for (i = 0; i < eaSize(&commitCallbacks); i++)
	{
		CommitCallbackStruct *commit = commitCallbacks[i];
		if (commit->bPreCommit != bPreCommit)
		{
			continue;
		}
		if (eaFind(alreadyCalled,commit) >= 0)
		{
			continue;
		}
		if (matchExact(commit->matchString,pathHolder->pathEString))
		{
			if(!commit->filterCallback || commit->filterCallback(object,pathHolder))
			{
				if (commit->bRunOnceWithAllPathOps)
				{
					QueuedContainerCommitStruct* queuedCommit = NULL;
					for (j = eaSize(queuedCommits)-1; j >= 0; j--)
					{
						queuedCommit = (*queuedCommits)[j];
						if (queuedCommit->commitCallback == commit->commitCallback)
						{
							break;
						}
					}
					if (j < 0)
					{
						queuedCommit = calloc(1, sizeof(QueuedContainerCommitStruct));
						queuedCommit->commitCallback = commit->commitCallback;
						eaPush(queuedCommits,queuedCommit);
					}
					eaPush(&queuedCommit->compiledPaths,pathHolder);
				}
				else
				{
					static ObjectPathOperation **pathHolderArray = NULL;
					if (!pathHolderArray)
						eaSetSize(&pathHolderArray, 1);
					eaSet(&pathHolderArray, pathHolder, 0);
					commit->commitCallback(object,pathHolderArray);
					if (commit->bRunOnce)
					{				
						eaPush(alreadyCalled,commit);
					}
				}
			}
		}
	}
}

int objContainerCommitNotify(Container *object, ObjectPathOperation **paths, bool bPreCommit)
{
	static void ** alreadyCalled = 0;
	static QueuedContainerCommitStruct ** queuedCommits = 0;
	int i;
	if (!object || !paths || object->isTemporary)
	{
		return 0;
	}
	PERFINFO_AUTO_START("objContainerCommitNotify",1);
	eaClear(&alreadyCalled);

	for (i = eaSize(&queuedCommits)-1; i >= 0; i--)
	{
		eaDestroy(&queuedCommits[i]->compiledPaths);
		SAFE_FREE(queuedCommits[i]);
	}
	eaClear(&queuedCommits);

	for (i = 0; i < eaSize(&paths); i++)
	{
		ObjectPathOperation *pathHolder = paths[i];

		objContainerCallCommitCallbacks(object,pathHolder,bPreCommit,
			gContainerRepository.commitCallbacks,&alreadyCalled,&queuedCommits);
		objContainerCallCommitCallbacks(object,pathHolder,bPreCommit,
			object->containerSchema->commitCallbacks,&alreadyCalled,&queuedCommits);
	}

	for (i = 0; i < eaSize(&queuedCommits); i++)
	{
		QueuedContainerCommitStruct *queuedCommit = (QueuedContainerCommitStruct*)queuedCommits[i];
		queuedCommit->commitCallback(object, queuedCommit->compiledPaths);
	}

	PERFINFO_AUTO_STOP();
	return 1;
}

static int sUnpackedContainers[GLOBALTYPE_MAXTYPES];
static int sDeletedUnpackedContainers[GLOBALTYPE_MAXTYPES];
static U64 sUnpackTime[GLOBALTYPE_MAXTYPES];

int objCountTotalUnpacksOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sUnpackedContainers[eType] + sDeletedUnpackedContainers[eType];
}

U64 objGetAverageUnpackTicksOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sUnpackTime[eType] / MAX(1, objCountTotalUnpacksOfType(eType));
}

AUTO_COMMAND;
void objPrintUnpackStats()
{
	printf("Unpacked %d containers for an average of %"FORM_LL"d ticks\n", objCountTotalUnpacksOfType(GLOBALTYPE_NONE), objGetAverageUnpackTicksOfType(GLOBALTYPE_NONE));
}

int objCountUnpackedContainersOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sUnpackedContainers[eType];
}

int objCountDeletedUnpackedContainersOfType(GlobalType eType)
{
	if (eType >= GLOBALTYPE_MAXTYPES) return 0;
	return sDeletedUnpackedContainers[eType];
}

bool objDoesContainerExist(GlobalType containerType, ContainerID containerID)
{
	ContainerStore *store;
	int index;

	store = objFindContainerStoreFromType(containerType);
	
	if (!store)
	{
		return false;
	}
	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store, containerID);
	objUnlockContainerStore_ReadOnly(store);
	return index >= 0;
}

bool objDoesDeletedContainerExist(GlobalType containerType, ContainerID containerID)
{
	ContainerStore *store;
	int index;

	store = objFindContainerStoreFromType(containerType);

	if (!store)
	{
		return false;
	}
	objLockContainerStoreDeleted_ReadOnly(store);
	index = objContainerStoreFindDeletedIndex(store, containerID);
	objUnlockContainerStoreDeleted_ReadOnly(store);
	return index >= 0;
}

bool objIsContainerOwnedByMe(GlobalType containerType, ContainerID containerID)
{
	Container *con;
	bool retVal;

	con = objGetContainerEx(containerType, containerID, false, false, true);
	retVal = con && con->meta.containerState == CONTAINERSTATE_OWNED;
	if(con)
		objUnlockContainer(&con);
	return retVal;
}

bool objIsDeletedContainerOwnedByMe(GlobalType containerType, ContainerID containerID)
{
	Container *con;
	bool retVal;

	con = objGetDeletedContainerEx(containerType, containerID, false, false, true);
	retVal = con && con->meta.containerState == CONTAINERSTATE_OWNED;
	if(con)
		objUnlockContainer(&con);
	return retVal;
}

Container *objGetContainer_dbg(GlobalType containerType, ContainerID containerID, int forceDecompress, int getFileData, int getLock MEM_DBG_PARMS)
{
	ContainerStore *store;
	int index;
	ObjectLock *lock = NULL;

	if (!containerID)
	{
		return NULL;
	}

	store = objFindContainerStoreFromType(containerType);
	
	if (!store)
	{
		return NULL;
	}

	if (gbEnableObjectLocks && getLock)
	{
		lock = objGetObjectLock(containerType, containerID);
		fullyLockObjectLock(lock);
	}

	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store, containerID);
	if (index >= 0)
	{
		Container *con = store->containers[index];
		objUnlockContainerStore_ReadOnly(store);

		if (gbEnableObjectLocks && getLock)
		{
			con->lock = lock;
		}

		if((forceDecompress && !con->containerData) || (getFileData && !con->fileData))
		{
			U64 start, end;
			U64 delta;
			GET_CPU_TICKS_64(start);
			objUnpackContainer(store->containerSchema, con, ForceKeepLazyLoadFileDataWithStore(store), !forceDecompress && getFileData, false);
			GET_CPU_TICKS_64(end);
			delta = end - start;
			sUnpackTime[containerType] += delta;
			sUnpackTime[GLOBALTYPE_NONE] += delta;
			++sUnpackedContainers[containerType];
			++sUnpackedContainers[GLOBALTYPE_NONE];
			verbose_printf("Container unpack of %s:%d took %"FORM_LL"d cycles (%s:%d)\n",
				GlobalTypeToName(containerType), containerID, delta, caller_fname, line);
		}
		return con;
	}
	objUnlockContainerStore_ReadOnly(store);

	if (gbEnableObjectLocks && getLock)
	{
		fullyUnlockObjectLock(lock);
	}
	return NULL;
}

Container *objGetDeletedContainer_dbg(GlobalType containerType, ContainerID containerID, int forceDecompress, int getFileData, int getLock MEM_DBG_PARMS)
{
	ContainerStore *store;
	int index;
	ObjectLock *lock = NULL;

	if (!containerID)
	{
		return NULL;
	}

	store = objFindContainerStoreFromType(containerType);
	
	if (!store)
	{
		return NULL;
	}

	if (gbEnableObjectLocks && getLock)
	{
		lock = objGetObjectLock(containerType, containerID);
		fullyLockObjectLock(lock);
	}

	objLockContainerStoreDeleted_ReadOnly(store);
	index = objContainerStoreFindDeletedIndex(store, containerID);
	if (index >= 0)
	{
		Container *con = store->deletedContainerCache[index];
		objUnlockContainerStoreDeleted_ReadOnly(store);

		if (gbEnableObjectLocks && getLock)
		{
			con->lock = lock;
		}

		if((forceDecompress && !con->containerData) || (getFileData && !con->fileData))
		{
			U64 start, end;
			U64 delta;
			GET_CPU_TICKS_64(start);
			objUnpackContainer(store->containerSchema, con, ForceKeepLazyLoadFileDataWithStore(store), !forceDecompress && getFileData, false);
			GET_CPU_TICKS_64(end);
			delta = end - start;
			sUnpackTime[containerType] += delta;
			sUnpackTime[GLOBALTYPE_NONE] += delta;
			++sDeletedUnpackedContainers[containerType];
			++sDeletedUnpackedContainers[GLOBALTYPE_NONE];
			verbose_printf("Container unpack of %s:%d took %"FORM_LL"d cycles (%s:%d)\n",
				GlobalTypeToName(containerType), containerID, delta, caller_fname, line);
		}
		return con;
	}

	if (gbEnableObjectLocks && getLock)
	{
		fullyUnlockObjectLock(lock);
	}

	objUnlockContainerStoreDeleted_ReadOnly(store);
	return NULL;
}

ObjectLock *objLockContainerByTypeAndID(GlobalType containerType, ContainerID containerID)
{
	ObjectLock *lock = NULL;

	if (!gbEnableObjectLocks) return NULL;

	lock = objGetObjectLock(containerType, containerID);
	fullyLockObjectLock(lock);
	return lock;
}

void objUnlockContainer(Container **container)
{
	ObjectLock *lock = (*container)->lock;

	if(!gbEnableObjectLocks)
	{
		*container = NULL;
		return;
	}

	// If this isn't populated by now, it means you are trying to unlock a container you locked
	// with some mechanism other than objGetContainerEx.
	assert(lock);
	(*container)->lock = NULL;
	fullyUnlockObjectLock(lock);

	*container = NULL;
}

void objUnlockContainerLock(ObjectLock **lock)
{
	if (!gbEnableObjectLocks)
	{
		*lock = NULL;
		return;
	}

	fullyUnlockObjectLock(*lock);
	*lock = NULL;
}

ContainerState objGetContainerState(GlobalType containerType, ContainerID containerID)
{
	Container *pContainer = objGetContainerEx(containerType, containerID, false, false, true);
	ContainerState state;
	if (!pContainer)
	{
		return CONTAINERSTATE_UNKNOWN;
	}
	state = pContainer->meta.containerState;
	objUnlockContainer(&pContainer);
	return state;
}

AUTO_RUN_FIRST;
void InitializeGlobalContainerRepository(void)
{
	InitializeCriticalSection_wrapper(&gContainerRepository.repositoryCriticalSection);

	InitTrackedContainersInRepository();
}

static bool useContainerStoreLocking = false;

void enableContainerStoreLocking(bool enable)
{
	useContainerStoreLocking = enable;
}

void objLockGlobalContainerRepository(void)
{
	if(useContainerStoreLocking)
		EnterCriticalSection_wrapper(gContainerRepository.repositoryCriticalSection);
}

void objUnlockGlobalContainerRepository(void)
{
	if(useContainerStoreLocking)
		LeaveCriticalSection_wrapper(gContainerRepository.repositoryCriticalSection);
}

void objLockContainerStore_dbg(ContainerStore *store, bool readOnly MEM_DBG_PARMS)
{
	assert(store);
	InterlockedIncrement(&gCurrentlyLockedContainerStores);
	if(useContainerStoreLocking)
	{
		if(readOnly)
			rwlReadLock(store->containerReadWriteLock);
		else
			rwlWriteLock(store->containerReadWriteLock, false);
		store->lockInfo.lastFilename = caller_fname;
		store->lockInfo.lastLineNumber = line;
		store->lockInfo.readOnly = readOnly;
	}
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	{
		if(readOnly)
		{
			U32 readers;
			readers = InterlockedIncrement(&store->readersInContainerCriticalSection);
			assert(readers >= 1);
			assert(store->writersInContainerCriticalSection == 0);
		}
		else
		{
			U32 writers;
			writers = InterlockedIncrement(&store->writersInContainerCriticalSection);
			assert(writers == 1);
			assert(store->readersInContainerCriticalSection == 0);
		}
	}
#endif
}

void objUnlockContainerStore(ContainerStore *store, bool readOnly)
{
#if DEBUG_NEW_CONTAINERSTORE_LOCKING 
	if(readOnly)
	{
		assert(store->readersInContainerCriticalSection >= 1);
		assert(store->writersInContainerCriticalSection == 0);
	}
	else
	{
		assert(store->readersInContainerCriticalSection == 0);
		assert(store->writersInContainerCriticalSection == 1);
	}
#endif
	if(useContainerStoreLocking)
	{
		if(readOnly)
			rwlReadUnlock(store->containerReadWriteLock);
		else
			rwlWriteUnlock(store->containerReadWriteLock);
	}
	InterlockedDecrement(&gCurrentlyLockedContainerStores);
#if DEBUG_NEW_CONTAINERSTORE_LOCKING 
	{
		U32 result;
		if(readOnly)
			result = InterlockedDecrement(&store->readersInContainerCriticalSection);
		else
			result = InterlockedDecrement(&store->writersInContainerCriticalSection);
	}
#endif
}

void objLockContainerStoreDeleted_dbg(ContainerStore *store, bool readOnly MEM_DBG_PARMS)
{
	if(useContainerStoreLocking)
	{
		if(readOnly)
			rwlReadLock(store->deletedContainerReadWriteLock);
		else
			rwlWriteLock(store->deletedContainerReadWriteLock, false);
		store->deletedLockInfo.lastFilename = caller_fname;
		store->deletedLockInfo.lastLineNumber = line;
		store->deletedLockInfo.readOnly = readOnly;
	}
#if DEBUG_NEW_CONTAINERSTORE_LOCKING 
	{
		if(readOnly)
		{
			U32 readers;
			readers = InterlockedIncrement(&store->readersInDeletedContainerCriticalSection);
			assert(readers >= 1);
			assert(store->writersInDeletedContainerCriticalSection == 0);
		}
		else
		{
			U32 writers;
			writers = InterlockedIncrement(&store->writersInDeletedContainerCriticalSection);
			assert(writers == 1);
			assert(store->readersInDeletedContainerCriticalSection == 0);
		}
	}
#endif
}

void objUnlockContainerStoreDeleted(ContainerStore *store, bool readOnly)
{
#if DEBUG_NEW_CONTAINERSTORE_LOCKING 
	if(readOnly)
	{
		assert(store->readersInDeletedContainerCriticalSection >= 1);
		assert(store->writersInDeletedContainerCriticalSection == 0);
	}
	else
	{
		assert(store->readersInDeletedContainerCriticalSection == 0);
		assert(store->writersInDeletedContainerCriticalSection == 1);
	}
#endif
	if(useContainerStoreLocking)
	{
		if(readOnly)
			rwlReadUnlock(store->deletedContainerReadWriteLock);
		else
			rwlWriteUnlock(store->deletedContainerReadWriteLock);
	}
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	{
		U32 result;
		if(readOnly)
			result = InterlockedDecrement(&store->readersInDeletedContainerCriticalSection);
		else
			result = InterlockedDecrement(&store->writersInDeletedContainerCriticalSection);
	}
#endif
}

CRITICAL_SECTION gCreateContainerStoreCS;

// Returns NULL if the schema's type is somehow out of bounds
// Otherwise returns the pointer to the store, whether created by this function, or already existing.
ContainerStore *objCreateContainerStoreEx(ContainerSchema *schema, StashTable headerFieldTable, U32 lut_size, bool lazyLoad, ObjectIndexHeaderType headerType, bool ignoreBaseContainerID)
{
	char* classname = 0;
	ContainerStore *store;
	GlobalType type = schema->containerType;

	ATOMIC_INIT_BEGIN;
	InitializeCriticalSection(&gCreateContainerStoreCS);
	ATOMIC_INIT_END;

	if (type <= 0 || type >= GLOBALTYPE_MAXTYPES)
	{
		return NULL;
	}

	store = objFindContainerStoreFromType(type);
	if (store)
	{
		return store; //Return the store even if we did not just create it.
	}
//	InitializeCriticalSection(&store->containerCriticalSection);
//	InitializeCriticalSection(&store->deletedContainerCriticalSection);
	EnterCriticalSection(&gCreateContainerStoreCS);
	// Check again, in case someone else created the store between the check and getting the lock. This allows us to skip the Critical section if it already exists.
	store = objFindContainerStoreFromType(type);
	if(store)
	{
		LeaveCriticalSection(&gCreateContainerStoreCS);
		return store; //Return the store even if we did not just create it.
	}
	store = &gContainerRepository.containerStores[type];
	store->containerReadWriteLock = CreateReadWriteLock();
	store->deletedContainerReadWriteLock = CreateReadWriteLock();
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	store->readersInContainerCriticalSection = 0;
	store->writersInContainerCriticalSection = 0;
	store->readersInDeletedContainerCriticalSection = 0;
	store->writersInDeletedContainerCriticalSection = 0;
#endif
	store->containerSchema = schema;
	objLockContainerStore_Write(store);
	eaCreate(&store->containers);
	store->lookupTable = stashTableCreateInt(lut_size);
	objUnlockContainerStore_Write(store);
	objLockContainerStoreDeleted_Write(store);
	store->deletedLookupTable = stashTableCreateInt(lut_size);
	store->deletedCleanUpLookupTable = stashTableCreateInt(lut_size);
	objUnlockContainerStoreDeleted_Write(store);
	store->requiresHeaders = !!headerFieldTable;
	store->headerFieldTable = headerFieldTable;
	store->lazyLoad = !!lazyLoad;
	store->objectIndexHeaderType = headerType;
	store->ignoreBaseContainerID = ignoreBaseContainerID;
	store->objectLocks = stashTableCreateInt(lut_size);
	LeaveCriticalSection(&gCreateContainerStoreCS);

	return store;
}

typedef struct RepositoryStatsInfo
{
	int numContainers;
	int numCompressed;
	int numNotLoaded;
} RepositoryStatsInfo;

static void CountCompressedAndUnloaded(Container *con, RepositoryStatsInfo *info)
{
	if (!con->containerData)
	{
		if (!con->fileData)
			info->numNotLoaded++;
		else
			info->numCompressed++;
	}
}

AUTO_COMMAND;
void objPrintRepositoryStats()
{
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (store->containerSchema)
		{
			RepositoryStatsInfo info = {0};

			objLockContainerStore_ReadOnly(store);
			info.numContainers = eaSize(&store->containers);
			objUnlockContainerStore_ReadOnly(store);

			ForEachContainerOfTypeEx(i, CountCompressedAndUnloaded, NULL, &info, false, false);

			printf("%s: %d containers, %d compressed (%2.0f%%), %d not loaded (%2.0f%%)\n", GlobalTypeToName(i),
				info.numContainers, info.numCompressed, 100 * (float)info.numCompressed / (info.numContainers+.0001),
				info.numNotLoaded, 100 * (float)info.numNotLoaded / (info.numContainers+.0001));

			objLockContainerStoreDeleted_ReadOnly(store);
			info.numContainers = eaSize(&store->deletedContainerCache);
			objUnlockContainerStoreDeleted_ReadOnly(store);
			if(info.numContainers)
			{
				info.numCompressed = 0;
				info.numNotLoaded = 0;
				ForEachContainerOfTypeEx(i, CountCompressedAndUnloaded, NULL, &info, false, true);

				printf("%s(deleted): %d containers, %d compressed (%2.0f%%), %d not loaded (%2.0f%%)\n", GlobalTypeToName(i),
					info.numContainers, info.numCompressed, 100 * (float)info.numCompressed / (info.numContainers+.0001),
					info.numNotLoaded, 100 * (float)info.numNotLoaded / (info.numContainers+.0001));
			}
		}
	}
}

void objContainerStoreSetSaveAllHeaders(ContainerStore *store, bool on)
{
	store->saveAllHeaders = !!on;
}

void objContainerStoreDisableIndexing(ContainerStore *store)
{
	store->disableIndexing = true;
}

ContainerStore *objFindContainerStoreFromType(GlobalType id)
{
	ContainerStore *store;
	if (id <= 0 || id >= GLOBALTYPE_MAXTYPES)
	{
		return NULL;
	}
	store = &gContainerRepository.containerStores[id];
	if (store->containerSchema)
	{
		return store;
	}
	return NULL;
}

ContainerStore *objFindOrCreateContainerStoreFromType(GlobalType id)
{
	ContainerStore *store = objFindContainerStoreFromType(id);
	if(!store)
	{
		ContainerSchema *schema = objFindContainerSchema(id);
		if(schema)
		{
			store = objCreateContainerStore(schema);
		}
	}

	return store;
}

int objSetNewContainerID(Container *object, ContainerID id, ContainerID curId)
{
	int keyIndex;
	ParseTable *tpi = object->containerSchema->classParse;

	PERFINFO_AUTO_START_FUNC();

	if ((keyIndex = ParserGetTableKeyColumn(tpi)) < 0)
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}
	
	if (object->containerData)
	{
		TokenStoreSetInt_inline(tpi, &tpi[keyIndex], keyIndex, object->containerData, 0, id, NULL, NULL);
	}

	object->containerID = id;
	PERFINFO_AUTO_STOP();
	return 1;
}

int objChangeContainerState(Container *object, ContainerState state, GlobalType ownerType, ContainerID ownerID)
{
	if (!object)
	{
		return 0;
	}
	if (!object->isTemporary)
	{
		ContainerStore *store = objFindContainerStoreFromType(object->containerType);
		if (!store)
		{
			return 0;
		}

		object->lastAccessed = timeSecondsSince2000();

		if (state == CONTAINERSTATE_OWNED && object->meta.containerState != CONTAINERSTATE_OWNED)
		{
			store->ownedContainers++;
		}
		if (state != CONTAINERSTATE_OWNED && object->meta.containerState == CONTAINERSTATE_OWNED)
		{
			store->ownedContainers--;
		}
		if (stateChangeCB)
		{
			stateChangeCB(object, state, ownerType, ownerID);
		}
	}
	object->meta.containerState = state;
	object->meta.containerOwnerType = ownerType;
	object->meta.containerOwnerID = ownerID;
	return 1;

}

// base must be locked when this is called
int objContainerStoreFindIndex(ContainerStore *base, ContainerID objectID)
{
	ContainerID foundID;
	if (!base || !base->containers)
		return -1;

#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	assert(base->readersInContainerCriticalSection >= 1 || base->writersInContainerCriticalSection >= 1);
#endif

	if (stashIntFindInt(base->lookupTable,objectID,&foundID))
	{
		return foundID;
	}
	return -1;
}

int objContainerStoreFindDeletedIndex(ContainerStore *base, ContainerID objectID)
{
	ContainerID foundID;
	if (!base || !base->deletedContainerCache)
		return -1;

#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	assert(base->readersInDeletedContainerCriticalSection >= 1 || base->writersInDeletedContainerCriticalSection >= 1);
#endif

	if (stashIntFindInt(base->deletedLookupTable,objectID,&foundID))
	{
		return foundID;
	}
	return -1;
}

void objUpdateContainerStoreMaxID(ContainerStore *store, ContainerID newMaxID)
{
	ContainerID maxID = store->maxID;
	bool updated = false;
	while(newMaxID > maxID)
	{
		ContainerID initialValue = InterlockedCompareExchange(&store->maxID, newMaxID, maxID);
		if(initialValue == maxID)
			updated = true;
		maxID = store->maxID;
	}
}

U32 gContainerIDAlertThrottleTimeSeconds = 86400;
AUTO_CMD_INT(gContainerIDAlertThrottleTimeSeconds, ContainerIDAlertThrottleTimeSeconds) ACMD_CMDLINE;

ACMD_CMDLINE;
void ContainerIDAlertThrottleHours(U32 hours)
{
	gContainerIDAlertThrottleTimeSeconds = hours * SECONDS_PER_HOUR;
}

SimpleEventThrottler *gContainerIDAlertThrottlers[GLOBALTYPE_MAXTYPES] = {0};

void TriggerContainerIDAlert(GlobalType containerType)
{
	SimpleEventThrottlerResult result;
	assert(IsValidGlobalType(containerType));

	if(!gContainerIDAlertThrottlers[containerType])
		gContainerIDAlertThrottlers[containerType] = SimpleEventThrottler_Create(1, gContainerIDAlertThrottleTimeSeconds, gContainerIDAlertThrottleTimeSeconds);

	result = SimpleEventThrottler_ItHappened(gContainerIDAlertThrottlers[containerType], timeSecondsSince2000());
	if(result == SETR_OK)
	{
		ErrorOrCriticalAlert("OBJECTDB.CONTAINERIDLIMIT", "Container type %s is within 25%% of the maximum id available. If we run out of id space, the shard will go down.", GlobalTypeToName(containerType));
	}
}

ContainerID objReserveNewContainerID(ContainerStore *base)
{
	ContainerID containerID;
	if (!base)
	{
		return 0;
	}
	if(!base->ignoreBaseContainerID && gBaseContainerID && base->maxID < gBaseContainerID)
		objUpdateContainerStoreMaxID(base, gBaseContainerID);
	containerID = InterlockedIncrement(&base->maxID);
	
	if(!base->ignoreBaseContainerID && gContainerIDAlertThreshold && containerID >= gContainerIDAlertThreshold)
	{
		TriggerContainerIDAlert(base->containerSchema->containerType);

		if(!base->ignoreBaseContainerID && gMaxContainerID && containerID >= gMaxContainerID)
		{
			// We have exceeded our maximum on a type. There is no way to fail gracefully.
			assertmsgf(0, "We have run out of ids for %s. This is a seriously fatal problem.", GlobalTypeToName(base->containerSchema->containerType));
		}
	}

	return containerID;
}

bool ContainerIDInBaseRange(ContainerStore *store, ContainerID containerID)
{
	if(store->ignoreBaseContainerID || !gMaxContainerID)
		return true;

	return containerID <= gMaxContainerID && containerID >= gBaseContainerID;
}

bool objAddToContainerStore(ContainerStore *base, Container *newcontainer, ContainerID newID, bool modified, U32 deletedTime)
{
	int newIndex;
	U32 key;
	ContainerSchema *schema = base->containerSchema;
	if (!base)
		return 0;

	PERFINFO_AUTO_START_FUNC();

	if (newcontainer->header)
		key = newcontainer->header->containerId;
	else if (newcontainer->containerData)
		objGetKeyInt(schema->classParse,newcontainer->containerData,&key);
	else
		key = newID;

	if (key == 0 && newID != 0)
	{
		// The container was marked for deletion.
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if(deletedTime)
	{
		objLockContainerStoreDeleted_ReadOnly(base);
		newIndex = objContainerStoreFindDeletedIndex(base, key);
	}
	else
	{
		objLockContainerStore_ReadOnly(base);
		newIndex = objContainerStoreFindIndex(base,key);
	}

	if(deletedTime)
		objUnlockContainerStoreDeleted_ReadOnly(base);
	else
		objUnlockContainerStore_ReadOnly(base);

	if (newIndex >= 0)
	{
		// Something with this key is already here, don't add it
		PERFINFO_AUTO_STOP();
		return 0;
	}
	if (newID == 0)
	{
		newID = objReserveNewContainerID(base); // Also updates the max id
	}
	else if (newID > 0)
	{
		if(ContainerIDInBaseRange(base, newID))
			objUpdateContainerStoreMaxID(base, newID);
	}
	else
	{
		PERFINFO_AUTO_STOP();
		return 0;
	}

	objSetNewContainerID(newcontainer,newID,key);

	if(deletedTime)
	{
		DeletedContainerQueueEntry *containerQueueEntry = callocStruct(DeletedContainerQueueEntry);
		objLockContainerStoreDeleted_Write(base);
		newIndex = eaPush(&base->deletedContainerCache,newcontainer);
		stashIntAddInt(base->deletedLookupTable,newID,newIndex,true);
		stashIntAddPointer(base->deletedCleanUpLookupTable, newID, containerQueueEntry, false);
		containerQueueEntry->containerID = newID;
		containerQueueEntry->iDeletedTime = deletedTime;
		containerQueueEntry->destroyTimeFunc = base->deletedContainerQueueDestroyTimeFunc;
		if(base->deletedContainerQueueDestroyTimeDataFunc)
			containerQueueEntry->userData = base->deletedContainerQueueDestroyTimeDataFunc(newcontainer);
		dcqPush(base, containerQueueEntry);
		base->deletedContainers++;
		objUnlockContainerStoreDeleted_Write(base);
	}
	else
	{
		objLockContainerStore_Write(base);
		newIndex = eaPush(&base->containers,newcontainer);
		stashIntAddInt(base->lookupTable,newID,newIndex,true);
		base->totalContainers++;
		objUnlockContainerStore_Write(base);
	}

	objInitContainerObject(newcontainer->containerSchema,newcontainer->containerData);

	if (base->requiresHeaders && !newcontainer->header)
	{
		objGenerateHeader(newcontainer, base->containerSchema);
	}

	if (modified)
		objContainerMarkModified(newcontainer);

	if (base->addCallback)
		base->addCallback(newcontainer, newcontainer->containerData);

	if(!base->disableIndexing)
	{
		if(deletedTime)
		{
			EARRAY_FOREACH_BEGIN(base->deletedIndices, i);
			{
				objIndexObtainWriteLock(base->deletedIndices[i]);
				if(base->requiresHeaders)
					objIndexInsert(base->deletedIndices[i], newcontainer->header);
				else
					objIndexInsert(base->deletedIndices[i], newcontainer->containerData);
				objIndexReleaseWriteLock(base->deletedIndices[i]);
			}
			EARRAY_FOREACH_END;
		}
		else
		{
			EARRAY_FOREACH_BEGIN(base->indices, i);
			{
				objIndexObtainWriteLock(base->indices[i]);
				if(base->requiresHeaders)
					objIndexInsert(base->indices[i], newcontainer->header);
				else
					objIndexInsert(base->indices[i], newcontainer->containerData);
				objIndexReleaseWriteLock(base->indices[i]);
			}
			EARRAY_FOREACH_END;
		}
	}

	PERFINFO_AUTO_STOP();
	return 1;
}

// Needs lock
void objFixLookupTable(ContainerStore *base, int index)
{
#if DEBUG_NEW_CONTAINERSTORE_LOCKING
	assert(base->writersInContainerCriticalSection == 1);
#endif
	if (index < 0 || index >= eaSize(&base->containers))
	{
		return;
	}
	stashIntAddInt(base->lookupTable,objGetContainerID(base->containers[index]),index,true);
}

void objResetDeletedCleanupTableEntry(ContainerStore *base, ContainerID containerID)
{
	DeletedContainerQueueEntry *containerQueueEntry;

	if (!base)
		return;

	if(stashIntFindPointer(base->deletedCleanUpLookupTable, containerID, &containerQueueEntry))
	{
		containerQueueEntry->queuedForDelete = false;
	}
}

// Locks the DCC
void objFixDeletedCleanupTables(ContainerStore *base, ContainerID containerID, bool alreadyHasLock, bool removeFromStashTable)
{
	DeletedContainerQueueEntry *containerQueueEntry;

	if (!base)
		return;

	if(!alreadyHasLock)
		objLockContainerStoreDeleted_Write(base);
	if(stashIntFindPointer(base->deletedCleanUpLookupTable, containerID, &containerQueueEntry))
	{
		dcqRemove(base, containerQueueEntry);
		if(removeFromStashTable)
		{
			stashIntRemovePointer(base->deletedCleanUpLookupTable, containerID, &containerQueueEntry);
			SAFE_FREE(containerQueueEntry);
		}
	}
	if(!alreadyHasLock)
		objUnlockContainerStoreDeleted_Write(base);
}

// Must already have the lock since we have an index being passed in.
void objFixDeletedLookupTable(ContainerStore *base, int index)
{
	if (index < 0 || index >= eaSize(&base->deletedContainerCache))
	{
		return;
	}
	stashIntAddInt(base->deletedLookupTable,objGetContainerID(base->deletedContainerCache[index]),index,true);
}

void objDeleteContainer(GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, bool destroyNow)
{
	ContainerStore *base = objFindContainerStoreFromType(containerType);
	if(base)
	{
		Container* container;
		int index = 0;
		objLockContainerStore_Write(base);
		if(stashIntFindInt(base->lookupTable, containerID, &index))
		{
			int newIndex;
			DeletedContainerQueueEntry *containerQueueEntry = callocStruct(DeletedContainerQueueEntry);
			container = base->containers[index];
			eaRemoveFast(&base->containers, index);
			stashIntRemoveInt(base->lookupTable, containerID, &index);
			objFixLookupTable(base, index);
			if(container->meta.containerState == CONTAINERSTATE_OWNED)
				--base->ownedContainers;
			--base->totalContainers;
			objUnlockContainerStore_Write(base);
			EARRAY_FOREACH_BEGIN(base->indices, i);
			{
				objIndexObtainWriteLock(base->indices[i]);
				if(base->requiresHeaders)
					objIndexRemove(base->indices[i], container->header);
				else
					objIndexRemove(base->indices[i], container->containerData);
				objIndexReleaseWriteLock(base->indices[i]);
			}
			EARRAY_FOREACH_END;

			objLockContainerStoreDeleted_Write(base);
			newIndex = eaPush(&base->deletedContainerCache, container);
			stashIntAddInt(base->deletedLookupTable, containerID, newIndex, true);
			containerQueueEntry->containerID = containerID;
			containerQueueEntry->destroyTimeFunc = base->deletedContainerQueueDestroyTimeFunc;
			if(base->deletedContainerQueueDestroyTimeDataFunc)
				containerQueueEntry->userData = base->deletedContainerQueueDestroyTimeDataFunc(container);
			if(destroyNow)
			{
				containerQueueEntry->iDeletedTime = 1; // Force the destroy to happen immediately
			}
			else
			{
				containerQueueEntry->iDeletedTime = timeSecondsSince2000();
			}
			dcqPush(base, containerQueueEntry);

			stashIntAddPointer(base->deletedCleanUpLookupTable, containerID, containerQueueEntry, false);
			++base->deletedContainers;
			objUnlockContainerStoreDeleted_Write(base);
			EARRAY_FOREACH_BEGIN(base->deletedIndices, i);
			{
				objIndexObtainWriteLock(base->deletedIndices[i]);
				if(base->requiresHeaders)
					objIndexInsert(base->deletedIndices[i], container->header);
				else
					objIndexInsert(base->deletedIndices[i], container->containerData);
				objIndexReleaseWriteLock(base->deletedIndices[i]);
			}
			EARRAY_FOREACH_END;

			if (base->deleteCallback)
				base->deleteCallback(container, container->containerData);
	
			objContainerMarkModified(container);
			if(gUnloadOnCachedDelete)
			{
				while (InterlockedIncrement(&container->updateLocked) > 1)
				{
					InterlockedDecrement(&container->updateLocked);
					Sleep(0);
				}

				objUnloadContainer(container);

				InterlockedDecrement(&container->updateLocked);
			}
		}
		else
		{
			objUnlockContainerStore_Write(base);
		}
	}
}

void objUndeleteContainer(GlobalType containerType, ContainerID containerID, GlobalType sourceType, ContainerID sourceID, char *namestr)
{
	ContainerStore *base = objFindContainerStoreFromType(containerType);
	if(base)
	{
		Container* container;
		int index = 0;
		objLockContainerStoreDeleted_Write(base);
		if(stashIntFindInt(base->deletedLookupTable, containerID, &index))
		{
			int newIndex;

			container = base->deletedContainerCache[index];

			eaRemoveFast(&base->deletedContainerCache, index);
			stashIntRemoveInt(base->deletedLookupTable, containerID, &index);
			objFixDeletedCleanupTables(base, containerID, true, true);
			objFixDeletedLookupTable(base, index);
			--base->deletedContainers;
			objUnlockContainerStoreDeleted_Write(base);
			EARRAY_FOREACH_BEGIN(base->deletedIndices, i);
			{
				objIndexObtainWriteLock(base->deletedIndices[i]);
				if(base->requiresHeaders)
					objIndexRemove(base->deletedIndices[i], container->header);
				else
					objIndexRemove(base->deletedIndices[i], container->containerData);
				objIndexReleaseWriteLock(base->deletedIndices[i]);
			}
			EARRAY_FOREACH_END;

			if(namestr && *namestr)
			{
				objPathSetString(".psaved.savedname", base->containerSchema->classParse, container->containerData, namestr);
				objGenerateHeader(container, base->containerSchema);
			}

			objLockContainerStore_Write(base);
			newIndex = eaPush(&base->containers, container);
			stashIntAddInt(base->lookupTable, containerID, newIndex, true);
			if(container->meta.containerState == CONTAINERSTATE_OWNED)
				++base->ownedContainers;
			++base->totalContainers;
			objUnlockContainerStore_Write(base);

			EARRAY_FOREACH_BEGIN(base->indices, i);
			{
				objIndexObtainWriteLock(base->indices[i]);
				if(base->requiresHeaders)
					objIndexInsert(base->indices[i], container->header);
				else
					objIndexInsert(base->indices[i], container->containerData);
				objIndexReleaseWriteLock(base->indices[i]);
			}
			EARRAY_FOREACH_END;

			if (base->undeleteCallback)
				base->undeleteCallback(container, container->containerData);

			objContainerMarkModified(container);
		}
		else
		{
			objUnlockContainerStoreDeleted_Write(base);
		}
	}
}

void AddContainerToTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon)
{
	int index = eaPush(&containerArray->trackedContainers, trackedCon);
	stashAddInt(containerArray->lookupContainerByRef, &trackedCon->conRef, index, true);
}

void RemoveContainerFromTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon)
{
	int index = 0;

	if (stashRemoveInt(containerArray->lookupContainerByRef, &trackedCon->conRef, &index))
	{
		eaRemoveFast(&containerArray->trackedContainers, index);

		if (index < eaSize(&containerArray->trackedContainers))
		{
			stashAddInt(containerArray->lookupContainerByRef, &containerArray->trackedContainers[index]->conRef, index, true);
		}
	}
}

TrackedContainer *FindContainerInTrackedContainerArray(TrackedContainerArray *containerArray, TrackedContainer *trackedCon)
{
	int index = 0;

	if (stashFindInt(containerArray->lookupContainerByRef, &trackedCon->conRef, &index))
	{
		return containerArray->trackedContainers[index];
	}

	return NULL;
}

void ClearTrackedContainerArray(TrackedContainerArray *containerArray)
{
	eaClearStruct(&containerArray->trackedContainers, parse_TrackedContainer);
	stashTableClear(containerArray->lookupContainerByRef);
}

//This function can take ownership of *temp. *temp will be set to NULL if so.
static void objRegisterRemovedContainer(Container **temp, bool bGuaranteeNoDuplicateRemoves)
{
	TrackedContainer trackedCon = {0};

	assert(temp);

	PERFINFO_AUTO_START_FUNC();

	trackedCon.conRef.containerType = (*temp)->containerType;
	trackedCon.conRef.containerID = (*temp)->containerID;

	if (GlobalTypeSchemaType((*temp)->containerType) == SCHEMATYPE_PERSISTED && objGetContainerSourcePath())
	{
		objLockGlobalContainerRepository();
		if (!bGuaranteeNoDuplicateRemoves)
		{
			if (FindContainerInTrackedContainerArray(&gContainerRepository.removedContainersLocking, &trackedCon))
			{
				// Already removed
				objUnlockGlobalContainerRepository();
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		if(objGetContainerType(*temp) == GLOBALTYPE_ENTITYPLAYER)
		{
			RemoveContainerFromTrackedContainerArray(&gContainerRepository.modifiedEntityPlayers, &trackedCon);
		}
		else if(objGetContainerType(*temp) == GLOBALTYPE_ENTITYSAVEDPET)
		{
			RemoveContainerFromTrackedContainerArray(&gContainerRepository.modifiedEntitySavedPets, &trackedCon);
		}
		else
		{
			RemoveContainerFromTrackedContainerArray(&gContainerRepository.modifiedContainers, &trackedCon);
		}

		log_printf(LOG_CONTAINER, "Removing Container %s[%d]", GlobalTypeToName((*temp)->containerType), (*temp)->containerID);

		{
			TrackedContainer *pRemoved = StructCreate(parse_TrackedContainer);
			pRemoved->conRef = trackedCon.conRef;
			pRemoved->container = *temp;
			if((*temp)->lock)
				objUnlockContainer(temp);
			*temp = NULL;

			AddContainerToTrackedContainerArray(&gContainerRepository.removedContainersLocking, pRemoved);
		}
		objUnlockGlobalContainerRepository();
	}

	PERFINFO_AUTO_STOP();
}

Container *objContainerCopy(Container *container, int temporary, char *pComment)
{
	Container *newcon = objCreateContainer(container->containerSchema);

	if (container->header)
	{
		newcon->header = calloc(1, sizeof(*newcon->header));
		memcpy(newcon->header, container->header, sizeof(*newcon->header));
		// OBJ_HEADER_PUB_ACCOUNTNAME
		newcon->header->pubAccountName = strdup(container->header->pubAccountName);
		// OBJ_HEADER_PRIV_ACCOUNTNAME
		newcon->header->privAccountName = strdup(container->header->privAccountName);
		// OBJ_HEADER_SAVEDNAME
		newcon->header->savedName = strdup(container->header->savedName);
		// OBJ_HEADER_EXTRA_DATA_1
		if(container->header->extraData1)
			newcon->header->extraData1 = strdup(container->header->extraData1);
		// OBJ_HEADER_EXTRA_DATA_2
		if(container->header->extraData2)
			newcon->header->extraData2 = strdup(container->header->extraData2);
		// OBJ_HEADER_EXTRA_DATA_3
		if(container->header->extraData3)
			newcon->header->extraData3 = strdup(container->header->extraData3);
		// OBJ_HEADER_EXTRA_DATA_4
		if(container->header->extraData4)
			newcon->header->extraData4 = strdup(container->header->extraData4);
		// OBJ_HEADER_EXTRA_DATA_5
		if(container->header->extraData5)
			newcon->header->extraData5 = strdup(container->header->extraData5);

		newcon->headerSize = container->headerSize;

		if(container->rawHeader && container->headerSize)
		{
			newcon->rawHeader = malloc(container->headerSize);
			memcpy(newcon->rawHeader, container->rawHeader, container->headerSize);		
		}
	}

	newcon->fileSize = container->fileSize;
	newcon->bytesCompressed = container->bytesCompressed;

	if(container->fileData)
	{
		U32 allocSize;

		if(container->bytesCompressed)
			allocSize = container->bytesCompressed;
		else
			allocSize = container->fileSize;

		newcon->fileData = malloc_FileData(allocSize);
		memcpy(newcon->fileData, container->fileData, allocSize);
		newcon->checksum = container->checksum;
	}

	if(container->containerData)
	{
		newcon->containerData = objCreateTempContainerObject(container->containerSchema, pComment);
		StructCopyFieldsVoid(container->containerSchema->classParse,container->containerData,newcon->containerData,0,0);
	}

	newcon->containerType = objGetContainerType(container);
	newcon->containerID = objGetContainerID(container);
	newcon->isTemporary = !!temporary;

	return newcon;
}

void objRegisterInvalidContainer(Container *temp)
{
	Container *removed;
	int i;
	objLockGlobalContainerRepository();
	for (i = eaSize(&gContainerRepository.invalidContainers) - 1; i>= 0; i--)
	{
		removed = gContainerRepository.invalidContainers[i];
		if (removed->containerType == temp->containerType &&
			removed->containerID == temp->containerID)
		{
			// Already there
			objUnlockGlobalContainerRepository();
			return;
		}
	}
	removed = objContainerCopy(temp, false, "Creating copy in objRegisterInvalidContainer");

	eaPush(&gContainerRepository.invalidContainers, removed);	
	objUnlockGlobalContainerRepository();
}

U32 objCountInvalidContainers(void)
{
	int size;
	objLockGlobalContainerRepository();
	size = eaSize(&gContainerRepository.invalidContainers);
	objUnlockGlobalContainerRepository();
	return size;
}

bool objRemoveContainerFromHog(Container **object)
{
	if(object)
	{
		objRegisterRemovedContainer(object, true);
		return true;
	}

	return false;
}

bool objRemoveFromContainerStore(ContainerStore *base, ContainerID objectID)
{
	return objRemoveFromContainerStoreAndHog(base, objectID, true);
}

bool objRemoveFromContainerStoreAndHog(ContainerStore *base, ContainerID objectID, bool removeFromHog)
{
	return objRemoveFromContainerStoreAndHogEx(base, objectID, removeFromHog, false);
}

bool objRemoveFromContainerStoreAndHogEx(ContainerStore *base, ContainerID objectID, bool removeFromHog, bool bGuaranteeNoDuplicateRemoves)
{
	Container *temp;
	void *data;
	int dummy;
	int index;
	PERFINFO_AUTO_START("objRemoveFromContainerStore",1);	
	objLockContainerStore_Write(base);
	index = objContainerStoreFindIndex(base,objectID);


	if (index < 0)
	{
		PERFINFO_AUTO_STOP();
		objUnlockContainerStore_Write(base);
		return 0;
	}
	temp = base->containers[index];
	data = temp->containerData;

	EARRAY_FOREACH_BEGIN(base->indices, i);
	{
		objIndexObtainWriteLock(base->indices[i]);
		if(base->requiresHeaders)
			objIndexRemove(base->indices[i], temp->header);
		else
			objIndexRemove(base->indices[i], data);
		objIndexReleaseWriteLock(base->indices[i]);
	}
	EARRAY_FOREACH_END;

	eaRemoveFast(&base->containers,index);
	objFixLookupTable(base,index);

	if (temp->meta.containerState == CONTAINERSTATE_OWNED)
		base->ownedContainers--;

	if (base->removeCallback) // must be called before the container object is destroyed
		base->removeCallback(NULL, data);

	if (removeFromHog) // objRegisterRemovedContainer can set temp to NULL. If it does, it will unlock the container
		objRegisterRemovedContainer(&temp, bGuaranteeNoDuplicateRemoves);

	if(temp && temp->lock) // Unlock the container if it still exists and is locked here.
		objUnlockContainer(&temp);

	if(temp)
		objDestroyContainer(temp);
	stashIntRemoveInt(base->lookupTable,objectID,&dummy);
	base->totalContainers--;
	objUnlockContainerStore_Write(base);
	PERFINFO_AUTO_STOP();
	return 1;
}

bool objRemoveDeletedFromContainerStoreAndHog(ContainerStore *base, ContainerID objectID, bool removeFromHog)
{
	return objRemoveDeletedFromContainerStoreAndHogEx(base, objectID, removeFromHog, false);
}

bool objRemoveDeletedFromContainerStoreAndHogEx(ContainerStore *base, ContainerID objectID, bool removeFromHog, bool bGuaranteeNoDuplicateRemoves)
{
	Container *temp;
	void *data;
	int dummy;
	int index;
	PERFINFO_AUTO_START("objRemoveFromContainerStore",1);
	objLockContainerStoreDeleted_Write(base);
	index = objContainerStoreFindDeletedIndex(base,objectID);


	if (index < 0)
	{
		objUnlockContainerStoreDeleted_Write(base);
		PERFINFO_AUTO_STOP();
		return 0;
	}
	temp = base->deletedContainerCache[index];
	data = temp->containerData;

	EARRAY_FOREACH_BEGIN(base->deletedIndices, i);
	{
		objIndexObtainWriteLock(base->deletedIndices[i]);
		if(base->requiresHeaders)
			objIndexRemove(base->deletedIndices[i], temp->header);
		else
			objIndexRemove(base->deletedIndices[i], data);
		objIndexReleaseWriteLock(base->deletedIndices[i]);
	}
	EARRAY_FOREACH_END;

	eaRemoveFast(&base->deletedContainerCache,index);
	objFixDeletedCleanupTables(base, objectID, true, true);
	objFixDeletedLookupTable(base,index);

	if (base->removeCallback) // must be called before the container object is destroyed
		base->removeCallback(NULL, data);

	if (removeFromHog) // objRegisterRemovedContainer can set temp to NULL. If it does, it will unlock the container
		objRegisterRemovedContainer(&temp, bGuaranteeNoDuplicateRemoves);

	if(temp && temp->lock) // Unlock the container if it still exists and is locked here.
		objUnlockContainer(&temp);

	if(temp)
		objDestroyContainer(temp);
	stashIntRemoveInt(base->deletedLookupTable,objectID,&dummy);
	
	stashIntRemovePointer(base->deletedCleanUpLookupTable, objectID, NULL);
	base->deletedContainers--;

	objUnlockContainerStoreDeleted_Write(base);

	PERFINFO_AUTO_STOP();
	return 1;
}

void objDestroyAllContainers(void)
{
	int i,j;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (!store->containerSchema)
		{
			continue;
		}
		objLockContainerStore_Write(store);
		for (j = 0; j < eaSize(&store->containers); j++)
		{
			objRegisterRemovedContainer(&store->containers[j], true);
			if(store->containers[j])
				objDestroyContainer(store->containers[j]);
		}
		eaDestroy(&store->containers);
		stashTableClear(store->lookupTable);
		store->ownedContainers = 0;
		store->totalContainers = 0;
		objUnlockContainerStore_Write(store);

		objLockContainerStoreDeleted_Write(store);
		for (j = 0; j < eaSize(&store->deletedContainerCache); j++)
		{
			objRegisterRemovedContainer(&store->deletedContainerCache[j], true);
			if(store->deletedContainerCache[j])
				objDestroyContainer(store->deletedContainerCache[j]);
		}
		eaDestroy(&store->deletedContainerCache);
		stashTableClear(store->deletedLookupTable);
		stashTableClear(store->deletedCleanUpLookupTable);
		clearArrayEx(store->deletedContainerQueue, NULL);
		store->deletedContainers = 0;
		objUnlockContainerStoreDeleted_Write(store);
	}
}


int objCountTotalContainers(void)
{
	int count = 0;
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (!store->containerSchema)
		{
			continue;
		}
		count += store->totalContainers;
	}

	return count;
}

int objCountOwnedContainers(void)
{
	int count = 0;
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (!store->containerSchema)
		{
			continue;
		}
		count += store->ownedContainers;
	}

	return count;
}

int objCountDeletedContainers(void)
{
	int count = 0;
	int i;
	for (i = 0; i < GLOBALTYPE_MAXTYPES; i++)
	{
		ContainerStore *store = &gContainerRepository.containerStores[i];
		if (!store->containerSchema)
		{
			continue;
		}
		count += store->deletedContainers;
	}

	return count;
}

int objCountDeletedContainersWithType(GlobalType type)
{
	ContainerStore *store = objFindContainerStoreFromType(type);
	if (!store)
	{
		return 0;
	}
	return store->deletedContainers;
}

int objCountOwnedContainersWithType(GlobalType type)
{
	ContainerStore *store = objFindContainerStoreFromType(type);
	if (!store)
	{
		return 0;
	}
	return store->ownedContainers;
}

int objCountUnownedContainersWithType(GlobalType type)
{
	return objCountTotalContainersWithType(type) - objCountOwnedContainersWithType(type);
}

int objCountTotalContainersWithType(GlobalType type)
{
	ContainerStore *store = objFindContainerStoreFromType(type);
	if (!store)
	{
		return 0;
	}
	return store->totalContainers;
}

int objCountTotalContainersInStore(ContainerStore *store)
{
	if (!store)
	{
		return 0;
	}
	return store->totalContainers;
}

int objContainerRootPathLookup(const char* name, const char *key, ParseTable** table, void** structptr, int* column)
{
	ContainerSchema *schema;
	ContainerStore *store;
	Container *container;
	GlobalType type;
	int containerID;
	int index;

	PERFINFO_AUTO_START("objContainerRootPathLookup",1);

	type = NameToGlobalType(name);

	if (!key || !key[0] || !name || !name[0]  || type == GLOBALTYPE_NONE)
	{
		PERFINFO_AUTO_STOP();
		return ROOTPATH_UNHANDLED;
	}
	schema = objFindContainerSchema(type);
	if (!schema)
	{
		PERFINFO_AUTO_STOP();
		return ROOTPATH_UNHANDLED;	
	}
	// At this point, we assume base is a ContainerRepository

	store = objFindContainerStoreFromType(type);
	if (!store)
	{
		PERFINFO_AUTO_STOP();
		return ROOTPATH_NOTFOUND;
	}
	containerID = atoi(key);
	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store,containerID);
	if (index < 0)
	{
		PERFINFO_AUTO_STOP();
		objUnlockContainerStore_ReadOnly(store);
		return ROOTPATH_NOTFOUND;
	}
	container = store->containers[index];
	objUnlockContainerStore_ReadOnly(store);
	*table = container->containerSchema->classParse;
	*structptr = container->containerData;
	*column = -1;
	PERFINFO_AUTO_STOP();
	return ROOTPATH_FOUND;
}

static ContainerStore *objGetStoreForRegisterCallback(GlobalType containerType)
{
	ContainerStore *store = objFindOrCreateContainerStoreFromType(containerType);
	ContainerSchema *schema = objFindContainerSchema(containerType);

	if (!schema)
	{
		Errorf("Invalid container type: %d", containerType);
		return NULL;
	}

	if (!store)
	{
		store = objCreateContainerStore(schema);
		if (!store)
		{
			Errorf("Invalid container type: %d", containerType);
			return NULL;
		}
	}

	return store;
}

void objRegisterContainerTypeAddCallback(GlobalType containerType, ContainerExistenceCallback cb)
{
	ContainerStore *store = objGetStoreForRegisterCallback(containerType);
	if (store) store->addCallback = cb;
}

void objRegisterContainerTypeRemoveCallback(GlobalType containerType, ContainerExistenceCallback cb)
{
	ContainerStore *store = objGetStoreForRegisterCallback(containerType);
	if (store) store->removeCallback = cb;
}

void objRegisterContainerTypeDeleteCallback(GlobalType containerType, ContainerExistenceCallback cb)
{
	ContainerStore *store = objGetStoreForRegisterCallback(containerType);
	if (store) store->deleteCallback = cb;
}

void objRegisterContainerTypeUndeleteCallback(GlobalType containerType, ContainerExistenceCallback cb)
{
	ContainerStore *store = objGetStoreForRegisterCallback(containerType);
	if (store) store->undeleteCallback = cb;
}

void objRegisterContainerTypeCommitCallback(GlobalType containerType, CommitContainerCallback cb, char *matchString, bool bRunOnce, bool bRunOnceWithAllPathOps, bool bPreCommit, CommitContainerCallbackFilter filterCallback)
{
	ContainerSchema *schema = objFindContainerSchema(containerType);
	if (schema)
	{
		CommitCallbackStruct *newStruct = calloc(sizeof(CommitCallbackStruct),1);
		newStruct->commitCallback = cb;
		newStruct->matchString = strdup(matchString);
		newStruct->bRunOnce = bRunOnce;
		newStruct->bRunOnceWithAllPathOps = bRunOnceWithAllPathOps;
		newStruct->bPreCommit = bPreCommit;
		newStruct->filterCallback = filterCallback;

		eaPush(&schema->commitCallbacks,newStruct);
	}
}

void objRegisterGlobalCommitCallback(CommitContainerCallback cb, char *matchString, bool bRunOnce, bool bPreCommit)
{
	CommitCallbackStruct *newStruct = calloc(sizeof(CommitCallbackStruct),1);
	newStruct->commitCallback = cb;
	newStruct->matchString = strdup(matchString);
	newStruct->bRunOnce = bRunOnce;
	newStruct->bPreCommit = bPreCommit;

	eaPush(&gContainerRepository.commitCallbacks,newStruct);
}

// Initialize a container iterator, that iterates over one container type
// Initializes to 0, no locking needed
void objInitContainerIteratorFromTypeEx(GlobalType containerType, ContainerIterator *iter, bool keepLock, bool readOnly)
{
	ContainerStore *store = NULL;
	store = objFindContainerStoreFromType(containerType);

	memset(iter, 0, sizeof(ContainerIterator));

	iter->store = iter->initStore = store;
	if(store && keepLock)
		objLockContainerStore(store, readOnly);
}

// Initializes to a particular container index, must lock when getting the index, does not guarantee to point to the container later
// If you want to have guarantees, get the lock outside the iteration
void objInitContainerIteratorFromContainerEx(Container *con, ContainerIterator *iter, bool keepLock, bool readOnly)
{
	ContainerStore *store = objFindContainerStoreFromType(con->containerType);
	int index;

	if(store)
		objLockContainerStore(store, readOnly);
	index = objContainerStoreFindIndex(store, con->containerID);

	if(index < 0)
	{
		index = eaSize(&store->containers); // Makes the first call to objGetNextContainerFromIterator() return NULL
	}
	if(!keepLock && store)
		objUnlockContainerStore(store, readOnly);
	memset(iter, 0, sizeof(ContainerIterator));

	iter->store = iter->initStore = store;
	iter->storeIndex = index;
	
}

// Initialize a container iterator from a ContainerStore
// Initializes to 0, no locking needed
void objInitContainerIteratorFromStoreEx(ContainerStore *store, ContainerIterator *iter, bool keepLock, bool readOnly)
{
	memset(iter, 0, sizeof(ContainerIterator));

	iter->store = iter->initStore = store;

	if(store && keepLock)
		objLockContainerStore(store, readOnly);
}

// Initializes a container iterator that iterates over all containers
// Initializes to 0, no locking needed
void objInitAllContainerIteratorEx(ContainerIterator *iter, bool keepLock, bool readOnly)
{
	memset(iter, 0, sizeof(ContainerIterator));

	iter->store = iter->initStore = &gContainerRepository.containerStores[0];
	iter->repository = &gContainerRepository;
	if(iter->store)
		objLockContainerStore(iter->store, readOnly);
}


// Returns the next Container (wrapper object) from an iterator
// This is not guaranteed to be valid or safe if modifications have been made to the container store
// If that happens, you should re-init the iterator
// Locks while getting the container
static __forceinline Container *objGetNextContainerFromIterator_internal(ContainerIterator *iter, bool alreadyHasLock, bool readOnly)
{
	int storeSize;
	Container *retVal;
	bool internalReadOnly = alreadyHasLock ? readOnly : true;

	if (!iter || !iter->store)
	{
		return NULL;
	}

	if(!alreadyHasLock)
		objLockContainerStore(iter->store, internalReadOnly);
	storeSize = eaSize(&iter->store->containers);
	if (!iter->store || iter->storeIndex < 0 || iter->storeIndex > storeSize)
	{
		if(!alreadyHasLock)
			objUnlockContainerStore(iter->store, internalReadOnly);
		return NULL;
	}

	if (iter->storeIndex == storeSize)
	{
		if (!iter->repository)
		{
			if(!alreadyHasLock)
				objUnlockContainerStore_ReadOnly(iter->store);
			return NULL;
		}

		objUnlockContainerStore(iter->store, internalReadOnly);

		do 
		{
			iter->repositoryIndex++;
			
			if(iter->repositoryIndex >= GLOBALTYPE_MAXTYPES)
				break;

			if(!iter->repository->containerStores[iter->repositoryIndex].containerSchema)
				continue;

			objLockContainerStore(&iter->repository->containerStores[iter->repositoryIndex], internalReadOnly);
			if(!eaSize(&iter->repository->containerStores[iter->repositoryIndex].containers))
			{
				objUnlockContainerStore(&iter->repository->containerStores[iter->repositoryIndex], internalReadOnly);
				continue;
			}

			break;
		} while(true); // loops until the index walks off the end or the store has something in it.

		if (iter->repositoryIndex >= GLOBALTYPE_MAXTYPES)
		{
			return NULL;
		}

		iter->store = &iter->repository->containerStores[iter->repositoryIndex];
		iter->storeIndex = 0;	
	}
	retVal = iter->store->containers[iter->storeIndex++];
	if(!alreadyHasLock)
		objUnlockContainerStore(iter->store, internalReadOnly);
	return retVal;
}

//wrapper around the old objGetNextContainerFromIterator which does the "first get all the ones with containerData, then
//do a whole second pass which gets the ones without containerData" logic
Container *objGetNextContainerFromIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly)
{
	while (1)
	{
		Container *pContainer = objGetNextContainerFromIterator_internal(iter, alreadyHasLock, readOnly);

		if (pContainer)
		{
			if (iter->bSecondPass)
			{
				if (pContainer->containerData == NULL)
				{
					return pContainer;
				}
			}
			else
			{
				if (pContainer->containerData)
				{
					return pContainer;
				}
				else
				{
					iter->bNeedSecondPass = true;
				}
			}
		}
		else
		{
			if (iter->bSecondPass || !iter->bNeedSecondPass)
			{
				objClearContainerIteratorEx(iter, alreadyHasLock, readOnly);
				memset(iter, 0, sizeof(ContainerIterator));
				return NULL;
			}

			iter->bSecondPass = true;
			iter->store = iter->initStore;
			iter->repositoryIndex = iter->storeIndex = 0;
		}
	}
}


// Returns the object wrapped by the next Container
void *objGetNextObjectFromIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly)
{
	Container *con = objGetNextContainerFromIteratorEx(iter, alreadyHasLock, readOnly);
	if (con)
	{
		return con->containerData;
	}
	return NULL;
}

void objClearContainerIteratorEx(ContainerIterator *iter, bool alreadyHasLock, bool readOnly)
{
	if (!iter || !iter->store)
	{
		return;
	}

	if(alreadyHasLock)
		objUnlockContainerStore(iter->store, readOnly);
}

Container *objAddExistingContainerToRepository(GlobalType containerType, ContainerID containerID, void *wrappedObject)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	Container *container;
	int index;
	ContainerSchema *schema = objFindContainerSchema(containerType);

	PERFINFO_AUTO_START("objAddExistingContainerToRepository",1);

	if (!schema)
	{
		Errorf("Invalid container type: %d",containerType);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (!store)
	{
		store = objCreateContainerStore(schema);
	}
	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store, containerID);
	if (index >= 0)
	{
		PERFINFO_AUTO_STOP();
		objUnlockContainerStore_ReadOnly(store);
		return 0;
	}
	else
	{
		container = objCreateContainer(store->containerSchema);
		container->containerData = wrappedObject;
		if(!IsThisCloneObjectDB())
			objChangeContainerState(container, CONTAINERSTATE_OWNED, GetAppGlobalType(), GetAppGlobalID());
		if (schema->registerCallback)
		{
			schema->registerCallback(schema, wrappedObject);
		}
	}

	objUnlockContainerStore_ReadOnly(store);

	if (!containerID)
	{
		objSetNewContainerID(container,0,-1);
	}

	if (!objAddToContainerStore(store,container, containerID, true, 0))
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: Bad TextParser Data", GlobalTypeToName(containerType), containerID);
		objRegisterInvalidContainer(container);
		objDestroyContainer(container);
		container = NULL;
	}

	PERFINFO_AUTO_STOP();
	return container;
}

Container *objAddToRepositoryFromText(GlobalType containerType, ContainerID containerID, char *structstring)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	Container *container;
	int index;
	ContainerSchema *schema = objFindContainerSchema(containerType);
	char fakeFileName[MAX_PATH];
	bool ok = true;

	PERFINFO_AUTO_START("objAddToRepositoryFromText",1);
	if (!schema)
	{
		Errorf("Invalid container type: %d",containerType);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (!store)
	{
		store = objCreateContainerStore(schema);
	}
	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store, containerID);
	if (index >= 0)
	{
		PERFINFO_AUTO_STOP();
		objUnlockContainerStore_ReadOnly(store);
		return NULL;
	}
	else
	{
		container = objCreateContainer(store->containerSchema);
		container->containerData = objCreateContainerObject(store->containerSchema, "Creating container in objAddToRepositoryFromText");
		if(!IsThisCloneObjectDB())
			objChangeContainerState(container, CONTAINERSTATE_OWNED, GetAppGlobalType(), GetAppGlobalID());
	}

	objUnlockContainerStore_ReadOnly(store);

	sprintf(fakeFileName, "%s[%d]", GlobalTypeToName(containerType), containerID);
	if (ParserReadTextForFile(structstring, fakeFileName, schema->classParse,container->containerData, 0))
	{
		verbose_printf(" Adding Container [%d] to store.\n", containerID);
		objSetNewContainerID(container,0,-1);
		if (!objAddToContainerStore(store,container,containerID,true, 0))
		{
			ok = false;
		}
	}

	if (!ok)
	{
		log_printf(LOG_CONTAINER, "Failed to read container %s[%d]: Bad TextParser Data", GlobalTypeToName(containerType), containerID);
		objRegisterInvalidContainer(container);
		objDestroyContainer(container);
		container = NULL;
	}

	PERFINFO_AUTO_STOP();
	return container;
}

Container *objAddToRepositoryFromString(GlobalType containerType, ContainerID containerID, char *diffString)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	Container *container;
	int index;
	ContainerSchema *schema = objFindContainerSchema(containerType);
	int retVal;

	PERFINFO_AUTO_START("objAddToRepositoryFromString",1);
	if (!schema)
	{
		Errorf("Invalid container type: %d",containerType);
		PERFINFO_AUTO_STOP();
		return 0;
	}

	if (!store)
	{
		store = objCreateContainerStore(schema);
	}
	objLockContainerStore_ReadOnly(store);
	index = objContainerStoreFindIndex(store, containerID);
	if (index >= 0)
	{
		PERFINFO_AUTO_STOP();
		objUnlockContainerStore_ReadOnly(store);
		return 0;
	}
	else
	{
		container = objCreateContainer(store->containerSchema);
		container->containerData = objCreateContainerObject(store->containerSchema, "Creating container in objAddToRepositoryFromString");
		// The clone objectdb doesn't get to own containers
		if(!IsThisCloneObjectDB())
			objChangeContainerState(container, CONTAINERSTATE_OWNED, GetAppGlobalType(), GetAppGlobalID());
	}

	objUnlockContainerStore_ReadOnly(store);

	if (diffString && diffString[0])
	{
		retVal = objPathParseAndApplyOperations(container->containerSchema->classParse,container->containerData,diffString);
	}

	if (!objAddToContainerStore(store,container, containerID, true, 0))
	{
		objDestroyContainer(container);
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return container;
}

int objRemoveContainerFromRepository(GlobalType containerType, ContainerID containerID)
{
	return objRemoveContainerFromRepositoryAndHog(containerType, containerID, true);
}

int objRemoveContainerFromRepositoryAndHog(GlobalType containerType, ContainerID containerID, bool removeFromHog)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);

	if (!store)
	{
		return 0;
	}
	return objRemoveFromContainerStoreAndHog(store,containerID, removeFromHog);
}

int objRemoveDeletedContainerFromRepository(GlobalType containerType, ContainerID containerID)
{
	return objRemoveDeletedContainerFromRepositoryAndHog(containerType, containerID, true);
}

int objRemoveDeletedContainerFromRepositoryAndHog(GlobalType containerType, ContainerID containerID, bool removeFromHog)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);

	if (!store)
	{
		return 0;
	}
	return objRemoveDeletedFromContainerStoreAndHog(store,containerID, removeFromHog);
}

void objRemoveAllContainersWithType(GlobalType containerType)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	if (store)
	{
		while (eaSize(&store->containers) > 0)
		{
			objRemoveFromContainerStoreAndHogEx(store, store->containers[0]->containerID, true, true);
		}
	}
}

void objContainerPrepareForModify(Container *container)
{
	if (container->isTemporary) return;
	if (container->savedUpdate)
	{
		objHandleUpdateDuringSave(container);
	}
}

void objContainerMarkModified(Container *container)
{

	if (!container->isModified && GlobalTypeSchemaType(container->containerType) == SCHEMATYPE_PERSISTED && objGetContainerSourcePath())
	{	
		if (!container->isTemporary)
		{
			TrackedContainer *pModifiedCon = StructCreate(parse_TrackedContainer);
			pModifiedCon->conRef.containerType = objGetContainerType(container);
			pModifiedCon->conRef.containerID = objGetContainerID(container);
			objLockGlobalContainerRepository();
			if(objGetContainerType(container) == GLOBALTYPE_ENTITYPLAYER)
				AddContainerToTrackedContainerArray(&gContainerRepository.modifiedEntityPlayers, pModifiedCon);
			else if (objGetContainerType(container) == GLOBALTYPE_ENTITYSAVEDPET)
				AddContainerToTrackedContainerArray(&gContainerRepository.modifiedEntitySavedPets, pModifiedCon);
			else
				AddContainerToTrackedContainerArray(&gContainerRepository.modifiedContainers, pModifiedCon);
			objUnlockGlobalContainerRepository();
		}
		container->isModified = true;
	}
}

ContainerID objContainerGetMaxID(GlobalType containerType)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	if (!store)
		return 0;
	return store->maxID;
}

ObjectIndex* objFindContainerStoreIndexWithPath(ContainerStore *store, const char *path)
{
	//ObjectPath *opath = NULL;
	if (!store) 
		return NULL;
	if (!path) 
		return NULL;

	//Find a cached objectpath, if we have an index it will use a cached path.
	//if (!ParserResolvePathEx(path, store->containerSchema->classParse, NULL, NULL, NULL, NULL, NULL, &opath, NULL, NULL, NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH)) 
	//	return NULL;

	//if (!opath) 
	//	return NULL;

	EARRAY_FOREACH_BEGIN(store->indices, i);
	{
		assert(store->indices[i]->columns);
		assert(store->indices[i]->columns[0]);
		assert(store->indices[i]->columns[0]->colPath);
		assert(store->indices[i]->columns[0]->colPath->key);
		if (!stricmp(store->indices[i]->columns[0]->colPath->key->pathString, path)) 
		//if (store->indices[i]->columns[0]->colPath == opath)
			return store->indices[i];
	}
	EARRAY_FOREACH_END;
	return NULL;
}

ObjectIndex* objAddContainerStoreIndexWithPathsInternal(ContainerStore *store, ObjectIndex *** indices, const char *paths, va_list vl)
{
	ObjectPath **objectpaths = NULL;
	const char **origpaths = NULL;
	char *result = 0;
	const char *path = paths;
	bool ok = true;
	ParseTable *tpi = store->containerSchema->classParse;
	S16 order = OBJINDEX_DEFAULT_ORDER;
	ObjectIndex *oi = NULL;

	estrStackCreate(&result);

	while (ok && path)
	{
		ObjectPath *objectpath;
		//create the ObjectPath by resolving it.
		ok = ParserResolvePathEx(path, tpi, NULL, NULL, NULL, NULL, NULL, &objectpath, &result, NULL, NULL, OBJPATHFLAG_DONTLOOKUPROOTPATH|OBJPATHFLAG_INCREASEREFCOUNT);
		if (!ok)
		{
			char * error = 0;
			EARRAY_FOREACH_BEGIN(objectpaths, i);
			{
				InterlockedDecrement(&objectpaths[i]->refcount);
			}
			EARRAY_FOREACH_END;
			eaDestroy(&objectpaths);

			estrStackCreate(&error);
			estrPrintf(&error, "Could not resolve path: %s for ObjectIndex creation. result:%s\n", path, result);
			assertmsg(false, error);
			estrDestroy(&error);
		}
		eaPush(&objectpaths, objectpath);
		eaPush(&origpaths, path);
		path = va_arg(vl, char *);
	}

	estrDestroy(&result);

	oi = objIndexCreate(order, 0, objectpaths, origpaths, store->headerFieldTable);

	eaPush(indices, oi);

	eaDestroy(&objectpaths);
	eaDestroy(&origpaths);

	return oi;
}

ObjectIndex* objAddContainerStoreIndexWithPaths(ContainerStore *store, const char *paths, ...)
{
	ObjectIndex *oi = NULL;
	va_list vl;

	// Only add indexes if !lazyLoad or requiresHeader.
	// The paths used must be in the header.

	va_start(vl, paths);
	oi = objAddContainerStoreIndexWithPathsInternal(store, &store->indices, paths, vl);
	va_end(vl);

	return oi;
}

ObjectIndex* objAddContainerStoreDeletedIndexWithPaths(ContainerStore *store, const char *paths, ...)
{
	ObjectIndex *oi = NULL;
	va_list vl;

	va_start(vl, paths);
	oi = objAddContainerStoreIndexWithPathsInternal(store, &store->deletedIndices, paths, vl);
	va_end(vl);

	return oi;
}

U32 objGetDeletedTime(GlobalType containerType, ContainerID containerID)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	if(store)
	{
		DeletedContainerQueueEntry *entry;
		if(stashIntFindPointer(store->deletedCleanUpLookupTable, containerID, &entry))
		{
			return entry->iDeletedTime;
		}
	}
	return 0;
}

#define DCQ_INITIAL_SIZE_PROD 10000
#define DCQ_INITIAL_SIZE_DEV 10

// Sets how long to keep deleted characters in the cache.
U32 gCachedDeleteTimeout = DAYS(8);
AUTO_CMD_INT(gCachedDeleteTimeout, CachedDeleteTimeout) ACMD_CMDLINE;

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_NAME(SetCachedDeleteTimeout) ACMD_ACCESSLEVEL(9);
void SetCachedDeleteTimeout(U32 timeout)
{
	gCachedDeleteTimeout = timeout;
}

U32 GetCachedDeleteTimeout()
{
	return gCachedDeleteTimeout;
}

void dcqIndexUpdate(DeletedContainerQueueEntry* item, int index)
{
	assert(item);
	item->heapIndex = index;
}

void dcqPush(ContainerStore *base, DeletedContainerQueueEntry *entry)
{
	if(!base->deletedContainerQueue)
	{
		base->deletedContainerQueue = createArray();
		initArray(base->deletedContainerQueue, isProductionMode() ? DCQ_INITIAL_SIZE_PROD : DCQ_INITIAL_SIZE_DEV);
	}
	pqPush(base->deletedContainerQueue, entry, defaultDCQCompare, dcqIndexUpdate);
}

void dcqRemove(ContainerStore *base, DeletedContainerQueueEntry *entry)
{
	if(!base->deletedContainerQueue)
		return;
	pqRemove(base->deletedContainerQueue, entry->heapIndex, defaultDCQCompare, dcqIndexUpdate);
}

U32 dccGetDestroyTime(const DeletedContainerQueueEntry *entry)
{
	U32 destroyTime = entry->destroyTimeFunc ? entry->destroyTimeFunc(entry) : (entry->iDeletedTime + gCachedDeleteTimeout);
	return destroyTime;
}

int defaultDCQCompare(const DeletedContainerQueueEntry *lhs, const DeletedContainerQueueEntry *rhs)
{
	U32 lhsTime = dccGetDestroyTime(lhs);
	U32 rhsTime = dccGetDestroyTime(rhs);
	if(lhsTime > rhsTime)
		return -1;
	else if(lhsTime < rhsTime)
		return 1;
	else
		return 0;
}

void objContainerStoreSetDestroyTimeFunc(ContainerStore *store, getDCCDestroyTimeFunc customGetDestroyTimeFunc, getDCCDestroyTimeUserDataFunc customGetDestroyTimeDataFunc)
{
	store->deletedContainerQueueDestroyTimeFunc = customGetDestroyTimeFunc;
	store->deletedContainerQueueDestroyTimeDataFunc = customGetDestroyTimeDataFunc;
}



typedef struct NonBlockingContainerIterator
{
	StashTableIterator iterator;
	U32 iStashTableSize;
	NonBlockingContainerIteration_ContainerCB pContainerCB;
	NonBlockingContainerIteration_ResetCB pResetCB;
	void *pUserData;
	ContainerStore *pStore;
	NonBlockingContainerIterationSummary summary;
} NonBlockingContainerIterator;

static NonBlockingContainerIterator **sppNonBlockingContainerIterators = NULL;

static NonBlockingContainerIterationSummary sEmptySummary = {0};

bool IterateContainersNonBlocking(GlobalType eContainerType, NonBlockingContainerIteration_ContainerCB pContainerCB, 
	NonBlockingContainerIteration_ResetCB pResetCB, void *pUserData)
{
	ContainerStore *pStore = objFindContainerStoreFromType(eContainerType);

	NonBlockingContainerIterator *pIterator;

	if (!pStore)
	{
		return false;
	}

	if (!pStore->lookupTable)
	{

		pContainerCB(NULL, &sEmptySummary, pUserData);
		return true;
	}

	pIterator = calloc(sizeof(NonBlockingContainerIterator), 1);

	objLockContainerStore_ReadOnly(pStore);
	stashGetIterator(pStore->lookupTable, &pIterator->iterator);

	pIterator->iStashTableSize = stashGetMaxSize(pStore->lookupTable);
	pIterator->pStore = pStore;
	pIterator->pContainerCB = pContainerCB;
	pIterator->pResetCB = pResetCB;
	pIterator->pUserData = pUserData;
	pIterator->summary.iStartTime = timeGetTime();
	eaPush(&sppNonBlockingContainerIterators, pIterator);
	objUnlockContainerStore_ReadOnly(pStore);

	return true;
}

static int siNonBlockingContainerIterators_MsecsPerTick = 3;
AUTO_CMD_INT(siNonBlockingContainerIterators_MsecsPerTick, NonBlockingContainerIterators_MsecsPerTick) ACMD_COMMANDLINE;

static int siNonBlockingContainerIterators_Granularity = 10;
AUTO_CMD_INT(siNonBlockingContainerIterators_Granularity, NonBlockingContainerIterators_Granularity) ACMD_COMMANDLINE;

static int siNonBlockingContainerIterators_SkipTicks = 0;
AUTO_CMD_INT(siNonBlockingContainerIterators_SkipTicks, NonBlockingContainerIterators_SkipTicks) ACMD_COMMANDLINE;

//each tick, we pull the first iterator off the front of our list, then return containers from it, stopping ever granularity
//to see if we've run out of time. If we come to the end of the iterator, then we just stop the tick.
void NonBlockingContainerIterators_Tick(void)
{
	S64 iEndingTime = timeGetTime() + siNonBlockingContainerIterators_MsecsPerTick;
	NonBlockingContainerIterator *pIterator = NULL;
	int i;
	bool bDoneWithThisIterator = false;
	static int iSkipTicks = 0;

	if (iSkipTicks)
	{
		iSkipTicks--;
		return;
	}

	pIterator = eaRemove(&sppNonBlockingContainerIterators, 0);
	if (!pIterator)
	{
		return;
	}



	iSkipTicks = siNonBlockingContainerIterators_SkipTicks;
	
	pIterator->summary.iTotalTicks++;

	objLockContainerStore_ReadOnly(pIterator->pStore);
	if (pIterator->iStashTableSize != stashGetMaxSize(pIterator->pStore->lookupTable))
	{
		pIterator->pResetCB(pIterator->pUserData);
		pIterator->iStashTableSize = stashGetMaxSize(pIterator->pStore->lookupTable);
		stashGetIterator(pIterator->pStore->lookupTable, &pIterator->iterator);
		pIterator->summary.iNumResets++;
		pIterator->summary.iTotalFound = 0;
	}

	do
	{
		for (i = 0; i < siNonBlockingContainerIterators_Granularity; i++)
		{
			StashElement elem;
			Container *pContainer;
			U32 iIndex;

			if (!stashGetNextElement(&pIterator->iterator, &elem))
			{
				bDoneWithThisIterator = true;
				break;
			}

			iIndex = stashElementGetInt(elem);
			if (iIndex >= 0)
			{			
				pContainer = pIterator->pStore->containers[iIndex];

				if (pContainer)
				{
					pIterator->summary.iTotalFound++;
					if (!pIterator->pContainerCB(pContainer, &pIterator->summary, pIterator->pUserData))
					{
						bDoneWithThisIterator = true;
						objUnlockContainerStore_ReadOnly(pIterator->pStore);
						SAFE_FREE(pIterator);
						break;
					}
				}
			}
		}

		if (bDoneWithThisIterator && pIterator)
		{
			pIterator->pContainerCB(NULL, &pIterator->summary, pIterator->pUserData);
			objUnlockContainerStore_ReadOnly(pIterator->pStore);
			SAFE_FREE(pIterator);
		}
	}
	while (pIterator && timeGetTime() < iEndingTime);
	if (pIterator)
	{
		objUnlockContainerStore_ReadOnly(pIterator->pStore);
		eaPush(&sppNonBlockingContainerIterators, pIterator);
	}
}

// Expression Query
AUTO_STRUCT;
typedef struct NonBlockingContainerQueryStruct
{
	void *pUserData; NO_AST
	U32 *pContainerIDs;
	char *pExpressionString;
	ExprContext *pExprContext; NO_AST
	Expression *pExpression;  NO_AST
	ContainerStore *pStore; NO_AST 
	NonBlockingQueryCB pCB; NO_AST
	int iMaxToReturn;
} NonBlockingContainerQueryStruct;

static void NonBlockingContainerQueryCBReturn(NonBlockingContainerIterationSummary *pSummary, NonBlockingContainerQueryStruct *pQueryStruct)
{
	if (pQueryStruct->pCB)
		pQueryStruct->pCB(&pQueryStruct->pContainerIDs, pSummary, pQueryStruct->pUserData);

	if (pQueryStruct->pExprContext)
	{
		exprContextDestroy(pQueryStruct->pExprContext);
	}

	if (pQueryStruct->pExpression)
	{
		exprDestroy(pQueryStruct->pExpression);
	}

	StructDestroy(parse_NonBlockingContainerQueryStruct, pQueryStruct);
}

static bool NonBlockingContainerQueryCB(Container *pContainer, NonBlockingContainerIterationSummary *pSummary, NonBlockingContainerQueryStruct *pQueryStruct)
{
	if (!pContainer)
	{
		NonBlockingContainerQueryCBReturn(pSummary, pQueryStruct);
		return false;
	}



	if (pContainer && pContainer->containerData)
	{
		MultiVal answer = {0};
		int iAnswer;


		//Set the default objectpath root
		exprContextSetUserPtr(pQueryStruct->pExprContext, pContainer->containerData, pQueryStruct->pStore->containerSchema->classParse);
		
		//I'm putting this back just in case people got married to the old way of filtering. This is somewhat redundant.
		exprContextSetPointerVar(pQueryStruct->pExprContext, "me", pContainer->containerData, pQueryStruct->pStore->containerSchema->classParse, true, true);

		if (!pQueryStruct->pExpression)
		{

			pQueryStruct->pExpression = exprCreate();
			exprGenerateFromString(pQueryStruct->pExpression, pQueryStruct->pExprContext, pQueryStruct->pExpressionString, NULL);
		}

		exprEvaluate(pQueryStruct->pExpression, pQueryStruct->pExprContext, &answer);

		//Expression will fail for all?
		if (exprContextCheckStaticError(pQueryStruct->pExprContext))
			return true;

		if (answer.type == MULTI_INT)
			iAnswer = QuickGetInt(&answer);
		else
			iAnswer = 0;

		if (iAnswer)
		{
			ea32Push(&pQueryStruct->pContainerIDs, pContainer->containerID);
			if (ea32Size(&pQueryStruct->pContainerIDs) == pQueryStruct->iMaxToReturn)
			{
				NonBlockingContainerQueryCBReturn(pSummary, pQueryStruct);
				return false;
			}
		}
	}

	return true;
}

void NonBlockingContainerQueryResetCB(NonBlockingContainerQueryStruct *pQueryStruct)
{
	ea32Clear(&pQueryStruct->pContainerIDs);
}

bool NonBlockingContainerQuery(GlobalType eType, char *pExpressionString, const char *pCustomExpressionTag, int iMaxToReturn, NonBlockingQueryCB pCB, void *pUserData)
{
	NonBlockingContainerQueryStruct *pQueryStruct;
	ContainerStore *pStore = objFindContainerStoreFromType(eType);	
	static ExprFuncTable* funcTable = NULL;

	if (!pStore)
	{
		if (pCB)
			pCB(NULL, &sEmptySummary, pUserData);
		return false;
	}
	
	pQueryStruct = calloc(sizeof(NonBlockingContainerQueryStruct), 1);
	pQueryStruct->pStore = pStore;

	pQueryStruct->pUserData = pUserData;
	pQueryStruct->pExprContext = exprContextCreate();
	pQueryStruct->pCB = pCB;
	pQueryStruct->iMaxToReturn = iMaxToReturn;
	exprContextSetUserPtrIsDefault(pQueryStruct->pExprContext, true);

	//load expression functions
	if (!funcTable)
	{
		funcTable = exprContextCreateFunctionTable("NonBlockingContainerQuery");
		exprContextAddFuncsToTableByTag(funcTable, "util");
	}

	if (pCustomExpressionTag && *pCustomExpressionTag)
		exprContextAddFuncsToTableByTag(funcTable, pCustomExpressionTag);

	exprContextSetFuncTable(pQueryStruct->pExprContext, funcTable);
	pQueryStruct->pExpressionString = strdup(pExpressionString);

	if(!IterateContainersNonBlocking(eType, NonBlockingContainerQueryCB,NonBlockingContainerQueryResetCB, pQueryStruct))
	{
		NonBlockingContainerQueryCBReturn(&sEmptySummary, pQueryStruct);
		return false;
	}
	
	return true;
}

// Non-expression search
AUTO_STRUCT;
typedef struct NonBlockingContainerSearchStruct
{
	U32 *pContainerIDs;
	ParseTable *searchPti; NO_AST
	void *searchData; NO_AST
	NonBlockingSearchFunc pSearchFunc; NO_AST

	ContainerStore *pStore; NO_AST 
	int iMaxToReturn;
	NonBlockingQueryCB pCB; NO_AST
	void *pUserData; NO_AST
} NonBlockingContainerSearchStruct;

static void NonBlockingContainerSearchCBReturn(NonBlockingContainerIterationSummary *pSummary, NonBlockingContainerSearchStruct *pSearchStruct)
{
	if (pSearchStruct->pCB)
		pSearchStruct->pCB(&pSearchStruct->pContainerIDs, pSummary, pSearchStruct->pUserData);
	if (pSearchStruct->searchPti)
		StructDestroyVoid(pSearchStruct->searchPti, pSearchStruct->searchData);
	StructDestroy(parse_NonBlockingContainerSearchStruct, pSearchStruct);
}

static bool NonBlockingContainerSearchCB(Container *pContainer, NonBlockingContainerIterationSummary *pSummary, NonBlockingContainerSearchStruct *pSearchStruct)
{
	if (!pContainer)
	{
		NonBlockingContainerSearchCBReturn(pSummary, pSearchStruct);
		return false;
	}
	
	if (pContainer && pContainer->containerData)
	{
		if (pSearchStruct->pSearchFunc(pContainer->containerData, pSearchStruct->searchData))
		{
			ea32Push(&pSearchStruct->pContainerIDs, pContainer->containerID);
			if (ea32Size(&pSearchStruct->pContainerIDs) == pSearchStruct->iMaxToReturn)
			{
				NonBlockingContainerSearchCBReturn(pSummary, pSearchStruct);
				PERFINFO_AUTO_STOP_FUNC();
				return false;
			}
		}
	}
	return true;
}

void NonBlockingContainerSearchResetCB(NonBlockingContainerSearchStruct *pSearchStruct)
{
	ea32Clear(&pSearchStruct->pContainerIDs);
}

// Search Data is destroyed by NonContainerIterator code if ParseTable is passed in
bool NonBlockingContainerSearch(GlobalType eType, NonBlockingSearchFunc searchFunc, ParseTable *searchPti, void *searchData, int iMaxToReturn, NonBlockingQueryCB pCB, void *pUserData)
{
	NonBlockingContainerSearchStruct *pSearchStruct;
	ContainerStore *pStore = objFindContainerStoreFromType(eType);	
	static ExprFuncTable* funcTable = NULL;

	if (!pStore)
	{
		if (pCB)
			pCB(NULL, &sEmptySummary, pUserData);
		return false;
	}

	pSearchStruct = calloc(sizeof(NonBlockingContainerSearchStruct), 1);
	pSearchStruct->pStore = pStore;

	pSearchStruct->pUserData = pUserData;
	pSearchStruct->pCB = pCB;
	pSearchStruct->iMaxToReturn = iMaxToReturn;
	pSearchStruct->pSearchFunc = searchFunc;
	pSearchStruct->searchPti = searchPti;
	pSearchStruct->searchData = searchData;

	if(!IterateContainersNonBlocking(eType, NonBlockingContainerSearchCB, NonBlockingContainerSearchResetCB, pSearchStruct))
	{
		NonBlockingContainerSearchCBReturn(&sEmptySummary, pSearchStruct);
		return false;
	}
	return true;
}

// XMLRPC Container Query
void NonBlockingContainerQuery_XMLRPCCB(U32 **ppOutContainerIDs, NonBlockingContainerIterationSummary *pSummary, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	if (pSlowReturnInfo->eHowCalled == CMD_CONTEXT_HOWCALLED_XMLRPC)
	{
		char *pReturnString = NULL;
		NonBlockingXMLRPCContainerQuery_ReturnStruct returnStruct = {0};
		StructInit(parse_NonBlockingXMLRPCContainerQuery_ReturnStruct, &returnStruct);
		returnStruct.pIDs = *ppOutContainerIDs;

		estrStackCreate(&pReturnString);
		XMLRPC_WriteSimpleStructResponse(&pReturnString, &returnStruct, parse_NonBlockingXMLRPCContainerQuery_ReturnStruct);

		DoSlowCmdReturn(1,pReturnString, pSlowReturnInfo);

		estrDestroy(&pReturnString);

		returnStruct.pIDs = NULL;
		StructDeInit(parse_NonBlockingXMLRPCContainerQuery_ReturnStruct, &returnStruct);
	}
	else
	{
		DoSlowCmdReturn(1, "Why did you call NonBlockingContainerQuery_XMLRPC not from XMLRPC, you silly person?", pSlowReturnInfo);
	}
	
	free(pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC);
void NonBlockingContainerQuery_XMLRPC(CmdContext *pContext, NonBlockingXMLRPCContainerQuery_QueryStruct *pQuery)
{
	CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo = malloc(sizeof(CmdSlowReturnForServerMonitorInfo));
	pContext->slowReturnInfo.bDoingSlowReturn = true;
	memcpy(pSlowReturnInfo, &pContext->slowReturnInfo, sizeof(CmdSlowReturnForServerMonitorInfo));

	NonBlockingContainerQuery(pQuery->eType, pQuery->pExpressionString, NULL, pQuery->iMaxToReturn, NonBlockingContainerQuery_XMLRPCCB, pSlowReturnInfo);
}

ContainerID *GetContainerIDListFromStore(ContainerStore *store, bool deleted)
{
	ContainerID *containerIDArray = NULL;
	int i;
	Container ***containerArray;

	assert(store);
	PERFINFO_AUTO_START_FUNC();
	objLockContainerStore_ReadOnly(store);
	containerArray = deleted ? &store->deletedContainerCache : &store->containers;
	for(i = 0; i < eaSize(containerArray); ++i)
	{
		ea32Push(&containerIDArray, (*containerArray)[i]->containerID);
	}

	objUnlockContainerStore_ReadOnly(store);
	PERFINFO_AUTO_STOP();
	return containerIDArray;
}

ContainerID *GetContainerIDListFromType(GlobalType containerType, bool deleted)
{
	ContainerStore *store = objFindContainerStoreFromType(containerType);
	if(store)
	{
		return GetContainerIDListFromStore(store, deleted);
	}

	return NULL;
}

void ForEachContainerOfTypeEx(GlobalType containerType, ForEachContainerCBType forEachCallback, ForEachContainerContinueCBType continueCallback, void *userData, bool unpack, bool deleted)
{
	ContainerID *containerIDArray;
	int numContainers;
	int i;

	PERFINFO_AUTO_START_FUNC();
	containerIDArray = GetContainerIDListFromType(containerType, deleted);
	numContainers = ea32Size(&containerIDArray);

	assert(forEachCallback);

	for(i = 0; i < numContainers && (!continueCallback || continueCallback(userData)); ++i)
	{
		Container *con = objGetContainerGeneralEx(containerType, containerIDArray[i], unpack, false, true, deleted);
		if(con)
		{
			forEachCallback(con, userData);
			objUnlockContainer(&con);
		}
	}

	ea32Destroy(&containerIDArray);
	PERFINFO_AUTO_STOP();
}

void ForEachContainerInRepositoryEx(ForEachContainerCBType forEachCallback, ForEachContainerContinueCBType continueCallback, void *userData, bool unpack, bool deleted)
{
	int i;
	for(i = 0; i < GLOBALTYPE_MAX; ++i)
	{
		ForEachContainerOfTypeEx(i, forEachCallback, continueCallback, userData, unpack, deleted);
	}
}

#include "autogen/objcontainer_h_ast.c"
#include "autogen/objcontainer_c_ast.c"

