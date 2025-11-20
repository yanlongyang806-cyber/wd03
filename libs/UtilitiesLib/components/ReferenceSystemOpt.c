#include "ReferenceSystem_Internal.h"
#include "wininclude.h"
#include "earray.h"
#include "timing.h"
#include "estring.h"
#include "stringCache.h"
#include "MemoryPool.h"
#include "mutex.h"
#include "ResourceSystem_Internal.h"

#define DEBUG_PRINTF if (0) printf


#define TEST_SIMPLE_POINTER_REFERENCES 0

#define REFVER_STRING "REFVER:"
#define REFVER_STRING_LEN 7

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

bool sbRefSystemAllowNulledActiveHandles = false;

extern bool gCreditMemoryToDictionary; // Defined in ReferenceSystem.c

static bool bSuppressUnknownDictionaryWarning_All=false;
char **ppSupressUnknownDictionaryWarningNames = NULL;



void ReleaseReferentInfo_Internal(ReferentInfoStruct *pReferentInfo);

//see comments in ReferenceSystem.h
void SetRefSystemAllowsNulledActiveHandles(bool bAllow)
{
	sbRefSystemAllowNulledActiveHandles = bAllow;
}

bool SuppressUnknownDictionaryWarning(char *pDictionaryName)
{
	if (bSuppressUnknownDictionaryWarning_All)
	{
		return true;
	}

	if (eaFindString(&ppSupressUnknownDictionaryWarningNames, pDictionaryName) != -1)
	{
		return true;
	}

	return false;
}

// Hides the warning about setting a reference to unknown dictionaries - useful in tools which only read partial sets of data
void SetRefSystemSuppressUnknownDicitonaryWarning_All(bool bSuppress)
{
	bSuppressUnknownDictionaryWarning_All = bSuppress;
}

//like the above, takes a comma-seperated list of names
void SetRefSystemSuppressUnknownDicitonaryWarning_CertainDictionaries(char *pDictNames)
{
	DivideString(pDictNames, ",", &ppSupressUnknownDictionaryWarningNames, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);
}

ReferenceDictionary *RefDictionaryFromNameOrHandle(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pRetVal;

	if (!dictHandle)
	{
		return NULL;
	}

	if ((uintptr_t)dictHandle <= (uintptr_t)giNumReferenceDictionaries)
	{
		return &gReferenceDictionaries[((uintptr_t)dictHandle) - 1];
	}

	if (stashFindPointer(gDictionariesByNameTable, (char*)dictHandle, &pRetVal))
	{
		return pRetVal;
	}

	return NULL;
}

bool RefSystem_DoesDictionaryHaveStringRefData(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;
	
	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
	{
		return false;
	}
	return pDictionary->bUsesStringsAsReferenceData;
}

bool RefSystem_DoesDictionaryExist(DictionaryHandleOrName dictHandle)
{
	if (RefDictionaryFromNameOrHandle(dictHandle))
	{
		return true;
	}
	return false;
}

DictionaryHandle RefSystem_GetDictionaryHandleFromDictionary(ReferenceDictionary *pDictionary)
{
	return DictionaryHandleFromIndex(pDictionary - gReferenceDictionaries);
}

DictionaryHandle RefSystem_GetDictionaryHandleFromNameOrHandle(DictionaryHandleOrName pDictionaryName)
{
	U32 iHash;
	int i;

	ASSERT_INITTED


	if ((uintptr_t)pDictionaryName <= (uintptr_t)giNumReferenceDictionaries)
	{
		// already a handle
		return (void *)pDictionaryName;
	}

	iHash = hashString((char *)pDictionaryName, false);

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		if (gReferenceDictionaries[i].iNameHash == iHash)
		{
			return DictionaryHandleFromIndex(i);
		}
	}

	return NULL;
}

const char *RefSystem_GetDictionaryNameFromNameOrHandle(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (pDictionary)
	{
		return pDictionary->pName;
	}
	return (const char *)dictHandle;
}

ResourceDictionary *RefSystem_GetResourceDictFromNameOrHandle(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (pDictionary)
	{
		return pDictionary->pResourceDict;
	}
	return 0;
}

ReferentInfoStruct *FindOrCreateReferentInfo(ReferenceDictionary *pDictionary, ConstReferenceData pRefData,
	bool bRequireNonNULLReferent, Referent pSelfDefiningReferent)
{
	Referent pReferent = NULL;
	
	bool bReferentIsRefData = false;

	ReferentInfoStruct *pReferentInfo = NULL;

	if (pDictionary->threadID)
	{
		if (pDictionary->threadID != GetCurrentThreadId())
		{
			pDictionary->threadErrors++;
		}
	}
	else
	{
		pDictionary->threadID = GetCurrentThreadId();
	}
	if (RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pReferentInfo))
	{
		assert(pReferentInfo && pReferentInfo->pDictionary == pDictionary);
		if (bRequireNonNULLReferent && !pReferentInfo->pReferent)
		{
			return NULL;
		}

		return pReferentInfo;
	}

	if (pDictionary->bIsSelfDefining)
	{
		pReferent = pSelfDefiningReferent;
	}
	else
	{
		pReferent = pDictionary->pDirectlyDecodeReferenceCB(pRefData);
	}

	if (pReferent == REFERENT_INVALID)
	{

		return NULL;
	}

	if (bRequireNonNULLReferent && !pReferent)
	{
		return NULL;
	}

	//have to do a special check to see if ref data and referent are equal. If they are, then this is a
	//"trivial" dictionary, and we need to reset the referent after calling the copy callback function,
	//so the referent pointer points to the right copy of the ref data.
	if ((void*)pReferent == (void*)pRefData)
	{
		bReferentIsRefData = true;
	}

	if (gCreditMemoryToDictionary)
	{
		pReferentInfo = GetFreeReferentInfo(MEM_DBG_STRUCT_PARMS_CALL_VOID(pDictionary));
	}
	else
	{
		pReferentInfo = GetFreeReferentInfo(MEM_DBG_PARMS_INIT_VOID);
	}

	pReferentInfo->pReferent = pReferent;

	pReferentInfo->pDictionary = pDictionary;
	
	if (pDictionary->bUsesStringsAsReferenceData)
	{
		pReferentInfo->pStringRefData = allocAddString(pRefData);		
		pReferentInfo->pOpaqueRefData = (ReferenceData)pReferentInfo->pStringRefData;
	}
	else
	{
		pReferentInfo->pOpaqueRefData = pDictionary->pCopyReferenceDataCB(pRefData);
		pReferentInfo->pStringRefData = pDictionary->pRefDataToStringCB(pReferentInfo->pOpaqueRefData);
	}

	if (bReferentIsRefData)
	{
		pReferentInfo->pReferent = (pReferentInfo->pOpaqueRefData);
	}	


	RefSystem_AddRefInfoToRefDataTable(pDictionary, pReferentInfo);

	if (pReferent)
	{
		ReferentInfoStruct *pTempReferentInfo;
		assertmsg(!stashFindPointer(gReferentTable, pReferent, &pTempReferentInfo), "Two different reference datas point to the same referent");

		if(!gReferentTable){
			RefSystem_CreateReferentsTable();
		}
		stashAddPointer(gReferentTable, pReferent, pReferentInfo, false);
		pDictionary->iNumberOfReferents++;
	}

	return pReferentInfo;
}

static void RefSystem_CreateDictPreviousTable(ReferenceDictionary* d){
	StashTable st;

	if(d->refPreviousTable){
		return;
	}
	
	if(gCreditMemoryToDictionary){
		if (d->bUsesStringsAsReferenceData){
			st = stashTableCreateWithStringKeysEx(	1024,
													d->bCaseSensitiveHashing ?
														StashCaseSensitive : 
														StashDefault
													MEM_DBG_STRUCT_PARMS_CALL(d));
		}else{
			st = stashTableCreateExternalFunctionsEx(	1024,
														StashDefault,
														d->pGetHashFromReferenceDataCB,
														d->pCompareReferenceDataCB
														MEM_DBG_STRUCT_PARMS_CALL(d));
		}
	}
	else if(d->bUsesStringsAsReferenceData){
		st = stashTableCreateWithStringKeys(1024,
											d->bCaseSensitiveHashing ?
												StashCaseSensitive :
												StashDefault);
	}else{
		st = stashTableCreateExternalFunctions(	1024,
												StashDefault,
												d->pGetHashFromReferenceDataCB,
												d->pCompareReferenceDataCB);
	}

	d->refPreviousTable = st;
}

static ReferenceDictionary**	dictsWithQueuedCopy;
static U32						dictsWithQueuedCopyLock;

void RefSystem_CopyQueuedToPrevious(void)
{
	if(!eaSize(&dictsWithQueuedCopy)){
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	
	assert(!dictsWithQueuedCopyLock);
	
	EARRAY_CONST_FOREACH_BEGIN(dictsWithQueuedCopy, i, isize);
	{
		ReferenceDictionary* d = dictsWithQueuedCopy[i];
		
		ASSERT_TRUE_AND_RESET(d->hasQueuedCopy);
		
		EARRAY_CONST_FOREACH_BEGIN(d->refInfoQueuedForCopy, j, jsize);
		{
			ReferentInfoStruct* pReferentInfo = d->refInfoQueuedForCopy[j];
			ReferentPrevious*	rp;
			
			if(!pReferentInfo){
				continue;
			}
			
			d->refInfoQueuedForCopy[j] = NULL;
			
			rp = callocStruct(ReferentPrevious);

			RefSystem_CreateDictPreviousTable(d);
			
			if(!stashAddPointer(d->refPreviousTable, pReferentInfo->pStringRefData, rp, false)){
				assert(0);
			}

			rp->referent = pReferentInfo->pReferent;
			rp->version = 1;
		}
		EARRAY_FOREACH_END;
		
		eaSetSize(&d->refInfoQueuedForCopy, 0);

		EARRAY_CONST_FOREACH_BEGIN(d->previousQueuedForCopy, j, jsize);
		{
			ReferentPrevious* rp = d->previousQueuedForCopy[j];
			
			if(!rp){
				continue;
			}
			
			d->previousQueuedForCopy[j] = NULL;
			
			ASSERT_TRUE_AND_RESET(rp->isQueuedForCopy);
			
			if(rp->referentPrevious){
				StructDestroySafeVoid(	d->pParseTable,
										&rp->referentPrevious);

				rp->version++;
			}
		}
		EARRAY_FOREACH_END;
		
		eaSetSize(&d->previousQueuedForCopy, 0);
	}
	EARRAY_FOREACH_END;
	
	eaClear(&dictsWithQueuedCopy);
	assert(!dictsWithQueuedCopyLock);

	PERFINFO_AUTO_STOP();
}


bool RefSystem_SetHandleFromRefDataWithReason(	DictionaryHandle dictHandle,
												ConstReferenceData pRefData,
												ReferenceHandle *pHandle,
												const char* reason)
{
	ReferentInfoStruct *pOldRefInfo = NULL;
	ReferentInfoStruct *pReferentInfo = NULL;
	ReferenceDictionary *pDictionary;
	ESetHandle setHandle;

	ASSERT_INITTED;

	assert(pHandle);

	PERFINFO_AUTO_START_FUNC_L2();
	
	stashFindPointer(gNormalHandleTable, pHandle, &pOldRefInfo);

	if (!pRefData)
	{
		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
		else
		{		
			SetInactiveRefHandle(pHandle);
		}
		PERFINFO_AUTO_STOP_L2();
		return true;
	}

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
	{
		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
		PERFINFO_AUTO_STOP_L2();
		return false;
	}
	
	pReferentInfo = FindOrCreateReferentInfo(pDictionary, pRefData, false, NULL);
	
	if (pOldRefInfo)
	{
		if (pOldRefInfo == pReferentInfo)
		{
			// Already the same
			PERFINFO_AUTO_STOP_L2();
			return true;
		}
		else
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
			// This can possibly invalidate pReferentInfo!
			pReferentInfo = FindOrCreateReferentInfo(pDictionary, pRefData, false, NULL);
		}
	}

	if (!pReferentInfo)
	{
		SetInactiveRefHandle(pHandle);
		PERFINFO_AUTO_STOP_L2();
		return false;
	}
	
	if (pDictionary->bReferentsLocked && pReferentInfo->pReferent)
	{
		pReferentInfo->iNumHandlesWhenDictionaryIsLocked++;
		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);
		SetActiveRefHandle(pHandle, pReferentInfo->pReferent);
		if (pReferentInfo->iNumHandlesWhenDictionaryIsLocked == 1
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(dictHandle, pRefData, reason);
		}
	}
	else
	{
		ResourceDictionary *pResDict = resGetDictionary(dictHandle);
		ResourceStatus *pResourceStatus = pResDict && pResDict->bShouldRequestMissingData ? resGetStatus(pResDict, pReferentInfo->pStringRefData) : NULL;

		setHandle = &pReferentInfo->setHandles;	
		if (gCreditMemoryToDictionary)
		{
			assert(eSetAdd_dbg(setHandle, pHandle MEM_DBG_STRUCT_PARMS_CALL(pDictionary)));
		}
		else
		{
			eSetAdd(setHandle, pHandle);
		}
		assert(setHandle == &pReferentInfo->setHandles);
		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);

		SetActiveRefHandle(pHandle, pReferentInfo->pReferent);

		if ((eSetGetCount(&pReferentInfo->setHandles) == 1 || (pResDict && pResourceStatus == NULL && eSetGetCount(&pReferentInfo->setHandles) > 0))
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(dictHandle, pRefData, reason);
		}
	}

	memMonitorTrackUserMemory(pDictionary->pNameForMemoryTracking, true, 0, 1);

	PERFINFO_AUTO_STOP_L2();

	return true;
}

bool RefSystem_SetHandleFromReferent(	DictionaryHandle dictHandle,
										Referent pReferent,
										ReferenceHandle *pHandle,
										const char* reason)
{
	ReferentInfoStruct *pOldRefInfo = NULL;
	ReferentInfoStruct *pReferentInfo = NULL;
	ReferenceDictionary *pDictionary;
	ESetHandle setHandle;

	PERFINFO_AUTO_START_FUNC();

	ASSERT_INITTED;

	assert(pHandle);	

	if (*pHandle == pReferent)
	{
		// Early exit
		PERFINFO_AUTO_STOP();
		return true;
	}

	

	stashFindPointer(gNormalHandleTable, pHandle, &pOldRefInfo);
	


	if (!pReferent)
	{
		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
		else
		{		
			SetInactiveRefHandle(pHandle);
		}
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (!stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
		PERFINFO_AUTO_STOP();
		return false;
	}



	if (pOldRefInfo)
	{
		if (pOldRefInfo == pReferentInfo)
		{
			// Already the same
			PERFINFO_AUTO_STOP();
			return true;
		}
		else
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
	}

	pDictionary = pReferentInfo->pDictionary;


	if (pDictionary->bReferentsLocked)
	{
		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);	
		SetActiveRefHandle(pHandle, pReferent);
		pReferentInfo->iNumHandlesWhenDictionaryIsLocked++;

		if (pReferentInfo->iNumHandlesWhenDictionaryIsLocked  == 1
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(dictHandle, pReferentInfo->pStringRefData, reason);
		}
	}
	else
	{
		ResourceDictionary *pResDict = resGetDictionary(dictHandle);
		ResourceStatus *pResourceStatus = pResDict && pResDict->bShouldRequestMissingData ?  resGetStatus(pResDict, pReferentInfo->pStringRefData) : NULL;

		setHandle = &pReferentInfo->setHandles;	
		if (gCreditMemoryToDictionary)
		{
			assert(eSetAdd_dbg(setHandle, pHandle MEM_DBG_STRUCT_PARMS_CALL(pDictionary)));
		}
		else
		{
			eSetAdd(setHandle, pHandle);
		}
		assert(setHandle == &pReferentInfo->setHandles);
		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);	

		SetActiveRefHandle(pHandle, pReferentInfo->pReferent);

		if ((eSetGetCount(&pReferentInfo->setHandles) == 1 || (pResDict && pResourceStatus == NULL && eSetGetCount(&pReferentInfo->setHandles) > 0))
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(dictHandle, pReferentInfo->pStringRefData, reason);
		}
	}

	memMonitorTrackUserMemory(pDictionary->pNameForMemoryTracking, true, 0, 1);

	PERFINFO_AUTO_STOP();

	return true;
}

bool RefSystem_SetHandleFromStringWithReason(	DictionaryHandle dictHandle,
												const char *pString,
												ReferenceHandle *pHandle,
												const char* reason)
{
	ConstReferenceData pRefData;
	ReferenceDictionary *pDictionary;
	ReferentInfoStruct *pOldRefInfo = NULL;
	ReferentInfoStruct  *pReferentInfo = NULL;
	ESetHandle setHandle;

	PERFINFO_AUTO_START_FUNC();

	ASSERT_INITTED;

	stashFindPointer(gNormalHandleTable, pHandle, &pOldRefInfo);

	if (!SAFE_DEREF(pString))
	{
		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}
		else
		{		
			SetInactiveRefHandle(pHandle);
		}
		PERFINFO_AUTO_STOP();
		return true;
	}

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
	{
		char *pDictName;

		if (pOldRefInfo)
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
		}

		pDictName = ((intptr_t)dictHandle > 256 ? (char*)dictHandle : "(unknown))");

		if (!SuppressUnknownDictionaryWarning(pDictName))
		{
			Errorf("Trying to set a reference in unknown dictionary %s... if this is on the login server you probably need to add this dictionary to make character xfers work", pDictName); 
		}
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (pDictionary->bUsesStringsAsReferenceData)
	{
		U32 iRequestedVersion = 0;

		pRefData = pString;
	}
	else
	{
		pRefData = pDictionary->pStringToRefDataCB(pString);

		if (!pRefData)
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	pReferentInfo = FindOrCreateReferentInfo(pDictionary, pRefData, false, NULL);

	if (pOldRefInfo)
	{
		if (pOldRefInfo == pReferentInfo)
		{
			// Already the same
			PERFINFO_AUTO_STOP();
			return true;
		}
		else
		{
			RefSystem_RemoveHandle_Internal(pHandle, pOldRefInfo, reason);
			// This can possibly invalidate pReferentInfo!
			pReferentInfo = FindOrCreateReferentInfo(pDictionary, pRefData, false, NULL);
		}
	}

	if (!pReferentInfo)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (pDictionary->bReferentsLocked && pReferentInfo->pReferent)
	{
		pReferentInfo->iNumHandlesWhenDictionaryIsLocked++;
		
		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);

		SetActiveRefHandle(pHandle, pReferentInfo->pReferent);

		if (pReferentInfo->iNumHandlesWhenDictionaryIsLocked  == 1
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(pDictionary->pHandle, pRefData, reason); // FASTLOAD
		}	
	}
	else
	{
		ResourceDictionary *pResDict = resGetDictionary(pDictionary->pHandle);
		ResourceStatus *pResourceStatus = pResDict && pResDict->bShouldRequestMissingData ? resGetStatus(pResDict, pString) : NULL;

		setHandle = &pReferentInfo->setHandles;	
		if (gCreditMemoryToDictionary)
		{
			assert(eSetAdd_dbg(setHandle, pHandle MEM_DBG_STRUCT_PARMS_CALL(pDictionary)));
		}
		else
		{
			eSetAdd(setHandle, pHandle);
		}
		assert(setHandle == &pReferentInfo->setHandles);

		if(!gNormalHandleTable){
			RefSystem_CreateNormalHandlesTable();
		}
		stashAddPointer(gNormalHandleTable, pHandle, pReferentInfo, false);

		SetActiveRefHandle(pHandle, pReferentInfo->pReferent);

		if ((eSetGetCount(&pReferentInfo->setHandles) == 1 || (pResDict && pResourceStatus == NULL && eSetGetCount(&pReferentInfo->setHandles) > 0))
			&& pDictionary->bResourceDictionary)
		{
			resNotifyRefsExistWithReason(pDictionary->pHandle, pRefData, reason); // FASTLOAD
		}
	}

	memMonitorTrackUserMemory(pDictionary->pNameForMemoryTracking, true, 0, 1);

	if (!pDictionary->bUsesStringsAsReferenceData)
		pDictionary->pFreeReferenceDataCB((ReferenceData)pRefData);

	PERFINFO_AUTO_STOP();
	return true;
}


void RefSystem_AppendReferenceString(char **ppEString, ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;

	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{		
		return;
	}

	estrAppend(ppEString, &pReferentInfo->pStringRefData);
	
}

const char *RefSystem_StringFromHandle(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;

	PERFINFO_AUTO_START_FUNC_L2();

	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
	
		PERFINFO_AUTO_STOP_L2();
		return NULL;

	}

	
	PERFINFO_AUTO_STOP_L2();
	
	return pReferentInfo->pStringRefData;
}

bool RefSystem_IsHandleActive(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;
	bool				retVal;
	
	PERFINFO_AUTO_START_FUNC_L2();

	ASSERT_INITTED;
	assert(pHandle);

	retVal = stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo);
	
	PERFINFO_AUTO_STOP_L2();
	
	return retVal;
}

const char *RefSystem_DictionaryNameFromHandle(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;
	
	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
		return NULL;
	}

	return pReferentInfo->pDictionary->pName;
}


void RefSystem_AddReferentWithReason(	DictionaryHandleOrName dictHandle,
										ConstReferenceData pRefData,
										Referent pReferent,
										const char* reason)
{
	ReferentInfoStruct *pReferentInfo;
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	
	assertmsgf(!pDictionary->bReferentsLocked, "Can't add referents to locked dictionary %s", pDictionary->pName);

	assert(pDictionary);
	assert(pReferent);
	assert(pRefData);

	if (stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		assertmsg(0, "Trying to add a referent where one already exists");
	}

	if (RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pReferentInfo))
	{
		int i;
		int iSize;

		if (pReferentInfo->pReferent)
		{
			if (pDictionary->bUsesStringsAsReferenceData)
			{
				assertmsgf(0, "In dictionary %s, adding a referent named %s, although one already exists",
					pDictionary->pName, (char*)pRefData);
			}
			else
			{
				assertmsgf(0, "In dictionary %s, adding a referent with the same name as one that already exists (non-string names)",
					pDictionary->pName);
			}
		}

		pReferentInfo->bReferentSetBySource = true;
		pReferentInfo->pReferent = pReferent;

		iSize = eSetGetMaxSize(&pReferentInfo->setHandles);

		for (i=0; i < iSize; i++)
		{
			void *pRef;
			if (pRef = eSetGetValueAtIndex(&pReferentInfo->setHandles, i))
			{			
				SetActiveRefHandle(pRef, pReferent);
			}
		}

		if(!gReferentTable){
			RefSystem_CreateReferentsTable();
		}
		stashAddPointer(gReferentTable, pReferent, pReferentInfo, false);
		pDictionary->iNumberOfReferents++;
	}
	else if (pDictionary->bIsSelfDefining)
	{
		pReferentInfo = FindOrCreateReferentInfo(pDictionary, pRefData, false, pReferent);
	}
	
	if (pDictionary->bResourceDictionary)
	{
		resNotifyObjectCreated(pDictionary->pResourceDict, pRefData, pReferent, reason);
		if (eSetGetCount(&pReferentInfo->setHandles) == 0)
		{
			resNotifyNoRefs(dictHandle, pRefData, false, reason); //FASTLOAD
		}
	}
}

void ReleaseReferentInfo(ReferentInfoStruct *pReferentInfo)
{

	RefSystem_RemoveReferentInfoFromRefDataTable(pReferentInfo);
	RefSystem_ClearReferentPreviousInternal(pReferentInfo->pDictionary, pReferentInfo->pStringRefData);

	if (pReferentInfo->pReferent)
	{
		STASHREMOVEPOINTER_NOFAIL(gReferentTable, pReferentInfo->pReferent);
		pReferentInfo->pDictionary->iNumberOfReferents--;
	}

	if (!pReferentInfo->pDictionary->bUsesStringsAsReferenceData)
	{
		// If it's string data, it was freed above
		pReferentInfo->pDictionary->pFreeReferenceDataCB(pReferentInfo->pOpaqueRefData);
	}

	eSetDestroy(&pReferentInfo->setHandles);

	ReleaseReferentInfo_Internal(pReferentInfo);
}

void RefSystem_RemoveHandleWithReason(	ReferenceHandle *pHandle,
										const char* reason)
{
	ReferentInfoStruct *pReferentInfo;
	
	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
		// Ok to try and remove inactive handle, just make sure it is cleared
		SetInactiveRefHandle(pHandle);
		return;
	}

	RefSystem_RemoveHandle_Internal(pHandle, pReferentInfo, reason);
}

void RefSystem_MoveHandle(ReferenceHandle *pNewHandle, ReferenceHandle *pOldHandle)
{
	ReferentInfoStruct *pReferentInfo;
	ESetHandle setHandle;

	ASSERT_INITTED;
	assert(pOldHandle);
	assert(pNewHandle);


	if (!stashFindPointer(gNormalHandleTable, pOldHandle, &pReferentInfo))
	{
		return;
	}

	STASHREMOVEPOINTER_NOFAIL(gNormalHandleTable, pOldHandle);
	stashAddPointer(gNormalHandleTable, pNewHandle, pReferentInfo, false);
	
	if (pReferentInfo->pDictionary->bReferentsLocked && pReferentInfo->pReferent)
	{
		//do nothing
	}
	else
	{
		setHandle = &pReferentInfo->setHandles;	
		assert(eSetRemove(setHandle, pOldHandle));
		if (gCreditMemoryToDictionary)
		{
			assert(eSetAdd_dbg(setHandle, pNewHandle MEM_DBG_STRUCT_PARMS_CALL(pReferentInfo->pDictionary)));
		}
		else
		{
			eSetAdd(setHandle, pNewHandle);
		}
		assert(setHandle == &pReferentInfo->setHandles);
	}
}

ReferenceData RefSystem_RefDataFromHandle(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;
	
	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
		return NULL;
	}

	return pReferentInfo->pOpaqueRefData;
}

bool RefSystem_CopyHandleWithReason(ReferenceHandle *pDstHandle,
									ConstReferenceHandle *pSrcHandle,
									const char* reason)
{
	ReferentInfoStruct *pReferentInfo = NULL;
	ReferentInfoStruct *pDestInfo = NULL;
	bool bResult;

	PERFINFO_AUTO_START_FUNC();

	ASSERT_INITTED;
	assert(pSrcHandle);
	assert(pDstHandle);

	if (*pSrcHandle == *pDstHandle && *pSrcHandle != REFERENT_SET_BUT_ABSENT)
	{
		// Already the same, and not REFERENT_SET_BUT_ABSENT (this will catch the case where both are NULL)
		PERFINFO_AUTO_STOP();
		return true;
	}

	stashFindPointer(gNormalHandleTable, pSrcHandle, &pReferentInfo);
	stashFindPointer(gNormalHandleTable, pDstHandle, &pDestInfo);

	// Copy from source only if source is active
	if (pReferentInfo)
	{
		if (pDestInfo)
		{
			if (pDestInfo == pReferentInfo)
			{
				// Already the same
				assert(*pDstHandle == *pSrcHandle);
				if(pDestInfo->pReferent){
					assert(*pDstHandle == pDestInfo->pReferent);
				}else{
					assert(*pDstHandle == REFERENT_SET_BUT_ABSENT);
				}
				PERFINFO_AUTO_STOP();
				return true;	
			}
			else
			{
				RefSystem_RemoveHandle_Internal(pDstHandle, pDestInfo, reason);
			}
		}
		bResult = RefSystem_SetHandleFromRefDataWithReason(RefSystem_GetDictionaryHandleFromDictionary(pReferentInfo->pDictionary), pReferentInfo->pOpaqueRefData, pDstHandle, reason);
		PERFINFO_AUTO_STOP();
		return bResult;	
	}
	else
	{
		if (pDestInfo)
		{
			RefSystem_RemoveHandle_Internal(pDstHandle, pDestInfo, reason);
		}
	}
	PERFINFO_AUTO_STOP();
	return true;
}

bool RefSystem_RemoveReferentWithReason(Referent pReferent,
										bool bCompletelyRemoveHandlesToMe,
										const char* reason)
{
	void *pOldReferent = NULL;
	ReferentInfoStruct *pReferentInfo;
	int iNormalSize, i;
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED;
	assert(pReferent);

	if (!stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		//for self-defining dictionaries, trying to remove a nonexistent referent is bad. For other dictionaries, it's fine
		//thus, this can not be an assertion, even if it kind of seems like it should be one
		
		return false;
	}

	pDictionary = pReferentInfo->pDictionary;

	assertmsgf(!pDictionary->bReferentsLocked, "Can't remove referents from locked dictionary %s", pDictionary->pName);

	iNormalSize = eSetGetMaxSize(&pReferentInfo->setHandles);

	pOldReferent = pReferentInfo->pReferent;
	if (pDictionary->bResourceDictionary)
	{
		resNotifyObjectDestroyed(pDictionary->pResourceDict, pReferentInfo->pStringRefData, pOldReferent, reason); // FASTLOAD
	}

	STASHREMOVEPOINTER_NOFAIL(gReferentTable, pReferent);
	pReferentInfo->pDictionary->iNumberOfReferents--;
		
	for (i=0; i < iNormalSize; i++)
	{
		void *pRef = eSetGetValueAtIndex(&pReferentInfo->setHandles, i);
		if (pRef)
		{		
			if (bCompletelyRemoveHandlesToMe)
			{
				memMonitorTrackUserMemory(pReferentInfo->pDictionary->pNameForMemoryTracking, true, 0, -1);

				STASHREMOVEPOINTER_NOFAIL(gNormalHandleTable, pRef);
			}

			if(bCompletelyRemoveHandlesToMe){
				SetInactiveRefHandle(pRef);
			}else{
				SetActiveRefHandle(pRef, NULL);
			}
		}
	}

	pReferentInfo->bReferentSetBySource = true;
	pReferentInfo->pReferent = NULL;

	if (bCompletelyRemoveHandlesToMe)
	{		
		ReleaseReferentInfo(pReferentInfo);
	}
	else if (eSetGetCount(&pReferentInfo->setHandles) == 0)
	{
		assertmsg(pReferentInfo->pDictionary->bIsSelfDefining, "Only self-defining dictionaries can have referents with no handles");
		ReleaseReferentInfo(pReferentInfo);
	}

	return true;
}

void RefSystem_MarkSetBySourceWithReason(		DictionaryHandleOrName dictHandle,
													ConstReferenceData pRefData,
													const char* reason)
{
	ReferentInfoStruct *pReferentInfo;
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	
	assertmsgf(!pDictionary->bReferentsLocked, "Can't add referents to locked dictionary %s", pDictionary->pName);

	assert(pDictionary);
	assert(pRefData);

	if (RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pReferentInfo))
	{
		pReferentInfo->bReferentSetBySource = true;
	}
}

void RefSystem_MoveReferentWithReason(	Referent pNewReferent,
										Referent pOldReferent,
										const char* reason)
{
	ReferentInfoStruct *pReferentInfo;
	int iSize, i;

	ASSERT_INITTED;
	assert(pNewReferent);
	assert(pOldReferent);

	if (!stashFindPointer(gReferentTable, pOldReferent, &pReferentInfo))
	{
		return;
	}
	
	assertmsgf(!pReferentInfo->pDictionary->bReferentsLocked, "Can't move a referent in locked dictionary %s", pReferentInfo->pDictionary->pName);


	if (pReferentInfo->pDictionary->bResourceDictionary)
		resNotifyObjectPreModified(pReferentInfo->pDictionary->pHandle, pReferentInfo->pStringRefData);

	STASHREMOVEPOINTER_NOFAIL(gReferentTable, pOldReferent);

	pReferentInfo->pReferent = pNewReferent;
	pReferentInfo->bReferentSetBySource = true;

	stashAddPointer(gReferentTable, pNewReferent, pReferentInfo, false);

	iSize = eSetGetMaxSize(&pReferentInfo->setHandles);

	for (i=0; i < iSize; i++)
	{
		void *pRef;
		if (pRef = eSetGetValueAtIndex(&pReferentInfo->setHandles, i))
		{		
			SetActiveRefHandle(pRef, pNewReferent);
		}
	}
	
	RefSystem_ReferentModifiedWithReason(pReferentInfo->pReferent, reason);
}

Referent RefSystem_ReferentFromString(DictionaryHandleOrName dictHandle, const char *pString)
{
	ReferenceDictionary *pDictionary;
	ReferentInfoStruct *pReferentInfo;
	ASSERT_INITTED;

	if (!pString)
		return NULL;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
		return NULL;

	if (pDictionary->bUsesStringsAsReferenceData)
	{
		if (RefSystem_FindRefInfoFromRefData(pDictionary, pString, &pReferentInfo))
		{
			return pReferentInfo->pReferent;
		}

		if (pDictionary->bIsSelfDefining)
		{
			return NULL;
		}

		return pDictionary->pDirectlyDecodeReferenceCB(pString);
	}
	else
	{
		ReferenceData pRefData = NULL;
		Referent pReferent;

		pRefData = pDictionary->pStringToRefDataCB(pString);

		if (!pRefData)
		{
			return NULL;
		}

		if (RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pReferentInfo))
		{
		
			pDictionary->pFreeReferenceDataCB(pRefData);
			

			return pReferentInfo->pReferent;
		}


		pReferent = pDictionary->pDirectlyDecodeReferenceCB(pRefData);
		pDictionary->pFreeReferenceDataCB(pRefData);

		return pReferent;
	}
}

ReferentPrevious* RefSystem_ReferentPreviousFromString(DictionaryHandleOrName dictHandle, const char *pString)
{
	ReferenceDictionary *pDictionary;
	ReferentPrevious* rp;
	ASSERT_INITTED;

	if (!pString)
		return NULL;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!SAFE_MEMBER(pDictionary, bUsesStringsAsReferenceData))
		return NULL;

	if (RefSystem_FindRefPreviousFromRefData(pDictionary, pString, &rp))
	{
		return rp;
	}

	return NULL;
}

static void RefSystem_QueueDictionaryForCopy(ReferenceDictionary* d){
	if(FALSE_THEN_SET(d->hasQueuedCopy)){
		writeLockU32(&dictsWithQueuedCopyLock, 0);
		eaPush(&dictsWithQueuedCopy, d);
		writeUnlockU32(&dictsWithQueuedCopyLock);
	}
}

void RefSystem_QueueCopyReferentToPrevious(DictionaryHandleOrName dictHandle, const char *pString)
{
	ReferenceDictionary *pDictionary;
	ReferentPrevious* rp;
	ASSERT_INITTED;

	if (!pString)
		return;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!SAFE_MEMBER(pDictionary, bUsesStringsAsReferenceData))
		return;

	assertmsgf(	!pDictionary->bReferentsLocked,
				"A dictionary can not both be referent locked and"
				" support previousReferents... being tried for %s",
				pDictionary->pName);

	rp = RefSystem_ReferentPreviousFromString(dictHandle, pString);
	
	if(!rp)
	{
		ReferentInfoStruct *pReferentInfo;
		
		if (RefSystem_FindRefInfoFromRefData(pDictionary, pString, &pReferentInfo))
		{
			writeLockU32(&pDictionary->queuedForCopyLock, 0);
			RefSystem_QueueDictionaryForCopy(pDictionary);
			eaPushUnique(&pDictionary->refInfoQueuedForCopy, pReferentInfo);
			writeUnlockU32(&pDictionary->queuedForCopyLock);
		}
	}
	else if(!rp->isQueuedForCopy)
	{
		writeLockU32(&pDictionary->queuedForCopyLock, 0);
		if(FALSE_THEN_SET(rp->isQueuedForCopy)){
			RefSystem_QueueDictionaryForCopy(pDictionary);
			eaPush(&pDictionary->previousQueuedForCopy, rp);
		}else{
			assert(pDictionary->hasQueuedCopy);
		}
		writeUnlockU32(&pDictionary->queuedForCopyLock);
	}
}

void RefSystem_ClearReferentPreviousInternal(ReferenceDictionary *d, const char *pString)
{
	ReferentPrevious* rp;

	if (!pString)
		return;

	if (!SAFE_MEMBER(d, bUsesStringsAsReferenceData) || !d->refPreviousTable)
		return;

	if(stashRemovePointer(	d->refPreviousTable,
							pString,
							&rp))
	{
		if(TRUE_THEN_RESET(rp->isQueuedForCopy)){
			S32 index = eaFind(&d->previousQueuedForCopy, rp);
				
			if(index < 0){
				assertmsg(0, "ReferentPrevious missing from previousQueuedForCopy");
			}
				
			// Set to NULL to not break the iteration of this array.
				
			d->previousQueuedForCopy[index] = NULL;
		}
		else if(eaFind(&d->previousQueuedForCopy, rp) >= 0){
			assertmsg(0, "ReferentPrevious unexpectedly in previousQueuedForCopy");
		}

		StructDestroySafeVoid(	d->pParseTable,
								&rp->referentPrevious);

		SAFE_FREE(rp);
	}
}

void RefSystem_ClearReferentPrevious(DictionaryHandleOrName dictHandle, const char *pString)
{
	ReferenceDictionary *pDictionary;

	if (!pString)
		return;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	if (pDictionary)
		RefSystem_ClearReferentPreviousInternal(pDictionary, pString);
}

#define NUM_REFERENT_INFOS_TO_ALLOCATE_AT_ONCE 256

MP_DEFINE(ReferentInfoStruct);

ReferentInfoStruct *GetFreeReferentInfo(MEM_DBG_PARMS_VOID)
{
	MP_CREATE(ReferentInfoStruct, NUM_REFERENT_INFOS_TO_ALLOCATE_AT_ONCE);

	return MP_ALLOC(ReferentInfoStruct);
}

void ReleaseReferentInfo_Internal(ReferentInfoStruct *pReferentInfo)
{
	MP_FREE(ReferentInfoStruct, pReferentInfo);
}

//stuff relating to RefDictIterators
void RefSystem_InitRefDictIterator(DictionaryHandleOrName dictHandle, RefDictIterator *pIterator)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	assert(pDictionary);

	stashGetIterator(pDictionary->refDataTable, &pIterator->stashIterator);
}

Referent RefSystem_GetNextReferentFromIterator(RefDictIterator *pIterator)
{
	StashElement element;

	while (stashGetNextElement(&pIterator->stashIterator, &element))
	{
		ReferentInfoStruct *pInfo = stashElementGetPointer(element);

		if (pInfo->pReferent)
		{
			return pInfo->pReferent;
		}
	}

	return NULL;
}

ReferenceData RefSystem_GetNextReferenceDataFromIterator(RefDictIterator *pIterator)
{
	StashElement element;

	if (stashGetNextElement(&pIterator->stashIterator, &element))
	{
		ReferentInfoStruct *pInfo = stashElementGetPointer(element);
		return pInfo->pOpaqueRefData;
	}

	return NULL;
}

void RefSystem_GetNextReferentAndRefDataFromIterator(RefDictIterator *pIterator, Referent *pOutReferent, ReferenceData *pOutReferenceData)
{
	StashElement element;

	if (stashGetNextElement(&pIterator->stashIterator, &element))
	{
		ReferentInfoStruct *pInfo = stashElementGetPointer(element);
		*pOutReferent = pInfo->pReferent;
		*pOutReferenceData = pInfo->pOpaqueRefData;
	}
	else
	{
		*pOutReferent = NULL;
		*pOutReferenceData = NULL;
	}
}




ReferenceData RefSystem_GetNextReferenceDataWithNULLReferentFromIterator(RefDictIterator *pIterator)
{
	StashElement element;

	while (stashGetNextElement(&pIterator->stashIterator, &element))
	{
		ReferentInfoStruct *pInfo = stashElementGetPointer(element);

		if (!pInfo->pReferent)
		{
			return pInfo->pOpaqueRefData;
		}
	}

	return NULL;
}

ReferentInfoStruct *RefSystem_GetNextReferentInfoFromIterator(RefDictIterator *pIterator)
{
	StashElement element;

	if (stashGetNextElement(&pIterator->stashIterator, &element))
	{
		ReferentInfoStruct *pInfo = stashElementGetPointer(element);
		return pInfo;
	}

	return NULL;
}

bool RefSystem_DoesDictHaveAnyNULLHandles(DictionaryHandleOrName dictHandle)
{
	RefDictIterator iterator;
	RefSystem_InitRefDictIterator(dictHandle, &iterator);
	if (RefSystem_GetNextReferenceDataWithNULLReferentFromIterator(&iterator))
	{
		return true;
	}

	return false;
}

U32 RefSystem_GetDictionaryNumberOfReferentInfos(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
		return 0;

	return stashGetCount(pDictionary->refDataTable);
}

U32 RefSystem_GetDictionaryNumberOfReferents(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

		pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!pDictionary)
		return 0;

	return pDictionary->iNumberOfReferents;
}

bool RefSystem_GetDictionaryIgnoreNullReferences(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;
	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	return SAFE_MEMBER(pDictionary, bIgnoreNullReferenceErrors);
}

void RefSystem_SetDictionaryIgnoreNullReferences(DictionaryHandleOrName dictHandle, bool b)
{
	ReferenceDictionary *pDictionary;
	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	if (pDictionary)
		pDictionary->bIgnoreNullReferenceErrors = b;
}


void RefSystem_LockDictionaryReferents(DictionaryHandleOrName pDictionaryName)
{
	ReferenceDictionary *pDictionary;
	pDictionary = RefDictionaryFromNameOrHandle(pDictionaryName);
	if (pDictionary)
	{
		StashTableIterator stashIterator;
		StashElement element;

		assertmsgf(pDictionary->bIsSelfDefining, "LockDictionaryReferents can only be called on self defining dictionaries... was called on %s", pDictionary->pName);

		assertmsgf(!pDictionary->previousQueuedForCopy && !pDictionary->refPreviousTable, "LockDictionaryReferents can not be called on dictionaries that use PreviousReferentInfo stuff... was called on %s", pDictionary->pName);

		if (pDictionary->bReferentsLocked)
		{
			return;
		}

		pDictionary->bReferentsLocked = true;

		stashGetIterator(pDictionary->refDataTable, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			ReferentInfoStruct *pReferentInfo = stashElementGetPointer(element);

			//if the referent is NULL, we leave the handle table so we can get names from the handles and so forth

			if (pReferentInfo->pReferent)
			{
				pReferentInfo->iNumHandlesWhenDictionaryIsLocked = eSetGetMaxSize(&pReferentInfo->setHandles);
				eSetDestroy(&pReferentInfo->setHandles);
			}
		}
	}
}





ParseTable *RefSystem_GetDictionaryParseTable(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!SAFE_MEMBER(pDictionary, pParseTable))
	{
		return NULL;
	}

	return pDictionary->pParseTable;
}

int RefSystem_GetDictionaryReferentSize(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!SAFE_MEMBER(pDictionary, pParseTable))
	{
		return 0;
	}

	return pDictionary->iReferentRootSize;
}


// Does a root path lookup, for the object path system
int RefSystem_RootPathLookup(const char *name, const char *key, ParseTable** table, void** structptr, int* column)
{
	DictionaryHandle dictHandle;
	void *object;
	ParseTable *refTable;
	if (!SAFE_DEREF(key) || !SAFE_DEREF(name))
	{
		return ROOTPATH_UNHANDLED;
	}
	dictHandle = RefSystem_GetDictionaryHandleFromNameOrHandle(name);
	if (!dictHandle)
	{
		// Dictionary doesn't exist
		return ROOTPATH_UNHANDLED;
	}

	refTable = RefSystem_GetDictionaryParseTable(dictHandle);
	if (!refTable)
	{
		// No parsetable associated, this isn't a proper type of reference dictionary
		return ROOTPATH_UNHANDLED;
	}

	object = RefSystem_ReferentFromString(name,key);
	*table = refTable;
	*structptr = object;
	*column = -1;

	if (!object)
	{	
		return ROOTPATH_NOTFOUND;
	}

	return ROOTPATH_FOUND;

}

//counts the number of active handles pointing to a given referent
int RefSystem_GetReferenceCountForReferent(Referent pReferent)
{
	ReferentInfoStruct *pReferentInfo;
	if (stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		if (pReferentInfo->pDictionary->bReferentsLocked && pReferentInfo->pReferent)
		{
			return pReferentInfo->iNumHandlesWhenDictionaryIsLocked;
		}
		else
		{
			return eSetGetCount(&pReferentInfo->setHandles);
		}
	}

	return 0;
}

bool RefSystem_DoesReferentExist(const Referent pReferent)
{
	ReferentInfoStruct *pReferentInfo;
	if (stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		return true;
	}

	return false;
}

bool RefSystem_FindReferentAndCountHandles(DictionaryHandleOrName dictHandle, char *pRefData, void **ppStruct, int *piNumHandles)
{
	ReferenceDictionary *pDictionary;
	ReferentInfoStruct *pReferentInfo;

	ASSERT_INITTED;

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (!SAFE_MEMBER(pDictionary, bUsesStringsAsReferenceData))
	{
		return false;
	}


	if (RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pReferentInfo))
	{
		*ppStruct = pReferentInfo->pReferent;

		if (pReferentInfo->pDictionary->bReferentsLocked && pReferentInfo->pReferent)
		{
			*piNumHandles = pReferentInfo->iNumHandlesWhenDictionaryIsLocked;
		}
		else
		{
			*piNumHandles = eSetGetCount(&pReferentInfo->setHandles);
		}

		return true;
	}

	if (!pDictionary->bIsSelfDefining)
	{
		*ppStruct = pDictionary->pDirectlyDecodeReferenceCB(pRefData);
		if (*ppStruct)
		{
			*piNumHandles = 0;
			return true;
		}
	}
	return false;
}

void RefSystem_ReferentModifiedWithReason(	Referent pReferent,
											const char* reason)
{
	ReferentInfoStruct *pReferentInfo;
	ASSERT_INITTED;
	
	if (!stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		return;
	}	

	if (pReferentInfo->pDictionary->bResourceDictionary)
	{
		resNotifyObjectModifiedWithReason(	pReferentInfo->pDictionary->pHandle,
											pReferentInfo->pStringRefData,
											reason);
	}

}

const char *RefSystem_StringFromReferent(Referent pReferent)
{
	ReferentInfoStruct *pReferentInfo;

	if (!stashFindPointer(gReferentTable, pReferent, &pReferentInfo))
	{
		return "UNKNOWN";
	}

	return pReferentInfo->pStringRefData;
}




//does the right kind of stash table add, uses the copy of the ref data in the ref info struct
void RefSystem_AddRefInfoToRefDataTable(ReferenceDictionary *pDictionary, ReferentInfoStruct *pReferentInfo)
{
	assert(stashAddPointer(pReferentInfo->pDictionary->refDataTable, pReferentInfo->pDictionary->bUsesStringsAsReferenceData ? pReferentInfo->pStringRefData : pReferentInfo->pOpaqueRefData, pReferentInfo, false));
}

void RefSystem_RemoveReferentInfoFromRefDataTable(ReferentInfoStruct *pReferentInfo)
{
	ReferenceDictionary* d = pReferentInfo->pDictionary;
	
	if(d->bUsesStringsAsReferenceData){
		assert(stashRemovePointer(	d->refDataTable,
									pReferentInfo->pStringRefData,
									NULL));
	}else{
		assert(stashRemovePointer(	d->refDataTable,
									pReferentInfo->pOpaqueRefData,
									NULL));
	}

	{
		S32 index = eaFind(&d->refInfoQueuedForCopy, pReferentInfo);
		
		if(index >= 0){
			// Set to NULL to not break the iteration of this array.

			d->refInfoQueuedForCopy[index] = NULL;
		}
	}
}


bool RefSystem_CompareHandles(ConstReferenceHandle *pHandle1, ConstReferenceHandle *pHandle2)
{
	ReferentInfoStruct *pReferentInfo1 = NULL;
	ReferentInfoStruct *pReferentInfo2 = NULL;

	stashFindPointer(gNormalHandleTable, pHandle1, &pReferentInfo1);
	stashFindPointer(gNormalHandleTable, pHandle2, &pReferentInfo2);
	

	return pReferentInfo1 == pReferentInfo2;
}

bool RefSystem_IsReferentSetBySource(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo = NULL;
	if(stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
		return pReferentInfo->bReferentSetBySource;
	}

	return false;
}

ParseTable *RefSystem_ParseTableFromHandle(ConstReferenceHandle *pHandle)
{
	ReferentInfoStruct *pReferentInfo;

	ASSERT_INITTED;
	assert(pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pReferentInfo))
	{
		return NULL;
	}

	return pReferentInfo->pDictionary->pParseTable;
}
