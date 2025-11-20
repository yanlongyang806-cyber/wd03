#ifndef _REFERENCESYSTEM_INTERNAL_H_
#define _REFERENCESYSTEM_INTERNAL_H_
#pragma once
GCC_SYSTEM

#include "ReferenceSystem.h"
#include "StashTable.h"
#include "textparser.h"
#include "eset.h"
#include "MemoryMonitor.h"
#include "HashFunctions.h"
#include "resourceManager.h"

#define REFERENCEDICTIONARY_MAX_NAME_LENGTH 32

#define MAX_REFERENCE_DICTIONARIES 256

#define ASSERT_INITTED if (!gRefSystemInitted) RefSystem_Init();
#define ASSERT_DICTIONARY(eDict) assert(eDict >= 0 && eDict < REF_DICTIONARY_LAST)

typedef struct ReferentInfoStruct ReferentInfoStruct;
typedef struct ResourceDictionary ResourceDictionary;

typedef struct ReferenceDictionary
{
	const char *pName; //allocAddStringed for easy comparison
	const char *pNameForMemoryTracking; //RefDict_name
	const char *pDeprecatedName;
	const void *pHandle; // index into gReferenceDictionaries array

	bool bUsesStringsAsReferenceData;
	bool bCaseSensitiveHashing;
	bool bIsSelfDefining;
	bool bShouldRequestMissingData;
	bool bResourceDictionary;
	bool bRegisterParseTableName;
	bool bIgnoreNullReferenceErrors;

	bool bReferentsLocked;

	RefCallBack_DirectlyDecodeReference *pDirectlyDecodeReferenceCB;
	RefCallBack_GetHashFromReferenceData *pGetHashFromReferenceDataCB;
	RefCallBack_CompareReferenceData *pCompareReferenceDataCB;
	RefCallBack_CopyReferenceData *pCopyReferenceDataCB;
	RefCallBack_FreeReferenceData *pFreeReferenceDataCB;
	RefCallBack_ReferenceDataToString *pRefDataToStringCB;
	RefCallBack_StringToReferenceData *pStringToRefDataCB;

	StashTable refDataTable;
	int iNumberOfReferents;

	StashTable refPreviousTable;
	U32 queuedForCopyLock;
	S32 hasQueuedCopy;
	ReferentInfoStruct** refInfoQueuedForCopy;
	ReferentPrevious** previousQueuedForCopy;

	ResourceDictionary *pResourceDict;
	//optional... some dictionaries know the parse table of the type of referent they "own", and the size of referents in
	//that dictionary (or at least the root of those referents)
	ParseTable *pParseTable;
	int iReferentRootSize;

	unsigned int threadID; // Test for thread use
	int threadErrors;
	

	U32 iNameHash;
	
	PERFINFO_TYPE*	piVerify;

	// Filename and line number of who created this dictionary
	MEM_DBG_STRUCT_PARMS
	
} ReferenceDictionary;

typedef struct ReferentInfoStruct
{
	//used by dictionaries with bUsesStringsAsReferenceData set
	const char *pStringRefData; // allocAddStringed
	//used by other dictionaries
	ReferenceData pOpaqueRefData;

	Referent pReferent;
	ReferenceDictionary *pDictionary;

	ESet setHandles; //Earray of pointers to handles.

	//only used when the dictionary is referent-locked. 
	S16 iNumHandlesWhenDictionaryIsLocked;
	bool bReferentSetBySource;
} ReferentInfoStruct;

extern int giNumReferenceDictionaries;
extern ReferenceDictionary gReferenceDictionaries[MAX_REFERENCE_DICTIONARIES];
extern bool gRefSystemInitted;


extern StashTable gNormalHandleTable;

extern StashTable gReferentTable;

void RefSystem_CreateReferentsTable(void);
void RefSystem_CreateNormalHandlesTable(void);

//table of dictionaries by name, also by name of referenced object (if any), derived from
//name of parse table.
extern StashTable gDictionariesByNameTable;

bool sbRefSystemAllowNulledActiveHandles;

ReferentInfoStruct *GetFreeReferentInfo(MEM_DBG_PARMS_VOID);
void ReleaseReferentInfo(ReferentInfoStruct *pReferentInfo);



//doing the compare before setting seems like just an optimization, but it's actually
//necessary for shared memory, which should already have verified that the
//handle has the correct value. So this must do nothing if it would be setting
//the handle to the value it already has.
__forceinline static void SetActiveRefHandle(ReferenceHandle *pHandle, Referent pReferent)
{
	void *pValToSet;

	pValToSet = pReferent ? (void*)pReferent : REFERENT_SET_BUT_ABSENT;

	if (pValToSet != *pHandle)
	{
		*pHandle = pValToSet;
	}
}


//doing the compare before setting seems like just an optimization, but it's actually
//necessary for shared memory, which should already have verified that the
//handle has the correct value. So this must do nothing if it would be setting
//the handle to the value it already has.
__forceinline static void SetInactiveRefHandle(ReferenceHandle *pHandle)
{
	if (*pHandle)
	{
		*pHandle = NULL;
	}
}


DictionaryHandle RefSystem_GetDictionaryHandleFromDictionary(ReferenceDictionary *pDictionary);
ReferenceDictionary *RefDictionaryFromNameOrHandle(DictionaryHandleOrName dictHandle);
ReferentInfoStruct *FindOrCreateReferentInfo(ReferenceDictionary *pDictionary, ConstReferenceData pRefData,
											 bool bRequireNonNULLReferent, Referent pSelfDefiningReferent);

__forceinline static DictionaryHandle DictionaryHandleFromIndex(int i)
{
	return (void*)((uintptr_t)(i + 1));
}

__forceinline static int DictionaryIndexFromHandle(DictionaryHandle handle)
{
	return (int)(((uintptr_t)handle) - 1);
}

__forceinline static DictionaryHandle DictionaryHandleFromDictionaryPointer(ReferenceDictionary *pDictionary)
{
	return DictionaryHandleFromIndex(pDictionary - gReferenceDictionaries);
}


ReferentInfoStruct *RefSystem_GetNextReferentInfoFromIterator(RefDictIterator *pIterator);

//does the right kind of stash table lookup for this dictionary
#define RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, ppOutReferentInfo) stashFindPointer(pDictionary->refDataTable, pRefData, ppOutReferentInfo)
#define RefSystem_FindRefPreviousFromRefData(pDictionary, pRefData, ppOutReferent) stashFindPointer(pDictionary->refPreviousTable, pRefData, ppOutReferent)

// Clear any referent previous backup data
void RefSystem_ClearReferentPreviousInternal(ReferenceDictionary *pDictionary, const char *pString);

//does the right kind of stash table add, uses the copy of the ref data in the ref info struct
void 	RefSystem_AddRefInfoToRefDataTable(ReferenceDictionary *pDictionary, ReferentInfoStruct *pReferentInfo);

void RefSystem_RemoveReferentInfoFromRefDataTable(ReferentInfoStruct *pReferentInfo);

__forceinline static bool STASHREMOVEPOINTER_NOFAIL(StashTable table, const void* pKey)
{
	bool bRetVal = stashRemovePointer(table, pKey, NULL);
	assert(bRetVal);
	return bRetVal;
}

__forceinline static void RefSystem_RemoveHandle_Internal(	ReferenceHandle *pHandle,
															ReferentInfoStruct *pReferentInfo,
															const char* reason)
{
	memMonitorTrackUserMemory(pReferentInfo->pDictionary->pNameForMemoryTracking, true, 0, -1);

	assertmsgf(*pHandle == pReferentInfo->pReferent || *pHandle == NULL && sbRefSystemAllowNulledActiveHandles || *pHandle == REFERENT_SET_BUT_ABSENT && pReferentInfo->pReferent == NULL, "Reference handle corrupted before removal. Referent name is probably %s",
		pReferentInfo->pStringRefData ? pReferentInfo->pStringRefData : "(UNKNOWN)");

	STASHREMOVEPOINTER_NOFAIL(gNormalHandleTable, pHandle);


	if (pReferentInfo->pDictionary->bReferentsLocked && pReferentInfo->pReferent)
	{
		pReferentInfo->iNumHandlesWhenDictionaryIsLocked--;
		assert(pReferentInfo->iNumHandlesWhenDictionaryIsLocked >= 0);
		if (pReferentInfo->iNumHandlesWhenDictionaryIsLocked == 0)
		{
			if (pReferentInfo->pDictionary->bResourceDictionary)
			{			
				resNotifyNoRefs(pReferentInfo->pDictionary->pName,
								pReferentInfo->pStringRefData,
								true,
								reason);
			}
		}
	}
	else
	{
		assert(eSetRemove(&pReferentInfo->setHandles, pHandle));

		if (eSetGetCount(&pReferentInfo->setHandles) == 0)
		{				
			if (pReferentInfo->pDictionary->bResourceDictionary)
			{
				resNotifyNoRefs(pReferentInfo->pDictionary->pName,
								pReferentInfo->pStringRefData,
								true,
								reason);
			}
			else if (!pReferentInfo->pDictionary->bIsSelfDefining)
			{
				ReleaseReferentInfo(pReferentInfo);
			}
		}
	}

	SetInactiveRefHandle(pHandle);
}


#endif
