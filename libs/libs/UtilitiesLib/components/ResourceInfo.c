#include "ResourceInfo.h"
#include "StringCache.h"
#include "ResourceSystem_Internal.h"

#include "estring.h"
#include "file.h"
#include "StringUtil.h"
#include "objPath.h"
#include "MemoryPool.h"
#include "Message.h"
#include "SharedMemory.h"
#include "FileVersionList.h"
#include "strings_opt.h"
#include "fileutil.h"
#include "timing.h"
#include "ugcProjectUtils.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

MP_DEFINE(ResourceInfo);
MP_DEFINE(ResourceReference);

AUTO_RUN;
void RegisterResourceInfoMemoryPools(void)
{
	MP_CREATE(ResourceInfo, 512);
	MP_CREATE(ResourceReference, 1024);
}

StashTable gResourceDictionaries;
extern bool gCreditMemoryToDictionary; // Defined in ReferenceSystem.c

ResourceDictionary *resGetDictionary(DictionaryHandleOrName pDictName)
{
	const char *refDictName;
	ResourceDictionary *dict;

	dict = RefSystem_GetResourceDictFromNameOrHandle(pDictName);

	if (dict)
		return dict;

	refDictName = RefSystem_GetDictionaryNameFromNameOrHandle(pDictName);
	if (refDictName && stashFindPointer(gResourceDictionaries, refDictName, &dict))
		return dict;
	return NULL;
}

ResourceStatus *resGetStatus(ResourceDictionary *pDict, const char *resourceName)
{
	ResourceStatus *pStatus;
	const char *pRealName;
	if (!pDict)
		return NULL;

	pRealName = allocFindString(resourceName);

	if (!pRealName)
		return NULL;

	if (stashFindPointer(pDict->resourceStatusTable, pRealName, &pStatus))
	{
		return pStatus;
	}
	return NULL;
}

MP_DEFINE(ResourceStatus);

void resDestroyStatus(ResourceDictionary *pDict, const char *resourceName)
{
	ResourceStatus *pStatus;
	const char *pRealName;
	if (!pDict)
		return;

	pRealName = allocFindString(resourceName);

	if (!pRealName)
		return;

	if (stashFindPointer(pDict->resourceStatusTable, pRealName, &pStatus))
	{
		if(pStatus->bInUnreferencedList)
		{
			RemoveResourceFromUnreferencedList(pDict, pStatus);
		}
		MP_FREE(ResourceStatus, pStatus);
		if (pDict->bUsingLoadingTable && pDict->resourceStatusLoadingTable)
			stashRemovePointer(pDict->resourceStatusLoadingTable, pRealName, &pStatus);
		stashRemovePointer(pDict->resourceStatusTable, pRealName, &pStatus);
	}
}

void resDestroyAllStatus(ResourceDictionary *pDictionary)
{
	StashTableIterator iterator;
	StashElement element;
	stashGetIterator(pDictionary->resourceStatusTable, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		ResourceStatus *pResourceStatus = stashElementGetPointer(element);

		MP_FREE(ResourceStatus, pResourceStatus);
	}
	stashTableClear(pDictionary->resourceStatusTable);
	if (pDictionary->bUsingLoadingTable && pDictionary->resourceStatusLoadingTable)
		stashTableClear(pDictionary->resourceStatusLoadingTable);

	// Need to clear out the unreferenced resource list, because they're now all invalid
	if (eaSize(&pDictionary->ppUnreferencedResources))
		eaClear(&pDictionary->ppUnreferencedResources);
}

ResourceStatus *resGetOrCreateStatus(ResourceDictionary *pResDict, const char *resourceName)
{
	ResourceStatus *pStatus;
	const char *pRealName;
	if (!pResDict)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	pRealName = allocAddString(resourceName);

	if (pResDict->resourceStatusTable)
	{
		if (stashFindPointer(pResDict->resourceStatusTable, pRealName, &pStatus))
		{
			if (pResDict->bUsingLoadingTable && pResDict->resourceStatusLoadingTable)
			{
				stashAddPointer(pResDict->resourceStatusLoadingTable, pRealName, pStatus, 0);
			}
			PERFINFO_AUTO_STOP();
			return pStatus;
		}
	}
	else
	{
		pResDict->resourceStatusTable = stashTableCreateAddress(8);
	}

	MP_CREATE(ResourceStatus, 512);
	pStatus = MP_ALLOC(ResourceStatus);

	pStatus->pResourceName = pRealName;

	stashAddPointer(pResDict->resourceStatusTable, pRealName, pStatus, 0);
	if (pResDict->bUsingLoadingTable && pResDict->resourceStatusLoadingTable)
	{
		stashAddPointer(pResDict->resourceStatusLoadingTable, pRealName, pStatus, 0);
	}

	PERFINFO_AUTO_STOP();

	return pStatus;
}

void resDeleteUnusedStatuses(void)
{
	ResourceDictionary *pDictionary;
	ResourceDictionaryIterator iter;

	loadstart_printf("Removing unused resource status...");

	resDictInitIterator(&iter);

	while (pDictionary = resDictIteratorGetNextDictionary(&iter))
	{
		assert (!pDictionary->resourceStatusLoadingTable); // This CANNOT be called while loading is enabled
		if (!pDictionary->bShouldRequestMissingData)
		{		
			resDestroyAllStatus(pDictionary);
		}
		else
		{
			StashTableIterator iterator;
			StashElement element;
			stashGetIterator(pDictionary->resourceStatusTable, &iterator);

			while (stashGetNextElement(&iterator, &element))
			{
				char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];				
				ResourceStatus *pResourceStatus = stashElementGetPointer(element);				

				if (pResourceStatus->bLoadedFromDisk && !resExtractNameSpace(pResourceStatus->pResourceName, ns, base))
				{
					// If we loaded it off disk and it's NOT from a namespace, then delete it because we won't need the info in the future
					assert(stashRemovePointer(pDictionary->resourceStatusTable, pResourceStatus->pResourceName, NULL));
					assert(!pResourceStatus->bInUnreferencedList);
					assert(eaFind(&pDictionary->ppUnreferencedResources, pResourceStatus) < 0);
					MP_FREE(ResourceStatus, pResourceStatus);
				}
			}
			// No resource loaded from disk will ever end up in the unreferenced list
		}

		if (stashGetCount(pDictionary->resourceStatusTable) > 0)
		{
			stashTableFindCurrentBestSize(pDictionary->resourceStatusTable);
		}
		else
		{
			stashTableDestroySafe(&pDictionary->resourceStatusTable);
		}
	}

	// Compact the status pool, as we just freed many
	if (memPoolResourceStatus)		
		mpCompactPool(memPoolResourceStatus);

	loadend_printf(" done.");
}





void resRegisterDictionary_dbg(const char *pDictName, const char *pDictCategory, enumResDictFlags eFlags, ParseTable *pDictTable,
							resCallback_GetObject *pGetObjectCB,
							resCallback_GetObjectPrevious *pGetObjectPreviousCB,
							resCallback_QueueCopyObjectToPrevious *pQueueCopyObjectToPreviousCB,
							resCallback_GetNumberOfObjects *pGetNumObjectsCB,
							resCallback_GetString *pGetLocationCB,
							resCallback_FindDependencies *pFindDependenciesCB,
							resCallback_InitIterator *pInitIteratorCB,
							resCallback_GetNext *pGetNextCB, 
							resCallback_FreeIterator *pFreeIteratorCB,
							resCallback_AddObject *pAddObjectCB,
							resCallback_RemoveObject *pRemoveObjectCB,
							resCallback_GetGlobalTypeAndID *pGetGlobalTypeAndIDCB,
							resCallback_GetVerboseName *pGetVerboseNameCB,
							const char *pDeprecatedName, void *pUserData
							MEM_DBG_PARMS)
{
	ResourceDictionary *pNewDict;
	if (!pDictName || !pDictName[0] || strlen(pDictName) > RESOURCE_DICT_NAME_MAX_SIZE)
	{
		assertmsgf(0, "Invalid name for Global Object Dictionary");
	}

	if (!gResourceDictionaries)
	{
		gResourceDictionaries = stashTableCreateWithStringKeys(64, StashDefault);
	}

	if (resGetDictionary(pDictName))
	{
		assertmsgf(0, "Can't register two Resource Dictionaries with same name %s", pDictName);
	}
	pNewDict = scalloc(sizeof(ResourceDictionary), 1);
	MEM_DBG_STRUCT_PARMS_INIT(pNewDict);

	pNewDict->pDictName = allocAddString(pDictName);
	pNewDict->pDictCategoryName = allocAddString(pDictCategory);
	pNewDict->eFlags = eFlags;
	pNewDict->pDeprecatedName = allocAddString(pDeprecatedName);
	pNewDict->pDictTable = pDictTable;
	assertmsgf(pGetObjectCB, "Can't register Resource Dictionary %s without get object callback", pDictName);
	pNewDict->pGetObjectCB = pGetObjectCB;
	pNewDict->pGetObjectPreviousCB = pGetObjectPreviousCB;
	pNewDict->pQueueCopyObjectToPreviousCB = pQueueCopyObjectToPreviousCB;
	pNewDict->pGetNumObjectsCB = pGetNumObjectsCB;
	pNewDict->pGetLocationCB = pGetLocationCB;
	pNewDict->pFindDependenciesCB = pFindDependenciesCB;
	pNewDict->pInitIteratorCB = pInitIteratorCB;
	pNewDict->pGetNextCB = pGetNextCB;
	pNewDict->pFreeIteratorCB = pFreeIteratorCB;
	pNewDict->pAddObjectCB = pAddObjectCB;
	pNewDict->pRemoveObjectCB = pRemoveObjectCB;
	pNewDict->pGetGlobalTypeAndIDCB = pGetGlobalTypeAndIDCB;
	pNewDict->pGetVerboseNameCB = pGetVerboseNameCB;
	pNewDict->bIsCopyDictionary = strStartsWith(pNewDict->pDictName, CopyDictionaryPrefix);
	pNewDict->iMaxUnreferencedResources = RES_DICT_KEEP_ALL;
	if (gCreditMemoryToDictionary)
	{
		pNewDict->pDictInfo = StructCreate_dbg(parse_ResourceDictionaryInfo, NULL MEM_DBG_PARMS_CALL);
	}
	else
	{
		pNewDict->pDictInfo = StructCreate(parse_ResourceDictionaryInfo);
	}
	pNewDict->pUserData = pUserData;

	DECONST(const char *, pNewDict->pDictInfo->pDictName) = pNewDict->pDictName;
	DECONST(const char *, pNewDict->pDictInfo->pDictCategoryName) = pNewDict->pDictCategoryName;
	DECONST(void *, pNewDict->pDictInfo->pDictTable) = pNewDict->pDictTable;
	
	pNewDict->resourceStatusTable = NULL;
	pNewDict->resourceStatusLoadingTable = NULL;

	stashAddPointer(gResourceDictionaries, pNewDict->pDictName, pNewDict, false);
	pNewDict->pRefDictHandle = pNewDict->pDictName;
}

void *resGetObject(DictionaryHandleOrName pDictName, const char *itemName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		void *pRetVal = pDict->pGetObjectCB(pDict->pRefDictHandle, itemName, pDict->pUserData);

		if (pRetVal && (pDict->eFlags & RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT))
		{
			FixupStructLeafFirst(pDict->pDictTable, pRetVal, FIXUPTYPE_GOTTEN_FROM_RES_DICT, NULL);
		}

		return pRetVal;
	}
	return NULL;
}


bool resFindGlobalTypeAndID(DictionaryHandleOrName pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		if (pDict->pGetGlobalTypeAndIDCB)
		{
			return pDict->pGetGlobalTypeAndIDCB(pDict->pDictName, itemName, pOutType, pOutID, pDict->pUserData);
		}
	}

	return false;
}


void *resGetObjectFromDict(ResourceDictionary *pDict, const char *itemName)
{
	if (pDict)
	{
		void *pRetVal =  pDict->pGetObjectCB(pDict->pRefDictHandle, itemName, pDict->pUserData); // FASTLOAD FIXME. not sure if this is correct. was 'pDict->pDictName'
	
		if (pRetVal && (pDict->eFlags & RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT))
		{
			FixupStructLeafFirst(pDict->pDictTable, pRetVal, FIXUPTYPE_GOTTEN_FROM_RES_DICT, NULL);
		}

		return pRetVal;
	}
	return NULL;
}

ReferentPrevious *resGetObjectPreviousFromDict(ResourceDictionary *pDict, const char *itemName)
{
	if (SAFE_MEMBER(pDict, pGetObjectPreviousCB))
	{
		return pDict->pGetObjectPreviousCB(pDict->pRefDictHandle, itemName, pDict->pUserData);
	}
	return NULL;
}

void resQueueCopyObjectToPrevious(ResourceDictionary *pDict, const char *itemName)
{
	if (SAFE_MEMBER(pDict, pQueueCopyObjectToPreviousCB))
	{
		pDict->pQueueCopyObjectToPreviousCB(pDict->pRefDictHandle, itemName, pDict->pUserData);
	}
}

ParseTable *resDictGetParseTable(DictionaryHandleOrName pDictName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		return pDict->pDictTable;
	}
	return NULL;
}

ResourceDictionaryInfo *resDictGetInfo(DictionaryHandleOrName pDictName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		return pDict->pDictInfo;
	}
	return NULL;
}

const char *resDictGetName(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(dictHandle))
	{
		return pDict->pDictName;
	}
	return NULL;
}

const char *resDictGetDeprecatedName(DictionaryHandleOrName pDictName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		return pDict->pDeprecatedName;
	}
	return NULL;
}

const char *resDictGetParseName(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(dictHandle))
	{
		if (pDict->pParseName)
			return pDict->pParseName;
		return pDict->pDictName;
	}
	return NULL;
}

void resDictInitIterator(ResourceDictionaryIterator *pIterator)
{
	assertmsgf(gResourceDictionaries, "Can't iterate over Resouce Dictionaries when none are defined");
	stashGetIterator(gResourceDictionaries, pIterator);
}

ResourceDictionaryInfo *resDictIteratorGetNextInfo(ResourceDictionaryIterator *pIterator)
{
	ResourceDictionary *resDict;
	StashElement iterElement;
	if (stashGetNextElement(pIterator, &iterElement))
	{
		resDict = stashElementGetPointer(iterElement);
		if (resDict)
		{
			return resDict->pDictInfo;
		}
	}
	return NULL;
}

ResourceDictionary *resDictIteratorGetNextDictionary(ResourceDictionaryIterator *pIterator)
{
	ResourceDictionary *resDict;
	StashElement iterElement;
	if (stashGetNextElement(pIterator, &iterElement))
	{
		resDict = stashElementGetPointer(iterElement);
		if (resDict)
		{
			return resDict;
		}
	}
	return NULL;
}

int resDictGetNumberOfObjects(DictionaryHandleOrName pDictName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		if (pDict->pGetNumObjectsCB)
			return pDict->pGetNumObjectsCB(pDict->pRefDictHandle, pDict->pUserData, pDict->eFlags);
	}
	return 0;
}

// This gets modified at run time
ParseTable parse_DictionaryEArrayTemplate[] =
{
	{ NULL, TOK_INDIRECT | TOK_EARRAY | TOK_STRUCT_X, offsetof(DictionaryEArrayStruct,ppReferents), 0, NULL },
	{ "", 0, 0 },
};

AUTO_RUN;
void RegisterReferenceEarrayTemplate(void)
{
	ParserSetTableInfo(parse_DictionaryEArrayTemplate,sizeof(DictionaryEArrayStruct),"ReferenceEArrayTemplate",NULL,__FILE__, SETTABLEINFO_ALLOW_CRC_CACHING);
}


void resDictEnableEArrayStruct(DictionaryHandleOrName dictHandle, bool bEnable)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	if (bEnable == !!pDictionary->pEArrayStruct)
	{
		return;
	}
	else if (bEnable)
	{
		void *pObject;
		ResourceIterator iter;
		int keyColumn;
		assertmsg(pDictionary->pDictTable,"To maintain an EArray, dictionary must have parse table!");
		assertmsg((keyColumn = ParserGetTableKeyColumn(pDictionary->pDictTable)) >= 0,"ParseTable must have Key to have a managed EArray!");
		pDictionary->pEArrayStruct = stcalloc(sizeof(DictionaryEArrayStruct),1,pDictionary);
		pDictionary->pEArrayStruct->pEArrayParseTable = stcalloc(sizeof(parse_DictionaryEArrayTemplate),1,pDictionary);
		memcpy(pDictionary->pEArrayStruct->pEArrayParseTable,parse_DictionaryEArrayTemplate,sizeof(parse_DictionaryEArrayTemplate));
		pDictionary->pEArrayStruct->pEArrayParseTable[0].name = allocAddString((char*)(pDictionary->pDictName));
		pDictionary->pEArrayStruct->pEArrayParseTable[0].param = ParserGetTableSize(pDictionary->pDictTable);
		pDictionary->pEArrayStruct->pEArrayParseTable[0].subtable = pDictionary->pDictTable;
		steaIndexedEnableVoid(&pDictionary->pEArrayStruct->ppReferents,pDictionary->pDictTable,pDictionary);

		resInitIterator(dictHandle, &iter);
		while (resIteratorGetNext(&iter, NULL, &pObject))
		{
			steaIndexedAdd(&pDictionary->pEArrayStruct->ppReferents, pObject, pDictionary);
		}
		resFreeIterator(&iter);
	}
	else if (!bEnable)
	{
		eaDestroy(&pDictionary->pEArrayStruct->ppReferents);
		SAFE_FREE(pDictionary->pEArrayStruct);
	}
}


DictionaryEArrayStruct *resDictGetEArrayStruct(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary;

	pDictionary = resGetDictionary(dictHandle);

	if (!pDictionary)
	{
		return NULL;
	}

	return pDictionary->pEArrayStruct;
}

void resDictSetDeprecatedName(DictionaryHandleOrName dictHandle, const char *pDeprecatedName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assertmsg(pDictionary, "Can't set deprecated name for invalid dictionary");

	if (pDeprecatedName)
	{
		pDictionary->pDeprecatedName = allocAddString(pDeprecatedName);
	}
}

void resDictSetParseName(DictionaryHandleOrName dictHandle, const char *pParseName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assertmsg(pDictionary, "Can't set parse name for invalid dictionary");

	if (pParseName)
	{
		pDictionary->pParseName = allocAddString(pParseName);
	}
}

void resDictSetDisplayName(DictionaryHandleOrName dictHandle, const char *pDisplayName, const char *pPluralDisplayName, const char *pCategoryName)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	assertmsg(pDictionary, "Can't set display name for invalid dictionary");

	if (pDisplayName)
	{
		assertmsg(!pDictionary->pItemDisplayName,"Can't set display name for already set dictionary");
		pDictionary->pItemDisplayName = strdup(pDisplayName);
	}
	if (pPluralDisplayName)
	{
		assertmsg(!pDictionary->pItemDisplayNamePlural,"Can't set display name for already set dictionary");
		pDictionary->pItemDisplayNamePlural = strdup(pPluralDisplayName);
	}
	if (pCategoryName)
	{
		pDictionary->pDictCategoryName = allocAddString(pCategoryName);
		pDictionary->pDictInfo->pDictCategoryName = pDictionary->pDictCategoryName;
	}

}

// Returns one of the display names. If none are registered, display the dictionary name
const char *resDictGetItemDisplayName(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (!pDictionary)
	{
		return NULL;
	}
	if (pDictionary->pItemDisplayName)
	{
		return pDictionary->pItemDisplayName;
	}
	return pDictionary->pDictName;
}
const char *resDictGetPluralDisplayName(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDictionary = resGetDictionary(dictHandle);
	if (!pDictionary)
	{
		return NULL;
	}
	if (pDictionary->pItemDisplayNamePlural)
	{
		return pDictionary->pItemDisplayNamePlural;
	}
	if (pDictionary->pItemDisplayName)
	{
		return pDictionary->pItemDisplayName;
	}
	return pDictionary->pDictName;
}


ResourceInfo *resGetInfo(DictionaryHandleOrName pDictName, const char *resourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	if (!pDictInfo)
	{
		return NULL;
	}
	return eaIndexedGetUsingString(&pDictInfo->ppInfos, resourceName);
}

int resGetNumberOfInfos(DictionaryHandleOrName pDictName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	if (!pDictInfo)
	{
		return 0;
	}
	return eaSize(&pDictInfo->ppInfos);
}

int resGetNumberOfInfosEvenPacked(DictionaryHandleOrName dictHandle)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(dictHandle))
	{
		return stashGetCount(pDict->resourceStatusTable);
	}
	return 0;
}


ResourceInfo *resGetInfoFromDictInfo(ResourceDictionaryInfo *pDictInfo, const char *resourceName)
{
	ResourceInfo *oldInfo = eaIndexedGetUsingString(&pDictInfo->ppInfos, resourceName);
	if (oldInfo)
	{
		return oldInfo;
	}
	return NULL;
}

ResourceInfo *resGetOrCreateInfo(ResourceDictionaryInfo *pDictInfo, const char *resourceName)
{
	ResourceInfo *oldInfo = eaIndexedGetUsingString(&pDictInfo->ppInfos, resourceName);
	if (oldInfo)
	{
		return oldInfo;
	}
	oldInfo = StructCreate(parse_ResourceInfo);
	oldInfo->resourceDict = pDictInfo->pDictName;
	oldInfo->resourceName = allocAddString(resourceName);
	eaPush(&pDictInfo->ppInfos, oldInfo);
	return oldInfo;
}



ResourceInfo *resGetInfoFromHolder(ResourceInfoHolder *pHolder, const char *pDictName, const char *resourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	const char *sharedDictName = pDictInfo->pDictName;
	int i;
	for (i = 0; i < eaSize(&pHolder->ppInfos); i++)
	{
		ResourceInfo *objectInfo = eaGet(&pHolder->ppInfos, i);
		if (sharedDictName == objectInfo->resourceDict &&
			stricmp(resourceName, objectInfo->resourceName) == 0)			
		{

			// Already here
			return objectInfo;
		}
	}
	return NULL;
}

ResourceInfo *resGetOrCreateInfoFromHolder(ResourceInfoHolder *pHolder, const char *pDictName, const char *resourceName)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(pDictName);
	ResourceInfo *pInfo = resGetInfoFromHolder(pHolder, pDictName, resourceName);
	if (pInfo)
	{
		return pInfo;
	}
	pInfo = StructCreate(parse_ResourceInfo);
	pInfo->resourceDict = pDictInfo->pDictName;
	pInfo->resourceName = allocAddString(resourceName);
	eaPush(&pHolder->ppInfos, pInfo);
	return pInfo;
}

ResourceReference *resInfoGetOrCreateReference(ResourceInfo *resInfo, const char *pDictName, const char *pResourceName, const char *path, ResourceReferenceType type, const char *errorString)
{
	ResourceReference *oldRef = NULL;
	int i;
	const char *pCachedDictName = allocFindString(pDictName);
	const char *pCachedResourceName = allocFindString(pResourceName);
	if (pCachedDictName && pCachedResourceName)
	{
		for (i = 0; i < eaSize(&resInfo->ppReferences); i++)
		{
			ResourceReference *pRef = resInfo->ppReferences[i];
			if (pCachedDictName == pRef->resourceDict && pCachedResourceName == pRef->resourceName && type == pRef->referenceType &&
				stricmp(path?path:"",pRef->referencePath?pRef->referencePath:"") == 0)
			{
				oldRef = pRef;
				break;
			}
		}
	}

	if (oldRef)
	{
		return oldRef;
	}
	oldRef = StructCreate(parse_ResourceReference);
	oldRef->referencePath = path?strdup(path):NULL;
	if(errorString){
		ResourceDictionary *pDictionary = resGetDictionary(pDictName);
		
		oldRef->errorString = strdup(errorString);
		pDictionary->bHasErrorToValidate = 1;
	}
	oldRef->resourceDict = allocAddString(pDictName);
	oldRef->resourceName = allocAddString(pResourceName);
	oldRef->referenceType = type;
	eaPush(&resInfo->ppReferences, oldRef);
	return oldRef;
}


void resRemoveInfo(ResourceDictionaryInfo *pDictInfo, const char *resourceName)
{
	int index = eaIndexedFindUsingString(&pDictInfo->ppInfos, resourceName);

	if (index >= 0)
	{
		ResourceInfo *toDelete = pDictInfo->ppInfos[index];
		eaRemove(&pDictInfo->ppInfos, index);
		StructDestroy(parse_ResourceInfo, toDelete);
	}
}

void resHandleChangedInfo(DictionaryHandleOrName pDictName, const char *pResourceName, bool bNoLocation)
{
	ResourceDictionary *pDictionary = resGetDictionary(pDictName);
	ResourceInfo *pNewInfo = resGetInfo(pDictName, pResourceName);
	if (!pDictionary)
	{
		return;
	}
	if (pNewInfo)
	{	
		if (areEditorsAllowed())
		{
			int i;
			for (i = 0; i < eaSize(&pNewInfo->ppReferences); i++)
			{
				resDictDependOnOtherDict(pDictionary, pNewInfo->ppReferences[i]->resourceDict, pNewInfo->ppReferences[i]->resourceName);
			}

			if(pNewInfo->resourceLocation && strcmp(NO_FILE, pNewInfo->resourceLocation) != 0 && gResourceBackend.CheckWritableCB)
			{
				gResourceBackend.CheckWritableCB(pDictionary, pNewInfo, bNoLocation, false);
			}
		}
	}
	resDictRunEventCallbacks(pDictionary, RESEVENT_INDEX_MODIFIED, pResourceName, NULL);
	resServerUpdateModifiedResourceInfoOnAllClients(pDictionary, pResourceName);
}

void resUpdateInfo(DictionaryHandleOrName pDictName, const char *pResourceName, ParseTable *pParseTable, void *pObject,
				   const char *displayNamePath, const char *scopePath, const char *tagPath, const char *notesPath, const char *iconPath, bool bFindDependencies, bool bMaintainIDs)
{
	bool bPending = false;
	ResourceDictionary *pDictionary = resGetDictionary(pDictName);
	ResourceDictionaryInfo *pDictInfo = resEditGetPendingInfo(pDictName);

	if (!pDictionary)
	{
		return;
	}

	if (pDictInfo)
	{
		bPending = true;
	}
	else
	{
		pDictInfo = resDictGetInfo(pDictName);
	}

	if (pObject)
	{
		ResourceInfo *pNewInfo;
		char *temp = NULL;
		estrStackCreate(&temp);

		pNewInfo = resGetOrCreateInfo(pDictInfo, pResourceName);

		if (isSharedMemory(pNewInfo))
		{
			// To avoid crashes when it frees the strings below
			resRemoveInfo(pDictInfo, pResourceName);
			pNewInfo = resGetOrCreateInfo(pDictInfo, pResourceName);
		}

		if (!pDictInfo->bNoLocation)
		{
			pNewInfo->resourceLocation = allocAddString(ParserGetFilename(pParseTable, pObject));
		}

		estrClear(&temp);
		if (objPathGetEString(displayNamePath, pParseTable, pObject, &temp))
		{
			// For now just give us the english display message
			const char *tempMsg = langTranslateMessageKeyDefault(getCurrentLocale(), temp, temp);
			pNewInfo->resourceDisplayName = ststrdup_ifdiff(tempMsg, pNewInfo->resourceDisplayName, pDictionary);
		}
		estrClear(&temp);
		if (objPathGetEString(scopePath, pParseTable, pObject, &temp))
		{
			pNewInfo->resourceScope = allocAddString(temp);
		}
		estrClear(&temp);
		if (objPathGetEString(tagPath, pParseTable, pObject, &temp))
		{
			pNewInfo->resourceTags = allocAddString(temp);
		}
		estrClear(&temp);
		if (objPathGetEString(notesPath, pParseTable, pObject, &temp))
		{
			pNewInfo->resourceNotes = ststrdup_ifdiff(temp, pNewInfo->resourceNotes, pDictionary);
		}
		estrClear(&temp);
		if (objPathGetEString(iconPath, pParseTable, pObject, &temp))
		{
			pNewInfo->resourceIcon = allocAddString(temp);
		}

		if (bFindDependencies)
		{			
			ParserFindDependencies(pParseTable, pObject, pNewInfo, NULL, false, false);			
		}

		if (bMaintainIDs)
		{
			sscanf(pNewInfo->resourceName, "%d", &pNewInfo->resourceID);
		}

		estrDestroy(&temp);
	}
	else
	{
		resRemoveInfo(pDictInfo, pResourceName);
	}

	if (!bPending)
	{	
		resHandleChangedInfo(pDictName, pResourceName, false);
	}
}


bool resFindDependencies(DictionaryHandleOrName pDictName, const char *itemName, ResourceInfoHolder *pHolder)
{
	bool result;
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		ResourceInfo *globalInfo = resGetInfo(pDictName, itemName);
		ResourceInfo *newInfo = resGetInfoFromHolder(pHolder, pDictName, itemName);
		void *pObject = resGetObjectFromDict(pDict, itemName);
		int i;

		if (!globalInfo)
		{
			return false;
		}
		
		if (!newInfo)
		{
			newInfo = resGetOrCreateInfoFromHolder(pHolder, pDictName, itemName);
			if (globalInfo)
			{
				StructCopyFields(parse_ResourceInfo, globalInfo, newInfo, 0, 0);
			}
			else
			{
				const char *tempFileName;
				char fileName[MAX_PATH];
				tempFileName = resGetLocation(pDictName, itemName);
				if (tempFileName)
				{
					fileLocateWrite(tempFileName, fileName);
					newInfo->resourceLocation = allocAddString(tempFileName);
				}
			}
		}
		if (pDict->pFindDependenciesCB)
		{
			result = pDict->pFindDependenciesCB(pDict->pDictName, itemName, newInfo, pDict->pUserData);
		}
		else if (pObject && pDict->pDictTable)
		{	
			result = ParserFindDependencies(pDict->pDictTable, pObject, newInfo, NULL, false, true);
		}
		else
		{
			result = false;
		}

		if (!result)
		{
			return false;
		}
		for (i = 0; i < eaSize(&newInfo->ppReferences); i++)
		{
			ResourceReference *pRef = newInfo->ppReferences[i];
			resFindDependencies(pRef->resourceDict, pRef->resourceName, pHolder);
		}
		return true;
	}
	return false;
}

bool resFindReferencesToResource(DictionaryHandleOrName dictHandle, const char *itemName, ResourceInfoHolder *pHolder)
{
	const char *pSearchName = allocFindString(itemName);
	int i,j;
	const ResourceDictionary *pDict = resGetDictionary(dictHandle);
	const ResourceDictionary *pSearchDict = pDict;

	bool bFound = false;

	if (!pSearchName || !pDict)
	{
		// Nothing found
		return false;
	}
	
	if (pSearchDict->pDictInfo)
	{
		for (j = 0; j < eaSize(&pSearchDict->pDictInfo->ppInfos); j++)
		{
			int k;
			ResourceInfo *pInfo = pSearchDict->pDictInfo->ppInfos[j];

			for (k = 0; k < eaSize(&pInfo->ppReferences); k++)
			{
				ResourceReference *pRef = pInfo->ppReferences[k];
				if (pRef->resourceDict == pDict->pDictName && pRef->resourceName == pSearchName)
				{
					bFound = true;
					eaPush(&pHolder->ppInfos, pInfo);
				}
			}
		}
	}

	for (i = 0; i < eaSize(&pDict->ppDictsDependingOnMe); i++)
	{
		pSearchDict = resGetDictionary(pDict->ppDictsDependingOnMe[i]);
		if (pSearchDict == pDict)
		{
			continue; // Already searched this one
		}

		if (pSearchDict->pDictInfo)
		{
			for (j = 0; j < eaSize(&pSearchDict->pDictInfo->ppInfos); j++)
			{
				int k;
				ResourceInfo *pInfo = pSearchDict->pDictInfo->ppInfos[j];

				for (k = 0; k < eaSize(&pInfo->ppReferences); k++)
				{
					ResourceReference *pRef = pInfo->ppReferences[k];
					if (pRef->resourceDict == pDict->pDictName && pRef->resourceName == pSearchName)
					{
						bFound = true;
						eaPush(&pHolder->ppInfos, pInfo);
					}
				}
			}
		}
	}
	
	return bFound;
}


const char *resGetLocation(DictionaryHandleOrName pDictName, const char *itemName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(pDictName))
	{
		if (pDict->pGetLocationCB)
		{
			return pDict->pGetLocationCB(pDict->pDictName, itemName, pDict->pUserData);
		}
		else if (pDict->pDictTable)
		{
			void *pObject = resGetObjectFromDict(pDict, itemName);
			return ParserGetFilename(pDict->pDictTable, pObject);
		}
	}
	return NULL;
}


void resGetVerboseObjectName(DictionaryHandleOrName dictHandle, const char *itemName, char **ppOutName)
{
	ResourceDictionary *pDict;
	if (pDict = resGetDictionary(dictHandle))
	{
		if (pDict->pGetVerboseNameCB)
		{
			void *pObject = resGetObjectFromDict(pDict, itemName);
			if (pObject)
			{
				pDict->pGetVerboseNameCB(pDict->pDictName, pObject, pDict->pUserData, ppOutName);
			}
		}
		else
		{
			estrPrintf(ppOutName, "%s", itemName);
		}
	}
}

bool resInitIterator(DictionaryHandleOrName pDictName, ResourceIterator *pIterator)
{
	if ((pIterator->pDict = resGetDictionary(pDictName)))
	{
		if (pIterator->pDict->pInitIteratorCB && pIterator->pDict->pGetNextCB)
		{
			return pIterator->pDict->pInitIteratorCB(pIterator->pDict->pDictName, pIterator, pIterator->pDict->pUserData);
		}
	}
	return false;
}

bool resIteratorGetNext(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj)
{
	ResourceDictionary *pDict;
	if (pIterator && (pDict = pIterator->pDict))
	{
		bool bRetVal = pDict->pGetNextCB(pIterator, ppOutName, ppOutObj, pDict->pUserData);
		
		if ((pDict->eFlags & RESDICTFLAG_USE_FIXUPTYPE_GOTTEN_FROM_RES_DICT) && bRetVal && ppOutObj && *ppOutObj)
		{
			FixupStructLeafFirst(pDict->pDictTable, *ppOutObj, FIXUPTYPE_GOTTEN_FROM_RES_DICT, NULL);
		}

		return bRetVal;
	}
	return false;
}

void resFreeIterator(ResourceIterator *pIterator)
{
	ResourceDictionary *pDict;
	if(pIterator && (pDict = pIterator->pDict) && pDict->pFreeIteratorCB)
	{
		pDict->pFreeIteratorCB(pIterator);
	}
}

bool resIteratorGetNextForNamespace(ResourceIterator *pIterator, const char* strNamespace, const char **ppOutName, void **ppOutObj)
{
	const char* outName = NULL;
	
	while( resIteratorGetNext( pIterator, &outName, ppOutObj )) {
		char nsBuffer[ RESOURCE_NAME_MAX_SIZE ];
		char nameBuffer[ RESOURCE_NAME_MAX_SIZE ];

		if( strNamespace ) {
			if( !resExtractNameSpace( outName, nsBuffer, nameBuffer )) {
				continue;
			}
			if( stricmp( nsBuffer, strNamespace ) != 0 ) {
				continue;
			}
		}

		if( ppOutName ) {
			*ppOutName = outName;
		}
		return true;
	}

	return false;
}

void *resGetObject_Ref(const char *pDictName, const char *itemName, void *pUserData)
{
	return RefSystem_ReferentFromString(pDictName, itemName);
}

ReferentPrevious *resGetObjectPrevious_Ref(const char *pDictName, const char *itemName, void *pUserData)
{
	return RefSystem_ReferentPreviousFromString(pDictName, itemName);
}

void resQueueCopyObjectToPrevious_Ref(const char *pDictName, const char *itemName, void *pUserData)
{
	RefSystem_QueueCopyReferentToPrevious(pDictName, itemName);
}

int resGetNumItems_Ref(DictionaryHandleOrName pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return RefSystem_GetDictionaryNumberOfReferents(pDictName);
}

bool resInitIterator_Ref(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	RefSystem_InitRefDictIterator(pDictName, &pIterator->refIter);
	return true;
}

bool resGetNext_Ref(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	char *pRefData = NULL;
	void *pReferent = NULL;

	RefSystem_GetNextReferentAndRefDataFromIterator(&pIterator->refIter, &pReferent, &pRefData);

	while ((pReferent || pRefData) && !(pReferent && pRefData))
		RefSystem_GetNextReferentAndRefDataFromIterator(&pIterator->refIter, &pReferent, &pRefData);

	if (pReferent)
	{
		if (ppOutName)
		{
			*ppOutName = pRefData;
		}
		if (ppOutObj)
		{
			*ppOutObj = pReferent;
		}


	

		return true;
	}

	return false;
}

bool resAddObject_Ref(ResourceDictionary* pResDict, const char *itemName, void *pObject, void *pOldObject, void *pUserData)
{
	if (pOldObject)
	{
		ResourceStatus *pStatus = resGetStatus(pResDict, itemName);
		RefSystem_MoveReferent(pObject, pOldObject);
		if (!pResDict->bDictionaryBeingModified && !(pStatus && pStatus->bIsBeingLoaded))
		{
			StructDestroyVoid(pResDict->pDictTable, pOldObject);
		}
	}
	else
	{	
		RefSystem_AddReferent(pResDict->pRefDictHandle, itemName, pObject);
	}
	return true;
}

bool resRemoveObject_Ref(ResourceDictionary* pResDict, const char *itemName, void *pObject, void *pUserData)
{
	RefSystem_RemoveReferent(pObject, false);
	return true;
}

void resRegisterDictionaryForRefDict_dbg(const char *pDictName MEM_DBG_PARMS)
{
	ResourceDictionary *pDictionary;
	ParseTable *pTable = RefSystem_GetDictionaryParseTable(pDictName);
	resRegisterDictionary_dbg(pDictName, RESCATEGORY_REFDICT, 0, pTable,
		resGetObject_Ref, resGetObjectPrevious_Ref, resQueueCopyObjectToPrevious_Ref,
		resGetNumItems_Ref,
		NULL, NULL,
		resInitIterator_Ref, resGetNext_Ref, NULL,
		resAddObject_Ref, resRemoveObject_Ref, 
		NULL, NULL,
		RefSystem_GetDictionaryDeprecatedName(pDictName), NULL
		MEM_DBG_PARMS_CALL);
	pDictionary = resGetDictionary(pDictName);
	pDictionary->pDictInfo->bBrowsable = true;
	pDictionary->pRefDictHandle = RefSystem_GetDictionaryHandleFromNameOrHandle(pDictName);
	RefSystem_SetResourceDictOnRefDict(pDictName,pDictionary);
}


bool resInitIterator_Container(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	GlobalType type = NameToGlobalType(pDictName);
	objInitContainerIteratorFromType(type, &pIterator->containerIter);
	return true;
}

bool resGetNext_Container(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	static char intString[128];
	Container *con = objGetNextContainerFromIterator(&pIterator->containerIter);

	if (con) 
	{
		if (ppOutObj)
		{
			*ppOutObj = con->containerData;	
		}

		if (ppOutName)
		{
			sprintf(intString, "%u", con->containerID);
			*ppOutName = intString;
		}

	

		return true;
	}
	return false;
}

bool resFreeIterator_Container(ResourceIterator *pIterator)
{
	objClearContainerIterator(&pIterator->containerIter);
	return true;
}

bool resGetGlobalTypeAndID_Container(const char *pDictName, const char *itemName, GlobalType *pOutType, ContainerID *pOutID, void *pUserData)
{
	int containerID = atoi(itemName);
	GlobalType type = NameToGlobalType(pDictName);
	if (type)
	{	
		Container *con = objGetContainerEx(type, containerID, false, false, true);
		if (con)
		{
			*pOutType = con->containerType;
			*pOutID = con->containerID;
			objUnlockContainer(&con);
			return true;
		}
	}

	return false;
}

void *resGetObject_Container(const char *pDictName, const char *itemName, void *pUserData)
{
	int containerID = atoi(itemName);
	GlobalType type = NameToGlobalType(pDictName);
	if (type)
	{	
		Container *con = objGetContainer(type, containerID);
		if (con)
		{
			return con->containerData;
		}
	}
	return NULL;
}

int resDictGetNumberOfObjects_Container(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	GlobalType type = NameToGlobalType(pDictName);
	return objCountTotalContainersWithType(type);
}

const char *resGetLocation_Container(const char *pDictName, const char *itemName, void *pUserData)
{
	static char locationString[256];
	int containerID = atoi(itemName);
	GlobalType type = NameToGlobalType(pDictName);
	if (type)
	{	
		Container *con = objGetContainerEx(type, containerID, false, false, true);
		if (con)
		{
			sprintf(locationString, "%s[%u]", GlobalTypeToName(con->meta.containerOwnerType), con->meta.containerOwnerID);
			objUnlockContainer(&con);
			return locationString;
		}
	}
	return NULL;

}

void resRegisterDictionaryForContainerType_dbg(GlobalType containerType MEM_DBG_PARMS)
{
	ContainerSchema *pSchema;
	ResourceDictionaryInfo *pInfo;
	char *pDictName = GlobalTypeToName(containerType);
	pSchema = objFindContainerSchema(containerType);

	resRegisterDictionary_dbg(pDictName, RESCATEGORY_CONTAINER, 0, pSchema->classParse,
		resGetObject_Container, NULL, NULL, resDictGetNumberOfObjects_Container,
		resGetLocation_Container, NULL,
		resInitIterator_Container, resGetNext_Container, resFreeIterator_Container,
		NULL, NULL, 
		resGetGlobalTypeAndID_Container,
		NULL, NULL, NULL
		MEM_DBG_PARMS_CALL);
	pInfo = resDictGetInfo(pDictName);
	pInfo->bBrowsable = true;
}


void *resGetObject_StashTable(const char *pDictName, const char *itemName, void *pUserData)
{
	void *pObj;
	StashTable sTable = (StashTable)pUserData;

	switch(stashTableGetKeyType(sTable))
	{	
		xcase StashKeyTypeStrings:
			if (stashFindPointer((StashTable)pUserData, itemName, &pObj))
			{
				return pObj;
			}
			return NULL;

		xcase StashKeyTypeInts:
			{
				U32 iID;

				if (!StringToUint_Paranoid(itemName, &iID))
				{
					return NULL;
				}

				if (stashIntFindPointer((StashTable)pUserData, iID, &pObj))
				{
					return pObj;
				}

				return NULL;
			}
		xdefault:
		if (stashFindPointer((StashTable)pUserData, allocAddString(itemName), &pObj))
		{
			return pObj;
		}
	}

	return NULL;
}

int resDictGetNumberOfObjects_StashTable(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	return stashGetCount((StashTable)pUserData);
}
bool resInitIterator_StashTable(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	stashGetIterator((StashTable)pUserData, &pIterator->stashIter);
	return true;
}
bool resGetNext_StashTable(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	StashElement element;
	static char tempRet[16];
	if (stashGetNextElement(&pIterator->stashIter, &element))
	{
		if (ppOutObj)
			*ppOutObj = stashElementGetPointer(element);
		switch (stashTableGetKeyType(pIterator->stashIter.pTable))
		{
		case StashKeyTypeInts:
		if (ppOutName)
		{
			sprintf(tempRet, "%u", stashElementGetIntKey(element));
			*ppOutName = tempRet;
		}
		return true;

		 default:
			if (ppOutName)
				*ppOutName = stashElementGetStringKey(element);
			return true;
		}
	
		return true;

	}
	else
	{
		return false;
	}
}
bool resAddObject_StashTable(ResourceDictionary *pResDict, const char *itemName, void *pObject, void *pOldObject, void *pUserData)
{
	if (stashRemovePointer((StashTable)pUserData, itemName, &pOldObject))
	{
		resNotifyObjectDestroyed(pResDict, itemName, pOldObject, __FUNCTION__);
	}
	if (stashAddPointer((StashTable)pUserData, itemName, pObject, false))
	{	
		resNotifyObjectCreated(pResDict, itemName, pObject, __FUNCTION__);
		return true;
	}
	return false;
}

bool resRemoveObject_StashTable(ResourceDictionary* pResDict, const char *itemName, void *pObject, void *pUserData)
{
	if (stashRemovePointer((StashTable)pUserData, itemName, &pObject))
	{
		resNotifyObjectDestroyed(pResDict, itemName, pObject, __FUNCTION__);
		return true;
	}
	return false;
}


void resRegisterDictionaryForStashTable_dbg(const char *pDictName, const char *pRefCategoryName, enumResDictFlags eFlags, StashTable table, ParseTable *pTPI MEM_DBG_PARMS)
{
	resRegisterDictionary_dbg(pDictName, pRefCategoryName, eFlags, pTPI, 
		resGetObject_StashTable, NULL, NULL, resDictGetNumberOfObjects_StashTable,
		NULL, NULL, 
		resInitIterator_StashTable, resGetNext_StashTable, NULL,
		resAddObject_StashTable, resRemoveObject_StashTable, NULL, NULL, NULL, table
		MEM_DBG_PARMS_CALL);
}



void *resGetObject_EArray(const char *pDictName, const char *itemName, void *pUserData)
{
	int i;

	void ***pppEArray = pUserData;
	static char *pKeyString = NULL;
	ParseTable *pTPI = resDictGetParseTable(pDictName);

	assert(pTPI);

	for (i=0; i < eaSize((void***)pUserData); i++)
	{
		if (eaGet(pppEArray, i))
		{
			estrClear(&pKeyString);
			objGetKeyEString(pTPI, (*pppEArray)[i], &pKeyString);
			if (stricmp(pKeyString, itemName) == 0)
			{
				return (*pppEArray)[i];
			}
		}
	}


	return NULL;
}

int resDictGetNumberOfObjects_EArray(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	void ***pppEArray = pUserData;
	int i;

	if (eFlags & REDICTFLAG_SPARSE_EARRAY)
	{
		int iCount = 0;

		for (i=0; i < eaSize(pppEArray); i++)
		{
			if (eaGet(pppEArray, i))
			{
				iCount++;
			}
		}


		return iCount;
	}
	else
	{
		return eaSize(pppEArray);
	}
}
bool resInitIterator_EArray(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	pIterator->index = 0;
	return true;
}
bool resGetNext_EArray(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	void ***pppEArray = pUserData;
	static char *pKeyString = NULL;

	assert(pppEArray);

	while (pIterator->index < eaSize(pppEArray) && !eaGet(pppEArray, pIterator->index))
	{
		pIterator->index++;
	}

	if (pIterator->index >= eaSize(pppEArray))
	{
		return false;
	}

	assert(*pppEArray);


	if (ppOutName)
	{
		estrClear(&pKeyString);
		objGetKeyEString(pIterator->pDict->pDictTable, (*pppEArray)[pIterator->index], &pKeyString);
		*ppOutName = pKeyString;
	}

	if (ppOutObj)
	{
		*ppOutObj = (*pppEArray)[pIterator->index];
	}

	pIterator->index++;

	return true;
}



void resRegisterDictionaryForEArray_dbg(const char *pDictName, const char *pRefCategoryName, enumResDictFlags eFlags, void ***pppEArray, ParseTable *pTPI MEM_DBG_PARMS)
{
	resRegisterDictionary_dbg(pDictName, pRefCategoryName, eFlags, pTPI, 
		resGetObject_EArray, NULL, NULL, resDictGetNumberOfObjects_EArray,
		NULL, NULL, 
		resInitIterator_EArray, resGetNext_EArray, NULL,
		NULL, NULL, NULL, NULL, NULL, pppEArray
		MEM_DBG_PARMS_CALL);
}


void *resGetObject_IndexOnly(const char *pDictName, const char *itemName, void *pUserData)
{
	return resGetInfo(pDictName, itemName);
}

int resDictGetNumberOfObjects_IndexOnly(const char *pDictName, void *pUserData, enumResDictFlags eFlags)
{
	ResourceDictionaryInfo *dictInfo = resDictGetInfo(pDictName);
	if (!dictInfo)
	{
		return 0;
	}
	return eaSize(&dictInfo->ppInfos);
}
bool resInitIterator_IndexOnly(const char *pDictName, ResourceIterator *pIterator, void *pUserData)
{
	ResourceDictionary *pDict = resGetDictionary(pDictName);
	ResourceDictionaryInfo *dictInfo = resDictGetInfo(pDictName);
	if (!dictInfo || !pDict)
	{
		return false;
	}
	pIterator->pDict = pDict;
	pIterator->index = 0;
	return true;
}
bool resGetNext_IndexOnly(ResourceIterator *pIterator, const char **ppOutName, void **ppOutObj, void *pUserData)
{
	if (!pIterator || !pIterator->pDict || !pIterator->pDict->pDictInfo)
	{
		return false;
	}
	if (pIterator->index < eaSize(&pIterator->pDict->pDictInfo->ppInfos))
	{
		ResourceInfo *pInfo = pIterator->pDict->pDictInfo->ppInfos[pIterator->index++];
		if (ppOutObj)
			*ppOutObj = pInfo;
		if (ppOutName)
			*ppOutName = (char *)pInfo->resourceName;
		return true;
	}
	return false;
}

void resRegisterIndexOnlyDictionary_dbg(const char *pDictName, const char *pCategoryName MEM_DBG_PARMS)
{
	ResourceDictionaryInfo *pInfo;

	resRegisterDictionary_dbg(pDictName, pCategoryName, 0, parse_ResourceInfo, 
		resGetObject_IndexOnly, NULL, NULL, resDictGetNumberOfObjects_IndexOnly,
		NULL, NULL, 
		resInitIterator_IndexOnly, resGetNext_IndexOnly, NULL, 
		NULL, NULL, NULL, NULL, NULL, NULL
		MEM_DBG_PARMS_CALL);

	pInfo = resDictGetInfo(pDictName);
	pInfo->bNoLocation = true;

	if (isDevelopmentMode() || isProductionEditMode())
	{
		resDictMaintainInfoIndex(pDictName, NULL, NULL, NULL, NULL, NULL);
	}
}



static int ResourceGroupCompare(const ResourceGroup **left, const ResourceGroup **right)
{
	return stricmp((*left)->pchName, (*right)->pchName);
}


static void resSortGroupTree(ResourceGroup *pGroup, resComparatorFunc *fpComparator)
{
	int i;

	for(i=eaSize(&pGroup->ppGroups)-1; i>=0; --i)
		resSortGroupTree(pGroup->ppGroups[i], fpComparator);

	eaQSort(pGroup->ppGroups, ResourceGroupCompare);

	if (fpComparator)
		eaQSort(pGroup->ppInfos, fpComparator);
}


static void resFreeGroupTree(ResourceGroup *pGroup)
{
	int i;
	for(i=eaSize(&pGroup->ppGroups)-1; i>=0; --i)
	{
		resFreeGroupTree(pGroup->ppGroups[i]);
		StructDestroy(parse_ResourceGroup, pGroup->ppGroups[i]);
	}
	StructFreeString(pGroup->pchName);
	pGroup->pchName = NULL;
	eaDestroy(&pGroup->ppGroups);
	eaDestroy(&pGroup->ppInfos);
}


static void resAddTreeEntry(ResourceGroup *pTop, ResourceInfo *pInfo)
{
	ResourceGroup ***peaGroups;
	ResourceGroup *pGroup;
	const char *pcPath = pInfo->resourceScope;
	char *pos;
	char buf[260];
	int i;
	bool bDone = false;

	pGroup = NULL;
	peaGroups = &pTop->ppGroups;
	if (!pcPath)
		bDone = true;

	while (!bDone) {
		bool bFound = false;

		// Get out group name
		pos = strchr(pcPath, '/');
		if (pos) {
			assert(pos > pcPath);
			strncpy(buf, pcPath, pos-pcPath);
			buf[pos-pcPath] = '\0';
			pcPath = pos + 1;
			if (buf[0] == '\0')
				strcpy(buf, "_Empty_");
		} else {
			bDone = true;
			if (pGroup && (pcPath[0] == '\0'))
				strcpy(buf, "_Empty_");
			else
				strcpy(buf, pcPath);
		}

		if (buf[0]) {
			// Look for matching group
			for(i=eaSize(peaGroups)-1; i>=0; --i) {
				if (stricmp(buf,(*peaGroups)[i]->pchName) == 0) {
					bFound = true;
					pGroup = (*peaGroups)[i];
					peaGroups = &pGroup->ppGroups;
					break;
				}
			}
			if (!bFound) {
				pGroup = StructCreate(parse_ResourceGroup);
				pGroup->pchName = StructAllocString(buf);
				eaPush(peaGroups, pGroup);
				peaGroups = &pGroup->ppGroups;
			}
		}
	}

	if (pGroup) {
		eaPush(&pGroup->ppInfos, pInfo);
	} else {
		eaPush(&pTop->ppInfos, pInfo);
	}
}

void resBuildGroupTreeEx(DictionaryHandleOrName dictHandle, ResourceGroup *pTop, resComparatorFunc *fpComparator)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(dictHandle);
	int i, count;

	// Free up the group tree
	resFreeGroupTree(pTop);

	// Add the infos
	count = eaSize(&pDictInfo->ppInfos);
	for( i = 0; i<count; ++i)
		resAddTreeEntry(pTop, pDictInfo->ppInfos[i]);

	// Sort the groups
	resSortGroupTree(pTop, fpComparator);
}

void resBuildFilteredGroupTree(DictionaryHandleOrName dictHandle, ResourceGroup *pTop, resCallback_GroupFilter *pCB, void *pUserData)
{
	ResourceDictionaryInfo *pDictInfo = resDictGetInfo(dictHandle);
	int i, count;

	// Free up the group tree
	resFreeGroupTree(pTop);

	// Add the infos
	count = eaSize(&pDictInfo->ppInfos);
	for( i = 0; i<count; ++i)
	{
		ResourceInfo *pInfo = pDictInfo->ppInfos[i];
		if (!pCB || pCB(pInfo, pUserData))
			resAddTreeEntry(pTop, pInfo);
	}

	// Sort the groups
	resSortGroupTree(pTop, NULL);
}

// Resource NameSpaces

StashTable gResourceNameSpaces;
static ResourceNameSpace *g_default_ns = 0;
ResourceNameSpace *resNameSpaceGetOrCreate(const char *spaceName)
{
	ResourceNameSpace *foundSpace;
	if (!gResourceNameSpaces)
	{
		gResourceNameSpaces = stashTableCreateWithStringKeys(64, StashDefault);
	}
	if (foundSpace = resNameSpaceGetByName(spaceName))
	{
		return foundSpace;
	}
	foundSpace = StructCreate(parse_ResourceNameSpace);
	foundSpace->pName = StructAllocString(spaceName);
	stashAddPointer(gResourceNameSpaces, foundSpace->pName, foundSpace, 0);

    // ab: quick and dirty, the default namespace is the first one added
    if(!g_default_ns)
        g_default_ns = foundSpace;

	return foundSpace;
}

ResourceNameSpace *resNameSpaceGetByName(const char *spaceName)
{
	ResourceNameSpace *foundSpace;
	if (!gResourceNameSpaces)
	{
		return NULL;
	}
	if (stashFindPointer(gResourceNameSpaces, spaceName, &foundSpace))
	{
		return foundSpace;
	}
	return NULL;
}

ResourceNameSpace *resNameSpaceGetDefault(void)
{
    return g_default_ns;
}

bool resNameSpaceSetDefault(char const *spaceName)
{
    ResourceNameSpace *r = resNameSpaceGetByName(spaceName);
    if(r)
    {
        g_default_ns = r;
        return true;
    }
    return false;
}


void resNameSpaceRemove(const char *spaceName)
{
	ResourceNameSpace *foundSpace;
	if (!gResourceNameSpaces)
	{
		return;
	}
	if (stashFindPointer(gResourceNameSpaces, spaceName, &foundSpace))
	{
		StructDestroy(parse_ResourceNameSpace, foundSpace);
		stashRemovePointer(gResourceNameSpaces, spaceName, &foundSpace);
        if(foundSpace == g_default_ns)
            g_default_ns = NULL;
	}
}

void resNameSpaceInitIterator(ResourceNameSpaceIterator *pIterator)
{
	stashGetIterator(gResourceNameSpaces, pIterator);
}

ResourceNameSpace *resNameSpaceIteratorGetNext(ResourceNameSpaceIterator *pIterator)
{
	ResourceNameSpace *resDict;
	StashElement iterElement;
	if (stashGetNextElement(pIterator, &iterElement))
	{
		resDict = stashElementGetPointer(iterElement);
		if (resDict)
		{
			return resDict;
		}
	}
	return NULL;
}

bool resNameSpaceValidForCache(ResourceNameSpace *pSpace, ResourceCache *pCache)
{
	if (gConf.bUserContent)
	{
		int i;
		if (isDevelopmentMode())
			return true;
		for (i = 0; i < eaSize(&pSpace->ppWritableAccounts); i++)
		{
			if (stricmp(pSpace->ppWritableAccounts[i], pCache->userLogin) == 0)
			{
				return true;
			}
		}
	}
	// TODO: Add actual verification
	return false;
}

bool resIsInDirectory(const char *resourceName, const char *directory)
{
	char nameSpace[RESOURCE_NAME_MAX_SIZE];
	char baseObjectName[RESOURCE_NAME_MAX_SIZE];
	if (resExtractNameSpace(resourceName, nameSpace, baseObjectName))
		return !strnicmp(baseObjectName, directory, strlen(directory));
	return !strnicmp(resourceName, directory, strlen(directory));
}

bool resHasNamespace(const char *resourceName)
{
	const char *colon;
	if (!resourceName || !resourceName[0])
	{
		return false;
	}
	colon = strchr(resourceName, ':');

	if (colon && colon[1] == ':')
	{
		return false;
	}

	return ( colon != NULL );
}

bool resExtractNameSpace_s(const char *resourceName, char *nameSpace, size_t nameSpaceSize, char *baseName, size_t baseNameSize)
{
	const char *colon;
	if (nameSpace)
		nameSpace[0] = '\0';
	if (baseName)
		baseName[0] = '\0';
	if (!resourceName || !resourceName[0])
	{
		return false;
	}
	colon = strchr(resourceName, ':');

	if (colon && colon[1] == ':')
		colon = 0;
	// Special case, ignore double colons

	if (colon)
	{	
		if (nameSpace)
		{	
			strncpy_s(nameSpace, nameSpaceSize, resourceName, colon - resourceName);
		}
		if (baseName)
		{
			if (colon[1] == '/')
				strcpy_s(baseName, baseNameSize, colon + 2);
			else
				strcpy_s(baseName, baseNameSize, colon + 1);
		}
		return true;
	}
	else if (strStartsWith(resourceName, NAMESPACE_PATH))
	{
		const char *tmp, *slash;
		tmp = resourceName + strlen(NAMESPACE_PATH);
		slash = strchr(tmp, '/');
		if (nameSpace)
		{	
			strncpy_s(nameSpace, nameSpaceSize, tmp, slash - tmp);
		}
		if (baseName)
		{
			if (slash[1] == '/')
				strcpy_s(baseName, baseNameSize, slash + 2);
			else
				strcpy_s(baseName, baseNameSize, slash + 1);
		}
		return true;
	}
	else
	{
		if (nameSpace)
		{
			strcpy_s(nameSpace, nameSpaceSize, "");
		}
		if (baseName)
		{
			strcpy_s(baseName, baseNameSize, resourceName);
		}
		return false;
	}
}

bool resNamespaceBaseNameEq(const char *resourceA, const char *resourceB)
{
	const char *colonA, *colonB;
	if (!resourceA || !resourceA[0] || !resourceB || !resourceB[0])
	{
		return false;
	}

	colonA = strchr(resourceA, ':');
	if (colonA && colonA[1] == ':')
		colonA = 0;

	colonB = strchr(resourceB, ':');
	if (colonB && colonB[1] == ':')
		colonB = 0;

	return (stricmp(colonA ? &colonA[1] : resourceA, colonB ? &colonB[1] : resourceB) == 0);
}

bool resNamespaceNameEq(const char *resourceA, const char *resourceB)
{
	const char *colonA, *colonB;
	if (!resourceA || !resourceA[0] || !resourceB || !resourceB[0])
	{
		return false;
	}

	colonA = strchr(resourceA, ':');
	if (colonA && colonA[1] == ':')
		colonA = 0;

	colonB = strchr(resourceB, ':');
	if (colonB && colonB[1] == ':')
		colonB = 0;

	return (!colonA && !colonB) ||
		(colonA && colonB && strnicmp(resourceA, resourceB, colonA-resourceA) == 0);
}

int gWriteBinManifest;

AUTO_CMD_INT(gWriteBinManifest, WriteBinManifest) ACMD_COMMANDLINE;

static FileScanAction AddToVersionList(char* dir, struct _finddata32_t* data, SavedFileVersionList *pList)
{
	char buffer[512];	

	if (data->attrib & _A_SUBDIR) {
		// If it's a subdirectory, we don't care about checking times, etc
		if (data->name[0]=='_') {
			return FSA_NO_EXPLORE_DIRECTORY;
		} else {
			return FSA_EXPLORE_DIRECTORY;
		}
	}

	if(!(data->name[0] == '_') && !strEndsWith(data->name,".manifest") && !(strEndsWith(data->name,".namespace")) && !strEndsWith(data->name,".bak") )
	{
		STR_COMBINE_SSS(buffer, dir, "/", data->name);
		AddFileVersionToList(pList, buffer, data->time_write, 0, 0, 0, 0, 0);
	}
	return FSA_EXPLORE_DIRECTORY;
	
}

void resWriteNamespaceManifests(void)
{
	ResourceNameSpace *pNameSpace;
	ResourceNameSpaceIterator iterator;

	if (!gConf.bUserContent || !gWriteBinManifest)
		return;		

	resNameSpaceInitIterator(&iterator);
	while (pNameSpace = resNameSpaceIteratorGetNext(&iterator))
	{
		char versionListPath[MAX_PATH];
		SavedFileVersionList *pList = CreateFileVersionList();

		sprintf(versionListPath, NAMESPACE_PATH"%s",pNameSpace->pName);

		fileScanAllDataDirs(versionListPath, AddToVersionList, pList);

		sprintf(versionListPath, "%s:/%s.manifest", pNameSpace->pName,pNameSpace->pName);

		WriteFileVersionList(pList, versionListPath);
		DestroyFileVersionList(pList);
	}
}

#include "AutoGen/ResourceInfo_h_ast.c"
