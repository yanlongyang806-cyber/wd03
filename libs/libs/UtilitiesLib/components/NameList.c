#include "cmdparse.h"
#include "MRUList.h"
#include "resourceManager.h"
#include "fileUtil2.h"
#include "Objpath.h"
#include "TokenStore.h"
#include "StashTable.h"

StashTable sNameListsByName = NULL;
StashTable sNameListCreateCBsByName = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

void FreeNameList(NameList *pNameList)
{

	if (pNameList->pGlobalName)
	{
		assert(sNameListsByName);
		stashRemovePointer(sNameListsByName, pNameList->pGlobalName, NULL);
		free(pNameList->pGlobalName);
	}

	if (pNameList->pFreeCB)
	{
		pNameList->pFreeCB(pNameList);
	}
	else
	{
		free(pNameList);
	}
}



void NameList_AssignName(NameList *pList, char *pName)
{
	pList->pGlobalName = strdup(pName);
	if (!sNameListsByName)
	{
		sNameListsByName = stashTableCreateWithStringKeys(2, StashDefault);
	}

	assertmsgf(!stashFindPointer(sNameListsByName, pList->pGlobalName, NULL), "Duplicate namelist global names: %s", pList->pGlobalName);

	stashAddPointer(sNameListsByName, pList->pGlobalName, pList, false);
}

void NameList_RegisterGetListCallback(char *pName, NameList_GetNamedListCB *pCB)
{
	if (!sNameListCreateCBsByName)
	{
		sNameListCreateCBsByName = stashTableCreateWithStringKeys(2, StashDeepCopyKeys_NeverRelease);
	}

	assertmsgf(!stashFindPointer(sNameListCreateCBsByName, pName, NULL), "Duplicate namelist creation CB names: %s", pName);

	stashAddPointer(sNameListCreateCBsByName, pName, pCB, false);
}



//find a name list by name
NameList *NameList_FindByName(char *pName)
{
	NameList *pNameList;
	NameList_GetNamedListCB *pCB;

	if (sNameListsByName)
	{
		if (stashFindPointer(sNameListsByName, pName, &pNameList))
		{
			return pNameList;
		}
	}

	if (sNameListCreateCBsByName)
	{
		if (stashFindPointer(sNameListCreateCBsByName, pName, (void**)(&pCB)))
		{
			return pCB();
		}
	}

	
	return NULL;
}



//----------------------Stuff for NameList_CmdList
typedef struct
{
	NameList baseList;

	int iMaxAccessLevel;
	CmdList *pCmdList;
	StashTableIterator iter;
} NameList_CmdList;

const char *NameList_CmdList_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_CmdList *pList;
	StashElement element;

	PERFINFO_AUTO_START_FUNC();

	pList = (NameList_CmdList*)pBaseList;

	if (!pList->iter.pTable)
	{
		stashGetIterator(pList->pCmdList->sCmdsByName, &pList->iter);
	}

	while (stashGetNextElement(&pList->iter, &element))
	{
		Cmd *pCmd = stashElementGetPointer(element);

		if (pCmd->access_level > pList->iMaxAccessLevel || (pCmd->flags & CMDF_HIDEPRINT) && pList->iMaxAccessLevel < 9)
		{
			continue;
		}

		PERFINFO_AUTO_STOP();
		return pCmd->name;
	}

	pList->iter.pTable = NULL;
	PERFINFO_AUTO_STOP();
	return NULL;

}

void NameList_CmdList_ResetCB(NameList *pBaseList)
{
	NameList_CmdList *pList = (NameList_CmdList*)pBaseList;

	pList->iter.pTable = NULL;
}


//a list of all the commands in a CmdList
NameList *CreateNameList_CmdList(CmdList *pCmdList, int iMaxAccessLevel)
{

	NameList_CmdList *pList = (NameList_CmdList*)calloc(sizeof(NameList_CmdList), 1);

	assert(pCmdList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_CmdList_GetNextCB;
	pList->baseList.pResetCB = NameList_CmdList_ResetCB;
	
	pList->iMaxAccessLevel = iMaxAccessLevel;
	pList->pCmdList = pCmdList;

	return (NameList*)pList;
}

void NameList_CmdList_SetAccessLevel(NameList *pBaseList, int iAccessLevel)
{
	NameList_CmdList *pList = (NameList_CmdList*)pBaseList;

	pList->iMaxAccessLevel = iAccessLevel;
}

//----------------------Stuff for NameList_StructArray
typedef struct 
{
	NameList baseList;

	ParseTable *pti;
	char* strpath;

	int counter;
	NameList_StructArrayFunc getArrayFunc;
} NameList_StructArray;

char *NameList_StructArray_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_StructArray *pList = (NameList_StructArray*)pBaseList;
	void **list = pList->getArrayFunc();
	ParseTable *resolvedPTI = NULL;
	int resolvedCol;
	int resolvedIndex;
	void *ptr;
	void *resolvedPtr;
	char *result = NULL;

	ptr = eaGet(&list, pList->counter);

	if(ptr && objPathResolveField(pList->strpath, pList->pti, ptr, 
							&resolvedPTI, &resolvedCol, &resolvedPtr, &resolvedIndex, 0))
	{
		result = (char*)TokenStoreGetString(resolvedPTI, resolvedCol, resolvedPtr, resolvedIndex, NULL);
	}
	
	pList->counter++;
	return result;
}

void NameList_StructArray_ResetCB(NameList *pBaseList)
{
	NameList_StructArray *pList = (NameList_StructArray*)pBaseList;

	pList->counter = 0;
}

void NameList_StructArray_FreeCB(NameList *pBaseList)
{
	NameList_StructArray *pList = (NameList_StructArray*)pBaseList;

	free(pList->strpath);
	free(pList);
}

NameList* CreateNameList_StructArray(ParseTable *pti, const char* name, NameList_StructArrayFunc func)
{
	NameList_StructArray *pList = (NameList_StructArray*)calloc(sizeof(NameList_StructArray), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_StructArray_FreeCB;
	pList->baseList.pGetNextCB = NameList_StructArray_GetNextCB;
	pList->baseList.pResetCB = NameList_StructArray_ResetCB;

	pList->getArrayFunc = func;
	pList->strpath = strdup(name);
	pList->counter = 0;
	pList->pti = pti;

	return (NameList*)pList;
}

//----------------------Stuff for NameList_Bucket
typedef struct
{
	NameList baseList;

	char **ppNames;
	StashTable stNames;
	unsigned int iCounter;
} NameList_Bucket;

char *NameList_Bucket_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_Bucket *pList;
	unsigned int iSize;
	
	PERFINFO_AUTO_START_FUNC();

	pList = (NameList_Bucket*)pBaseList;
	iSize = eaSize(&pList->ppNames);

	if (pList->iCounter >= iSize)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return pList->ppNames[pList->iCounter++];
}

void NameList_Bucket_ResetCB(NameList *pBaseList)
{
	NameList_Bucket *pList = (NameList_Bucket*)pBaseList;

	pList->iCounter = 0;
}

void NameList_Bucket_FreeCB(NameList *pBaseList)
{
	NameList_Bucket *pList = (NameList_Bucket*)pBaseList;

	eaDestroyEx(&pList->ppNames, NULL);
	stashTableDestroySafe(&pList->stNames);

	free(pList);
}

//a list of all the commands in a Bucket
NameList *CreateNameList_Bucket(void)
{

	NameList_Bucket *pList = (NameList_Bucket*)calloc(sizeof(NameList_Bucket), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_Bucket_FreeCB;
	pList->baseList.pGetNextCB = NameList_Bucket_GetNextCB;
	pList->baseList.pResetCB = NameList_Bucket_ResetCB;
	
	pList->iCounter = 0;
	pList->ppNames = NULL;

	return (NameList*)pList;
}

void NameList_Bucket_AddName(NameList *pBaseList, const char *pName)
{
	NameList_Bucket *pList = (NameList_Bucket*)pBaseList;
	
	if(!pList->stNames)
	{
		pList->stNames = stashTableCreateWithStringKeys(100, StashDefault);
	}
	
	if(!stashFindInt(pList->stNames, pName, NULL))
	{
		char* nameCopy = strdup(pName);
		
		eaPush(&pList->ppNames, nameCopy);
		
		if(!stashAddInt(pList->stNames, nameCopy, eaSize(&pList->ppNames) - 1, false))
		{
			assert(0);
		}
	}
}

void NameList_Bucket_RemoveName(NameList *pBaseList, const char *pName)
{
	NameList_Bucket *pList = (NameList_Bucket*)pBaseList;
	int idx;
	pList->baseList.pResetCB(pBaseList);

	if(stashRemoveInt(pList->stNames, pName, &idx))
	{
		const char* movedName = NULL;
		char *removedName;

		removedName = eaRemoveFast(&pList->ppNames, idx);
		free(removedName);

		if(movedName = eaGet(&pList->ppNames, idx))
		{
			stashAddInt(pList->stNames, movedName, idx, true);
		}
	}
}

void NameList_Bucket_Clear(NameList *pBaseList)
{
	NameList_Bucket *pList = (NameList_Bucket*)pBaseList;

	pList->baseList.pResetCB(pBaseList);

	stashTableClear(pList->stNames);
	eaClearEx(&pList->ppNames, NULL);
}

//----------------------Stuff for NameList_AccessLevelBucket
typedef struct
{
	NameList baseList;

	char **ppNames;
	StashTable stNames;
	int iMaxAccessLevel;
	int *piAccessLevels;
	unsigned int iCounter;
} NameList_AccessLevelBucket;

char *NameList_AccessLevelBucket_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_AccessLevelBucket *pList;
	unsigned int iSize;

	PERFINFO_AUTO_START_FUNC();
	
	pList = (NameList_AccessLevelBucket*)pBaseList;
	iSize = eaSize(&pList->ppNames);

	while (pList->iCounter < iSize && pList->piAccessLevels[pList->iCounter] > pList->iMaxAccessLevel)
	{
		pList->iCounter++;
	}

	if (pList->iCounter >= iSize)
	{
		PERFINFO_AUTO_STOP();
		return NULL;
	}

	PERFINFO_AUTO_STOP();
	return pList->ppNames[pList->iCounter++];
}

void NameList_AccessLevelBucket_ResetCB(NameList *pBaseList)
{
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)pBaseList;

	pList->iCounter = 0;
}

void NameList_AccessLevelBucket_FreeCB(NameList *pBaseList)
{
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)pBaseList;

	eaDestroyEx(&pList->ppNames, NULL);
	eaiDestroy(&pList->piAccessLevels);
	stashTableDestroySafe(&pList->stNames);

	free(pList);
}

//a list of all the commands in a Bucket
NameList *CreateNameList_AccessLevelBucket(int iMaxAccessLevel)
{
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)calloc(sizeof(NameList_AccessLevelBucket), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_AccessLevelBucket_FreeCB;
	pList->baseList.pGetNextCB = NameList_AccessLevelBucket_GetNextCB;
	pList->baseList.pResetCB = NameList_AccessLevelBucket_ResetCB;

	pList->iCounter = 0;
	pList->ppNames = NULL;
	pList->piAccessLevels = NULL;
	pList->iMaxAccessLevel = iMaxAccessLevel;
	return (NameList*)pList;
}


void NameList_AccessLevelBucket_SetAccessLevel(NameList *pBaseList, int iAccessLevel)
{
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)pBaseList;

	pList->iMaxAccessLevel = iAccessLevel;
}

void NameList_AccessLevelBucket_AddName(NameList *pBaseList, const char *pName, int iAccessLevel)
{
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)pBaseList;

	if(!pList->stNames)
	{
		pList->stNames = stashTableCreateWithStringKeys(100, StashDefault);
	}

	if(!stashFindInt(pList->stNames, pName, NULL))
	{
		char* nameCopy = strdup(pName);
		
		eaPush(&pList->ppNames, nameCopy);
		eaiPush(&pList->piAccessLevels, iAccessLevel);
		
		if(!stashAddInt(pList->stNames, nameCopy, eaSize(&pList->ppNames) - 1, false))
		{
			assert(0);
		}
	}
}

int NameList_AccessLevelBucket_GetAccessLevel(NameList *pBaseList, const char *pName) {
	NameList_AccessLevelBucket *pList = (NameList_AccessLevelBucket*)pBaseList;

	U32 index;
	if(stashFindInt(pList->stNames, pName, &index))
	{
		assert(index < eaiUSize(&pList->piAccessLevels));
		return pList->piAccessLevels[index];
	}

	return -1;
}

//----------------------Stuff for NameList_Callbacks
typedef struct
{
	NameList baseList;
	
	NameList_GetNextDataCallback *pGetNextCB;
	NameList_DataCallback *pResetCB;
	NameList_DataCallback *pFreeCB;
	void *pData;
} NameList_Callbacks;

static const char *NameList_Callbacks_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_Callbacks *pList = (NameList_Callbacks*)pBaseList;

	if (pList->pGetNextCB)
	{
		return pList->pGetNextCB(pBaseList, pList->pData);
	}
	return NULL;
}

static void NameList_Callbacks_ResetCB(NameList *pBaseList)
{
	NameList_Callbacks *pList = (NameList_Callbacks*)pBaseList;

	if (pList->pResetCB)
	{
		pList->pResetCB(pBaseList, pList->pData);
	}
}

static void NameList_Callbacks_FreeCB(NameList *pBaseList)
{
	NameList_Callbacks *pList = (NameList_Callbacks*)pBaseList;

	if (pList->pFreeCB)
	{
		pList->pFreeCB(pBaseList, pList->pData);
	}
	free(pList);
}


//a list of all the commands in a Callbacks namelist
NameList *CreateNameList_Callbacks(NameList_GetNextDataCallback *pGetNextCB,
								   NameList_DataCallback *pResetCB,
								   NameList_DataCallback *pFreeCB,
								   void *pData)
{
	NameList_Callbacks *pList = (NameList_Callbacks*)calloc(sizeof(NameList_Callbacks), 1);

	assert(pList);

	// Set the internal callbacks
	pList->baseList.pFreeCB = NameList_Callbacks_FreeCB;
	pList->baseList.pGetNextCB = NameList_Callbacks_GetNextCB;
	pList->baseList.pResetCB = NameList_Callbacks_ResetCB;
	
	// Set the external callbacks and data
	pList->pGetNextCB = pGetNextCB;
	pList->pResetCB = pResetCB;
	pList->pFreeCB = pFreeCB;
	pList->pData = pData;
	
	return (NameList*)pList;
}

//----------------------Stuff for NameList_MultiList
typedef struct
{
	NameList baseList;

	NameList **ppLists;
	unsigned int iCounter;
} NameList_MultiList;

bool NameList_MultiList_HasEarlierDupeCB(NameList *pBaseList, const char* name)
{
	NameList_MultiList *pList = (NameList_MultiList*)pBaseList;
	unsigned int iSize = eaSize(&pList->ppLists);
	unsigned int i;

	PERFINFO_AUTO_START_FUNC();

	assert(pList->iCounter < iSize);

	for (i=0; i<pList->iCounter; i++)
	{
		NameList *pSubList = pList->ppLists[i];
		const char *pOldValue;
		pSubList->pResetCB(pSubList);
		while(pOldValue = pSubList->pGetNextCB(pSubList, false)){
			if (stricmp(name, pOldValue)==0) {
				PERFINFO_AUTO_STOP();
				return true;
				break;
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return false;
}

const char *NameList_MultiList_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_MultiList *pList = (NameList_MultiList*)pBaseList;
	unsigned int iSize = eaSize(&pList->ppLists);
	const char *pRetVal;

	if (pList->iCounter >= iSize)
	{
		return NULL;
	}

	do
	{
		do {
			NameList* pCurList = pList->ppLists[pList->iCounter];

			PERFINFO_AUTO_START("getNextCB", 1);
			pRetVal = pCurList->pGetNextCB(pCurList, false);
			PERFINFO_AUTO_STOP();

			if (pRetVal)
			{
				if(	skipDupes &&
					NameList_MultiList_HasEarlierDupeCB(pBaseList, pRetVal))
				{
					continue;
				}

				return pRetVal;
			}
		} while (pRetVal);

		pList->iCounter++;
		if (pList->iCounter >= iSize)
		{
			return NULL;
		}		

		pList->ppLists[pList->iCounter]->pResetCB(pList->ppLists[pList->iCounter]);
	}

	while (1);
	

	return NULL;
}

void NameList_MultiList_ResetCB(NameList *pBaseList)
{
	NameList_MultiList *pList = (NameList_MultiList*)pBaseList;

	pList->iCounter = 0;

	if (eaSize(&pList->ppLists))
	{
		pList->ppLists[0]->pResetCB(pList->ppLists[0]);
	}
}

void NameList_MultiList_FreeCB(NameList *pBaseList)
{
	NameList_MultiList *pList = (NameList_MultiList*)pBaseList;

	int i;

	for (i=0; i < eaSize(&pList->ppLists); i++)
	{
		FreeNameList(pList->ppLists[i]);
	}

	eaDestroy(&pList->ppLists);

	free(pList);
}

//a list of all the commands in a MultiList
NameList *CreateNameList_MultiList(void)
{

	NameList_MultiList *pList = (NameList_MultiList*)calloc(sizeof(NameList_MultiList), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_MultiList_FreeCB;
	pList->baseList.pGetNextCB = NameList_MultiList_GetNextCB;
	pList->baseList.pHasEarlierDupeCB = NameList_MultiList_HasEarlierDupeCB;
	pList->baseList.pResetCB = NameList_MultiList_ResetCB;
	
	pList->iCounter = 0;
	pList->ppLists = NULL;

	return (NameList*)pList;
}

void NameList_MultiList_AddList(NameList *pBaseList, NameList *pChildList)
{
	NameList_MultiList *pList = (NameList_MultiList*)pBaseList;

	pChildList->pResetCB(pChildList);

	eaPush(&pList->ppLists, pChildList);
}

//------------------------------stuff for NameList_RefDictionary
//a list of names of items in a reference dictionary
//the names of things in a reference dictionary
typedef struct
{
	NameList baseList;

	RefDictIterator iterator;
	DictionaryHandle dictHandle;
} NameList_RefDictionary;

char *NameList_RefDictionary_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_RefDictionary *pList = (NameList_RefDictionary*)pBaseList;

	return (char*)RefSystem_GetNextReferenceDataFromIterator(&pList->iterator);
}

void NameList_RefDictionary_ResetCB(NameList *pBaseList)
{
	NameList_RefDictionary *pList = (NameList_RefDictionary*)pBaseList;

	RefSystem_InitRefDictIterator(pList->dictHandle, &pList->iterator);
}


NameList *CreateNameList_RefDictionary(char *pDictionaryName)
{
	NameList_RefDictionary *pList;
	DictionaryHandle handle = RefSystem_GetDictionaryHandleFromNameOrHandle(pDictionaryName);
	if (!handle)
	{
		Errorf("Trying to make a namelist (probably for auto-completion) for unknown dictionary %s\n", pDictionaryName);
		return NULL;
	}

	pList = (NameList_RefDictionary*)calloc(sizeof(NameList_RefDictionary), 1);

	assert(pList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_RefDictionary_GetNextCB;
	pList->baseList.pResetCB = NameList_RefDictionary_ResetCB;

	pList->dictHandle = handle;

	RefSystem_InitRefDictIterator(pList->dictHandle, &pList->iterator);

	assert(RefSystem_DoesDictionaryHaveStringRefData(pList->dictHandle));

	return (NameList*)pList;
}


//------------------------------stuff for NameList_ResourceDictionary
//a list of named things in a resource dictionary
typedef struct
{
	NameList baseList;

	ResourceIterator iterator;
	const char *pDictName;
} NameList_ResourceDictionary;

char *NameList_ResourceDictionary_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	char *result;
	NameList_ResourceDictionary *pList = (NameList_ResourceDictionary*)pBaseList;

	if (resIteratorGetNext(&pList->iterator, &result, NULL))
	{
		return result;
	}
	return NULL;
}

void NameList_ResourceDictionary_ResetCB(NameList *pBaseList)
{
	NameList_ResourceDictionary *pList = (NameList_ResourceDictionary*)pBaseList;

	resInitIterator(pList->pDictName, &pList->iterator);
}

// If we ever end up using NameLists for resource dictionaries that use iterator locking,
// we may need to be more aggressive about freeing these.
void NameList_ResourceDictionary_FreeCB(NameList *pBaseList)
{
	NameList_ResourceDictionary *pList = (NameList_ResourceDictionary*)pBaseList;

	resFreeIterator(&pList->iterator);
}

NameList *CreateNameList_ResourceDictionary(char *pDictionaryName)
{
	ResourceDictionaryInfo *pDictInfo;

	NameList_ResourceDictionary *pList = (NameList_ResourceDictionary*)calloc(sizeof(NameList_ResourceDictionary), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_ResourceDictionary_FreeCB;
	pList->baseList.pGetNextCB = NameList_ResourceDictionary_GetNextCB;
	pList->baseList.pResetCB = NameList_ResourceDictionary_ResetCB;

	pDictInfo = resDictGetInfo(pDictionaryName);
	assert (pDictInfo);
	pList->pDictName = pDictInfo->pDictName;

	resInitIterator(pList->pDictName, &pList->iterator);

	return (NameList*)pList;
}


//------------------------------stuff for NameList_ResourceInfo
//a list of names of resource infos
typedef struct
{
	NameList baseList;

	ResourceDictionaryInfo *dictInfo;
	int i;
} NameList_ResourceInfo;

char *NameList_ResourceInfo_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_ResourceInfo *pList = (NameList_ResourceInfo*)pBaseList;
	if (!pList || !pList->dictInfo)
	{
		return NULL;
	}
	if (pList->i < eaSize(&pList->dictInfo->ppInfos))
	{
		if (!pList->dictInfo->ppInfos[pList->i])
		{
			return NULL;
		}
		return (char *)pList->dictInfo->ppInfos[pList->i++]->resourceName;
	}
	return NULL;
}

void NameList_ResourceInfo_ResetCB(NameList *pBaseList)
{
	NameList_ResourceInfo *pList = (NameList_ResourceInfo*)pBaseList;
	pList->i = 0;
}


NameList *CreateNameList_ResourceInfo(char *pDictionaryName)
{

	NameList_ResourceInfo *pList = (NameList_ResourceInfo*)calloc(sizeof(NameList_ResourceInfo), 1);

	assert(pList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_ResourceInfo_GetNextCB;
	pList->baseList.pResetCB = NameList_ResourceInfo_ResetCB;
	pList->dictInfo = resDictGetInfo(pDictionaryName);
	assert(pList->dictInfo);

	pList->i = 0;

	return (NameList*)pList;
}



//------------------------------stuff for NameList_StashTable
//a list of names of items in a reference dictionary
//the names of things in a reference dictionary
typedef struct
{
	NameList baseList;

	StashTableIterator iterator;
	StashTable table;
} NameList_StashTable;

char *NameList_StashTable_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_StashTable *pList = (NameList_StashTable*)pBaseList;
	StashElement element;

	if (stashGetNextElement(&pList->iterator, &element))
	{
		return stashElementGetStringKey(element);
	}

	return NULL;
}

void NameList_StashTable_ResetCB(NameList *pBaseList)
{
	NameList_StashTable *pList = (NameList_StashTable*)pBaseList;

	stashGetIterator(pList->table, &pList->iterator);
}


NameList *CreateNameList_StashTable(StashTable table)
{

	NameList_StashTable *pList = (NameList_StashTable*)calloc(sizeof(NameList_StashTable), 1);

	assert(pList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_StashTable_GetNextCB;
	pList->baseList.pResetCB = NameList_StashTable_ResetCB;

	pList->table = table;
	stashGetIterator(pList->table, &pList->iterator);

	return (NameList*)pList;
}




//------------------------------stuff for NameList_StaticDefine
//a list of names of items in a reference dictionary
//the names of things in a reference dictionary
typedef struct
{
	NameList baseList;

	StaticDefine *pStaticDefine;
	int iCurIndex;
} NameList_StaticDefine;

const char *NameList_StaticDefine_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_StaticDefine *pList = (NameList_StaticDefine*)pBaseList;
	
	do
	{
		const char *pKey = pList->pStaticDefine[pList->iCurIndex].key;

		if ( pKey == U32_TO_PTR(DM_END))
		{
			return NULL; // failed lookup
		}
		else if (pKey == U32_TO_PTR(DM_TAILLIST))
		{
			pList->pStaticDefine = (StaticDefine *)pList->pStaticDefine[pList->iCurIndex].value;
			pList->iCurIndex = 0;
			continue;
		}
		
		pList->iCurIndex++;

		if (!(pKey == U32_TO_PTR(DM_INT) || pKey == U32_TO_PTR(DM_STRING) || pKey == U32_TO_PTR(DM_DYNLIST)))
		{
			return pKey;
		}
	} while (1);
}

void NameList_StaticDefine_ResetCB(NameList *pBaseList)
{
	NameList_StaticDefine *pList = (NameList_StaticDefine*)pBaseList;

	pList->iCurIndex = 0;
}


NameList *CreateNameList_StaticDefine(StaticDefine *pStaticDefine)
{

	NameList_StaticDefine *pList = (NameList_StaticDefine*)calloc(sizeof(NameList_StaticDefine), 1);

	assert(pList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_StaticDefine_GetNextCB;
	pList->baseList.pResetCB = NameList_StaticDefine_ResetCB;

	pList->pStaticDefine = pStaticDefine;
	pList->iCurIndex = 0;

	return (NameList*)pList;
}





//------------------------------stuff for NameList_MRUList
//a list of names of items in a reference dictionary
//the names of things in a reference dictionary
typedef struct
{
	NameList baseList;

	MRUList *pMRUList;
	int iCurIndex;

	char name[128];
	int maxEntries;
	int maxStringSize;

} NameList_MRUList;

static void NameList_MRUList_LazyInit(NameList_MRUList *pList)
{
	if (!pList->pMRUList)
	{
		pList->pMRUList = createMRUList(pList->name, pList->maxEntries, pList->maxStringSize);
	}
}

const char *NameList_MRUList_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_MRUList *pList = (NameList_MRUList*)pBaseList;

	NameList_MRUList_LazyInit(pList);

	if (pList->iCurIndex >= pList->pMRUList->count)
		return NULL;
	pList->iCurIndex++;
	return pList->pMRUList->values[pList->pMRUList->count - pList->iCurIndex];
}

void NameList_MRUList_ResetCB(NameList *pBaseList)
{
	NameList_MRUList *pList = (NameList_MRUList*)pBaseList;

	pList->iCurIndex = 0;
}

void NameList_MRUList_FreeCB(NameList *pBaseList)
{
	NameList_MRUList *pList = (NameList_MRUList*)pBaseList;
	if (pList->pMRUList)
		destroyMRUList(pList->pMRUList);
	free(pList);
}

void NameList_MRUList_AddName(NameList *pBaseList, const char *string)
{
	NameList_MRUList *pList = (NameList_MRUList*)pBaseList;
	NameList_MRUList_LazyInit(pList);
	mruAddToList(pList->pMRUList, string);
}


NameList *CreateNameList_MRUList(const char *name, int maxEntries, int maxStringSize)
{
	NameList_MRUList *pList = (NameList_MRUList*)calloc(sizeof(NameList_MRUList), 1);

	assert(pList);

	pList->baseList.pFreeCB = NULL;
	pList->baseList.pGetNextCB = NameList_MRUList_GetNextCB;
	pList->baseList.pResetCB = NameList_MRUList_ResetCB;

	sprintf(pList->name, "NameList_%s", name);
	pList->maxEntries = maxEntries;
	pList->maxStringSize = maxStringSize;
	pList->iCurIndex = 0;

	return (NameList*)pList;
}



//----------------------Stuff for NameList_FilesInDirectory
typedef struct
{
	NameList baseList;

	char **ppNames;
	const char *pDirName;
	const char *pNameToMatch;
	unsigned int iCounter;
} NameList_FilesInDirectory;

char *NameList_FilesInDirectory_GetNextCB(NameList *pBaseList, bool skipDupes)
{
	NameList_FilesInDirectory *pList = (NameList_FilesInDirectory*)pBaseList;
	unsigned int iSize = eaSize(&pList->ppNames);

	if (!pList->ppNames)
	{
		return NULL;
	}

	while (pList->iCounter < iSize 
		&& ((pList->pNameToMatch && !strstri(pList->ppNames[pList->iCounter], pList->pNameToMatch))
		|| pList->ppNames[pList->iCounter] && pList->ppNames[pList->iCounter][0] == '_'))
	{
		pList->iCounter++;
	}
	

	if (pList->iCounter >= iSize)
	{
		return NULL;
	}

	return pList->ppNames[pList->iCounter++];
}

void NameList_FilesInDirectory_ResetCB(NameList *pBaseList)
{
	NameList_FilesInDirectory *pList = (NameList_FilesInDirectory*)pBaseList;

	if (pList->ppNames)
	{
		fileScanDirFreeNames(pList->ppNames);
	}

	pList->ppNames = fileScanDirFolders(pList->pDirName, FSF_FILES | FSF_RETURNSHORTNAMES);
	pList->iCounter = 0;

}

void NameList_FilesInDirectory_FreeCB(NameList *pBaseList)
{
	NameList_FilesInDirectory *pList = (NameList_FilesInDirectory*)pBaseList;

	if (pList->ppNames)
	{
		fileScanDirFreeNames(pList->ppNames);
	}

	free(pList);
}

NameList *CreateNameList_FilesInDirectory(const char *pDirName, const char *pNameToMatch)
{

	NameList_FilesInDirectory *pList = (NameList_FilesInDirectory*)calloc(sizeof(NameList_FilesInDirectory), 1);

	assert(pList);

	pList->baseList.pFreeCB = NameList_FilesInDirectory_FreeCB;
	pList->baseList.pGetNextCB = NameList_FilesInDirectory_GetNextCB;
	pList->baseList.pResetCB = NameList_FilesInDirectory_ResetCB;
	
	pList->iCounter = 0;
	pList->ppNames = NULL;
	pList->pDirName = pDirName;
	pList->pNameToMatch = pNameToMatch;

	return (NameList*)pList;
}





NameList *CreateTempNameListFromTypeAndData(enumNameListType eType, void **ppData)
{
	static enumNameListType sLastType = NAMELISTTYPE_NONE;
	static void **sppLastData = NULL;
	static NameList *spLastNameList = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (sLastType == eType && sppLastData == ppData)
	{
		if (spLastNameList)
		{
			spLastNameList->pResetCB(spLastNameList);
			PERFINFO_AUTO_STOP();
			return spLastNameList;
		}
	}

 	if (spLastNameList && sLastType != NAMELISTTYPE_PREEXISTING && sLastType != NAMELISTTYPE_NAMED)
	{
		FreeNameList(spLastNameList);
		spLastNameList = NULL;
	}

	sLastType = eType;
	sppLastData = ppData;

	switch (eType)
	{
	case NAMELISTTYPE_NONE:
		spLastNameList = NULL;
		break;
	case NAMELISTTYPE_PREEXISTING:
		spLastNameList = (NameList*)(*ppData);
		break;
	case NAMELISTTYPE_REFDICTIONARY:
		spLastNameList = CreateNameList_RefDictionary((char*)ppData);
		break;
	case NAMELISTTYPE_RESOURCEDICTIONARY:
		spLastNameList = CreateNameList_ResourceDictionary((char*)ppData);
		break;
	case NAMELISTTYPE_RESOURCEINFO:
		spLastNameList = CreateNameList_ResourceInfo((char*)ppData);
		break;
	case NAMELISTTYPE_STASHTABLE:
		spLastNameList = CreateNameList_StashTable((StashTable)(*ppData));
		break;
	case NAMELISTTYPE_COMMANDLIST:
		spLastNameList = CreateNameList_CmdList((CmdList*)(ppData), 9);
		break;
	case NAMELISTTYPE_STATICDEFINE:
		spLastNameList = CreateNameList_StaticDefine((StaticDefine*)ppData);
		break;
	case NAMELISTTYPE_NAMED:
		{
			NameList *pNameList = NameList_FindByName((char*)ppData);
			if (pNameList)
			{
				pNameList->pResetCB(pNameList);
			}
			PERFINFO_AUTO_STOP();
			return pNameList;
		}

	default:
		assertmsg(0, "Unsupported namelist type");
		break;
	}

	PERFINFO_AUTO_STOP();
	return spLastNameList;
}

bool NameList_HasEarlierDupe(NameList *pList, const char* name)
{
	return	pList->pHasEarlierDupeCB &&
			pList->pHasEarlierDupeCB(pList, name);
}


void NameList_FindAllMatchingStrings(NameList *pList, const char *pInString, const char ***pppOutStrings)
{
	const char *pStr;
	int i;

	char **ppStringsToMatch = NULL;
	static StashTable stAlreadyInserted;

	if (!stAlreadyInserted)
		stAlreadyInserted = stashTableCreateWithStringKeys(64, StashDefault);

	stashTableClear(stAlreadyInserted);

	DivideString(pInString, "/", &ppStringsToMatch, 0);

	pList->pResetCB(pList);

	while ((pStr = pList->pGetNextCB(pList, false)))
	{
		bool bMatches = true;

		PERFINFO_AUTO_START("check match", 1);

		for (i=0; i < eaSize(&ppStringsToMatch); i++)
		{
			if (!strstri(pStr, ppStringsToMatch[i]))
			{
				bMatches = false;
				break;
			}
		}

		PERFINFO_AUTO_STOP_START("insert",1); // on the slow calls, most of the time is in here
		if (bMatches &&
			!stashFindInt(stAlreadyInserted, pStr, NULL))
		{
			verify(stashAddInt(stAlreadyInserted, pStr, 1, false));
			eaPush(pppOutStrings, pStr);
		}
		PERFINFO_AUTO_STOP();
	}

	eaDestroyEx(&ppStringsToMatch, NULL);
}

void NameList_FindAllPrefixMatchingStrings(NameList *pList, const char *pInStringPrefix, const char ***pppOutStrings)
{
	const char *pStr;
	int i;

	char **ppStringsToMatch = NULL;

	DivideString(pInStringPrefix, "/", &ppStringsToMatch, 0);

	pList->pResetCB(pList);

	while ((pStr = pList->pGetNextCB(pList, false)))
	{
		bool bMatches = false;

		for (i=0; i < eaSize(&ppStringsToMatch); i++)
		{
			if (strStartsWith(pStr, ppStringsToMatch[i]))
			{
				bMatches = true;
				break;
			}
		}

		if (bMatches &&
			!NameList_HasEarlierDupe(pList, pStr))
		{
			eaPush(pppOutStrings, pStr);
		}
	}

	eaDestroyEx(&ppStringsToMatch, NULL);
}

