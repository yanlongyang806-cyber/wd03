#include "ReferenceSystem.h"
#include "ReferenceSystem_Internal.h"

#include "stringutil.h"
#include "ScratchStack.h"
#include "stringcache.h"
#include "estring.h"
#include "fileutil.h"
#include "timing.h"

#include "ReferenceSystem_c_ast.h"
#include "httpXpathSupport.h"


#if 0
	#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
	#define DEBUG_PRINTF(...)
#endif

#define TEST_SIMPLE_POINTER_REFERENCES 0

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););

bool gRefSystemInitted = false;
int giNumReferenceDictionaries;
ReferenceDictionary gReferenceDictionaries[MAX_REFERENCE_DICTIONARIES] = {0};

bool gCreditMemoryToDictionary = false;

StashTable gNormalHandleTable;
StashTable gReferentTable;

//table of dictionaries by name, also by name of referenced object (if any), derived from
//name of parse table.
StashTable gDictionariesByNameTable;

#if REFSYSTEM_TEST_HARNESS
Referent RTHDirectlyDecodeReference(ConstReferenceData pRefData);
ReferenceHashValue RTHGetHashFromReferenceData(ConstReferenceData pRefData);
ReferenceData RTHCopyReferenceData(ConstReferenceData pRefData);
void RTHFreeReferenceData(ConstReferenceData pRefData);
bool RTHReferenceDataToString(char **ppEString, ConstReferenceData pRefData);
ReferenceData RTHStringToReferenceData(const char *pString);

void RTH_Test(void);
#endif


#if TEST_SIMPLE_POINTER_REFERENCES
void TestSimplePointerReferences(void);
#endif

AUTO_STRUCT;
typedef struct NamedStashTableSize
{
	char *pName;
	int iSize;
} NamedStashTableSize;

AUTO_STRUCT;
typedef struct NamedStashTableSizeList
{
	NamedStashTableSize **ppTableSize;
} NamedStashTableSizeList;

static NamedStashTableSizeList sSpecificRefSystemStashTableSizeList = {0};
static NamedStashTableSizeList sGlobalRefSystemStashTableSizeList = {0};

static bool sbDidRefSystemStashTableSizeLoading = false;

bool ProcessRefSystemOverviewIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags);



int GetRefSystemStashTableStartingSize(const char *pName)
{
	if (sbDidRefSystemStashTableSizeLoading)
	{
		int i;

		for (i=0; i < eaSize(&sSpecificRefSystemStashTableSizeList.ppTableSize); i++)
		{
			if (stricmp(sSpecificRefSystemStashTableSizeList.ppTableSize[i]->pName, pName) == 0)
			{
				return sSpecificRefSystemStashTableSizeList.ppTableSize[i]->iSize;
			}
		}

		for (i=0; i < eaSize(&sGlobalRefSystemStashTableSizeList.ppTableSize); i++)
		{
			if (stricmp(sGlobalRefSystemStashTableSizeList.ppTableSize[i]->pName, pName) == 0)
			{
				return sGlobalRefSystemStashTableSizeList.ppTableSize[i]->iSize;
			}
		}
	}
	if (IsGameServerBasedType())
	{
		if (stricmp(pName,"Referents")==0)
			return 1048576;
		else if (stricmp(pName,"NormalHandles")==0)
			return 1048576;
		else if (stricmp(pName,"DynMove")==0)
			return 32768;
		else if (stricmp(pName,"DynSequence")==0)
			return 4096;
		else if (stricmp(pName,"ModelHeader")==0)
			return 32768;
		else if (stricmp(pName,"Message")==0)
			return 131072;
		else if (stricmp(pName,"TranslationEnglish")==0)
			return 131072;
//		else if (stricmp(pName,"TranslationChineseTraditional")==0)
//			return 131072;
//		else if (stricmp(pName,"TranslationKorean")==0)
//			return 131072;
//		else if (stricmp(pName,"TranslationJapanese")==0)
//			return 131072;
		else if (stricmp(pName,"TranslationGerman")==0)
			return 131072;
		else if (stricmp(pName,"TranslationFrench")==0)
			return 131072;
//		else if (stricmp(pName,"TranslationSpanish")==0)
//			return 131072;
//		else if (stricmp(pName,"TranslationRussian")==0)
//			return 131072;
	}
	return 64;
}

void RefSystem_LoadStashTableSizesFromDataFiles(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	int i;

	sbDidRefSystemStashTableSizeLoading = true;

	if (fileExists("server/RefSysStashTableSizes.txt"))
	{
		ParserReadTextFile("server/RefSysStashTableSizes.txt", parse_NamedStashTableSizeList, &sGlobalRefSystemStashTableSizeList, 0);
	}

	sprintf(fileName, "server/%s_RefSysStashTableSizes.txt", GlobalTypeToName(GetAppGlobalType()));

	if (fileExists(fileName))
	{
		ParserReadTextFile(fileName, parse_NamedStashTableSizeList, &sSpecificRefSystemStashTableSizeList, 0);
	}

	if(gNormalHandleTable)
	{
		stashTableSetMinSize(gNormalHandleTable, GetRefSystemStashTableStartingSize("NormalHandles"));
	}

	if(gReferentTable)
	{
		stashTableSetMinSize(gReferentTable, GetRefSystemStashTableStartingSize("Referents"));
	}

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		stashTableSetMinSize(gReferenceDictionaries[i].refDataTable, GetRefSystemStashTableStartingSize(gReferenceDictionaries[i].pName));
	}
}



void AddRefDictToStashTable(ReferenceDictionary *pDict, const char *pName,  ParseTable *pTPI)
{
	ReferenceDictionary *pOther;

	if (stashFindPointer(gDictionariesByNameTable, pName, &pOther))
	{
		assertmsgf(0, "Ref system conflict... two dictionaries named %s (or one dictionary and one contained type)\n", pName);
	}
	stashAddPointer(gDictionariesByNameTable, pName, pDict, false);

	if (pTPI && pDict->bRegisterParseTableName)
	{
		const char *pTableName = ParserGetTableName(pTPI);

		if (pTableName)
		{
			if (stashFindPointer(gDictionariesByNameTable, pTableName, &pOther))
			{
				assertmsgf(pOther == pDict, "Ref system conflict... two dictionaries named %s (or one dictionary and one contained type)\n", pTableName);
			}
			else
			{
				stashAddPointer(gDictionariesByNameTable, pTableName, pDict, false);
			}
		}
	}
}

DictionaryHandle RefSystem_RegisterDictionary_dbg(const char *pName,
	RefCallBack_DirectlyDecodeReference *pDecodeCB,
	RefCallBack_GetHashFromReferenceData *pGetHashCB,
	RefCallBack_CompareReferenceData *pCompareCB,
	RefCallBack_ReferenceDataToString *pRefDataToStringCB,
	RefCallBack_StringToReferenceData *pStringToRefDataCB,
	RefCallBack_CopyReferenceData *pCopyReferenceCB,
	RefCallBack_FreeReferenceData *pFreeReferenceCB,
	ParseTable *pParseTable, bool bRegisterParseTableName
	MEM_DBG_PARMS)
{
	ReferenceDictionary *pDictionary;
	ASSERT_INITTED

	assert(pName);
	assert(strlen(pName) < REFERENCEDICTIONARY_MAX_NAME_LENGTH);
	assert(giNumReferenceDictionaries < MAX_REFERENCE_DICTIONARIES);
	assertmsgf(RefDictionaryFromNameOrHandle(pName) == NULL, "Duplicate reference dictionaries with name %s", pName);

	pDictionary = &gReferenceDictionaries[giNumReferenceDictionaries];

	MEM_DBG_STRUCT_PARMS_INIT(pDictionary);

	pDictionary->pName = allocAddString(pName);
	pDictionary->pNameForMemoryTracking = allocAddString(STACK_SPRINTF("RefDict_%s", pName));

	pDictionary->pHandle = RefSystem_GetDictionaryHandleFromDictionary(pDictionary);
	pDictionary->pCopyReferenceDataCB = pCopyReferenceCB;
	pDictionary->pDirectlyDecodeReferenceCB = pDecodeCB;
	pDictionary->pFreeReferenceDataCB = pFreeReferenceCB;
	pDictionary->pGetHashFromReferenceDataCB = pGetHashCB;
	pDictionary->pCompareReferenceDataCB = pCompareCB;
	pDictionary->pRefDataToStringCB = pRefDataToStringCB;
	pDictionary->pStringToRefDataCB = pStringToRefDataCB;
	pDictionary->bIsSelfDefining = !pDecodeCB;


	pDictionary->iNameHash = hashString(pName, false);

	if (gCreditMemoryToDictionary)
	{
		pDictionary->refDataTable = stashTableCreateExternalFunctionsEx(GetRefSystemStashTableStartingSize(pDictionary->pName), StashDefault, pGetHashCB, pCompareCB MEM_DBG_STRUCT_PARMS_CALL(pDictionary));
	}
	else
	{
		pDictionary->refDataTable = stashTableCreateExternalFunctions(GetRefSystemStashTableStartingSize(pDictionary->pName), StashDefault, pGetHashCB, pCompareCB);
	}

	pDictionary->pParseTable = pParseTable;
	pDictionary->bRegisterParseTableName = bRegisterParseTableName;
	if (pParseTable)
		pDictionary->iReferentRootSize = ParserGetTableSize(pParseTable);

	AddRefDictToStashTable(pDictionary, pName, pParseTable);

	memBudgetAddMapping(pDictionary->pNameForMemoryTracking, BUDGET_ReferenceHandles);
	
	return DictionaryHandleFromIndex(giNumReferenceDictionaries++);
}

static int RefSystem_IntGetHash(ConstReferenceData pRefData, int iHashSeed)
{
	return MurmurHash2_inline((void*)pRefData, 4, iHashSeed);
}

static int RefSystem_IntCompare(ConstReferenceData pRefData1, ConstReferenceData pRefData2)
{
	return (*((int*)pRefData1)) != (*((int*)pRefData2));
}

static char *RefSystem_IntToString(ConstReferenceData pRefData)
{
	static char tempString[32];
	sprintf(tempString, "%d", *((int *)pRefData));
	return tempString;
}

static ReferenceData RefSystem_IntFromString(const char *pString)
{
	Errorf("can't convert int dictionary entries to strings");
	assert(0);
	return NULL;
}

static ReferenceData RefSystem_IntCopyReference(ConstReferenceData pRefData)
{
	int *pRefDataOut = malloc(sizeof(int));
	*pRefDataOut = *((int *)pRefData);
	return pRefDataOut;
}

static void RefSystem_IntFreeReference(ReferenceData *pRefData)
{
	free(pRefData);
}

DictionaryHandle RefSystem_RegisterDictionaryWithIntRefData_dbg(const char *pName,
	RefCallBack_DirectlyDecodeReference *pDecodeCB,
	ParseTable *pParseTable, bool bRegisterParseTableName
	MEM_DBG_PARMS)
{
	return RefSystem_RegisterDictionary_dbg(pName, 
		pDecodeCB, 
		RefSystem_IntGetHash,
		RefSystem_IntCompare,
		RefSystem_IntToString,
		RefSystem_IntFromString,
		RefSystem_IntCopyReference,
		RefSystem_IntFreeReference,
		pParseTable, bRegisterParseTableName MEM_DBG_PARMS_CALL);
}

DictionaryHandle RefSystem_RegisterDictionaryWithStringRefData_dbg(const char *pName,
	RefCallBack_DirectlyDecodeReference *pDecodeCB,
	bool bCaseSensitive,
	ParseTable *pParseTable, bool bRegisterParseTableName
	MEM_DBG_PARMS)
{
	ReferenceDictionary *pDictionary;
	ASSERT_INITTED

	assert(pName);
	assert(strlen(pName) < REFERENCEDICTIONARY_MAX_NAME_LENGTH);
	assert(giNumReferenceDictionaries < MAX_REFERENCE_DICTIONARIES);
	assertmsgf(RefDictionaryFromNameOrHandle(pName) == NULL, "Duplicate reference dictionaries with name %s", pName);
	assert(pDecodeCB);

	pDictionary = &gReferenceDictionaries[giNumReferenceDictionaries];
	MEM_DBG_STRUCT_PARMS_INIT(pDictionary);

	pDictionary->pHandle = RefSystem_GetDictionaryHandleFromDictionary(pDictionary);
	pDictionary->pName = allocAddString(pName);
	pDictionary->pNameForMemoryTracking = allocAddString(STACK_SPRINTF("RefDict_%s", pName));

	pDictionary->pDirectlyDecodeReferenceCB = pDecodeCB;


	pDictionary->iNameHash = hashString(pName, false);


	pDictionary->bUsesStringsAsReferenceData = true;
	pDictionary->bCaseSensitiveHashing = bCaseSensitive;
	
	if (gCreditMemoryToDictionary)
	{
		pDictionary->refDataTable = stashTableCreateWithStringKeysEx(GetRefSystemStashTableStartingSize(pDictionary->pName), bCaseSensitive ? StashCaseSensitive : StashDefault MEM_DBG_STRUCT_PARMS_CALL(pDictionary));
	}
	else
	{
		pDictionary->refDataTable = stashTableCreateWithStringKeys(GetRefSystemStashTableStartingSize(pDictionary->pName), bCaseSensitive ? StashCaseSensitive : StashDefault);
	}

	pDictionary->pParseTable = pParseTable;
	pDictionary->bRegisterParseTableName = bRegisterParseTableName;
	if (pParseTable)
		pDictionary->iReferentRootSize = ParserGetTableSize(pParseTable);

	AddRefDictToStashTable(pDictionary, pName, pParseTable);

	memBudgetAddMapping(pDictionary->pNameForMemoryTracking, BUDGET_ReferenceHandles);
	resRegisterDictionaryForRefDict_dbg(pName MEM_DBG_STRUCT_PARMS_CALL(pDictionary));

	return DictionaryHandleFromIndex(giNumReferenceDictionaries++);
}



DictionaryHandle RefSystem_RegisterSelfDefiningDictionary_dbg(const char *pName, bool bCaseSensitive,
	ParseTable *pParseTable, bool bRegisterParseTableName, bool bMaintainEArray, const char *pDeprecatedName
	MEM_DBG_PARMS)
{
	DictionaryHandle dictHandle;
	ReferenceDictionary *pDictionary;
	ASSERT_INITTED

	assert(pName);
	assert(giNumReferenceDictionaries < MAX_REFERENCE_DICTIONARIES);
	assertmsgf(RefDictionaryFromNameOrHandle(pName) == NULL, "Duplicate reference dictionaries with name %s", pName);

	pDictionary = &gReferenceDictionaries[giNumReferenceDictionaries];
	pDictionary->pHandle = RefSystem_GetDictionaryHandleFromDictionary(pDictionary);
	pDictionary->pName = allocAddString(pName);
	pDictionary->pNameForMemoryTracking = allocAddString(STACK_SPRINTF("RefDict_%s", pName));

	MEM_DBG_STRUCT_PARMS_INIT(pDictionary);
	

	if (pDeprecatedName)
	{
		pDictionary->pDeprecatedName = allocAddString(pDeprecatedName);
	}

	pDictionary->iNameHash = hashString(pName, false);


	pDictionary->bUsesStringsAsReferenceData = true;
	pDictionary->bCaseSensitiveHashing = bCaseSensitive;
	pDictionary->bIsSelfDefining = true;

	if (gCreditMemoryToDictionary)
	{
		pDictionary->refDataTable = stashTableCreateWithStringKeysEx(GetRefSystemStashTableStartingSize(pDictionary->pName), bCaseSensitive ? StashCaseSensitive : StashDefault MEM_DBG_STRUCT_PARMS_CALL(pDictionary));
	}
	else
	{
		pDictionary->refDataTable = stashTableCreateWithStringKeys(GetRefSystemStashTableStartingSize(pDictionary->pName), bCaseSensitive ? StashCaseSensitive : StashDefault);
	}

	pDictionary->pParseTable = pParseTable;
	pDictionary->bRegisterParseTableName = bRegisterParseTableName;
	if (pParseTable)
		pDictionary->iReferentRootSize = ParserGetTableSize(pParseTable);		

	AddRefDictToStashTable(pDictionary, pName, pParseTable);


	dictHandle = DictionaryHandleFromIndex(giNumReferenceDictionaries++);

	memBudgetAddMapping(pDictionary->pNameForMemoryTracking, BUDGET_ReferenceHandles);

	resRegisterDictionaryForRefDict_dbg(pName MEM_DBG_STRUCT_PARMS_CALL(pDictionary));
	pDictionary->bResourceDictionary = true;

	if (bMaintainEArray)
	{
		resDictEnableEArrayStruct(pName, true);
	}
	return dictHandle;
}

const char *RefSystem_GetDictionaryDeprecatedName(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary;

	ASSERT_INITTED

	pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	if (pDictionary && pDictionary->pDeprecatedName)
	{
		return pDictionary->pDeprecatedName;
	}
	return NULL;
}


void RefSystem_ClearDictionary(DictionaryHandleOrName dictHandle, bool bCompletelyRemoveHandlesToMe)
{
	RefDictIterator iterator;
	Referent pReferent;
	RefSystem_InitRefDictIterator(dictHandle, &iterator);
	while ((pReferent = RefSystem_GetNextReferentFromIterator(&iterator)))
		RefSystem_RemoveReferent(pReferent, bCompletelyRemoveHandlesToMe);
}

void RefSystem_ClearDictionaryEx(DictionaryHandleOrName dictHandle, bool bCompletelyRemoveHandlesToMe, ReferentDestructor destructor)
{
	RefDictIterator iterator;
	Referent pReferent;
	RefSystem_InitRefDictIterator(dictHandle, &iterator);
	while ((pReferent = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		RefSystem_RemoveReferent(pReferent, bCompletelyRemoveHandlesToMe);
		if (destructor)
			destructor(pReferent);
		else
			free(pReferent);
	}
}

//stuff relating to the "null dictionary"
Referent NullDictDirectlyDecodeReference(ConstReferenceData pRefData)
{
	return (Referent)pRefData;
}

int NullDictGetHashFromReferenceData(ConstReferenceData pRefData, int iHashSeed)
{
	return MurmurHash2_inline((U8*)(&pRefData), sizeof(void*), iHashSeed);

}

int NullDictCompareReferenceData(ConstReferenceData pRefData1, ConstReferenceData pRefData2)
{
	return pRefData1 != pRefData2;
}

ReferenceData NullDictCopyReferenceData(ConstReferenceData pRefData)
{
	
	return (ReferenceData)pRefData;
}

void NullDictFreeReferenceData(ConstReferenceData pRefData)
{
}

//This function converts a reference data to a string, and APPENDS that string to the EString
const char *NullDictReferenceDataToString(ConstReferenceData pRefData)
{
	static char tempString[32];
	snprintf_s(tempString, sizeof(tempString)-1, "%Id", (uintptr_t)pRefData);
	return tempString;
}



ReferenceData NullDictStringToReferenceData(const char *pString)
{
	Errorf("can't convert null dictionary entries to strings");
	assert(0);
	return NULL;

}

void RefSystem_CreateReferentsTable(void){
	if(!gReferentTable){
		U32 size = GetRefSystemStashTableStartingSize("Referents");
		printf("RefSystem: Creating Referents table, size %d.\n", size);
		gReferentTable = stashTableCreateAddress(size);
		stashTableSetMinSize(gReferentTable, size);
	}
}

void RefSystem_CreateNormalHandlesTable(void){
	if(!gNormalHandleTable){
		U32 size = GetRefSystemStashTableStartingSize("NormalHandles");
		printf("RefSystem: Creating NormalHandles table, size %d.\n", size);
		gNormalHandleTable = stashTableCreateAddress(size);	
		stashTableSetMinSize(gNormalHandleTable, size);
	}
}



void RefSystem_Init(void)
{
	ReferenceDictionary *pDictionary;

	if (gRefSystemInitted)
	{
		return;
	}

	memset(gReferenceDictionaries, 0, sizeof(gReferenceDictionaries));

	gRefSystemInitted = true;

	gDictionariesByNameTable = stashTableCreateWithStringKeys(64, StashDeepCopyKeys_NeverRelease);


	RefSystem_RegisterDictionary_dbg("nullDictionary",
		NullDictDirectlyDecodeReference,
		NullDictGetHashFromReferenceData,
		NullDictCompareReferenceData,
		NullDictReferenceDataToString,
		NullDictStringToReferenceData,
		NullDictCopyReferenceData,
		NullDictFreeReferenceData, NULL, 0
		MEM_DBG_PARMS_INIT);

	pDictionary = RefDictionaryFromNameOrHandle("nullDictionary");
	assert(pDictionary);


#if REFSYSTEM_TEST_HARNESS
#if	TEST_HARNESS_STRINGVERSION
	RefSystem_RegisterDictionaryWithStringRefData("TestHarness",
		RTHDirectlyDecodeReference, true, NULL, 0);
#else
	RefSystem_RegisterDictionary_dbg("TestHarness",
		RTHDirectlyDecodeReference,
		RTHGetHashFromReferenceData,
		RTHReferenceDataToString,
		RTHStringToReferenceData,
		RTHCopyReferenceData,
		RTHFreeReferenceData
		MEM_DBG_PARMS_INIT);
#endif


	RTH_Test();

#endif

#if TEST_SIMPLE_POINTER_REFERENCES
	TestSimplePointerReferences();
#endif

	RegisterCustomXPathDomain(".refSystem", ProcessRefSystemOverviewIntoStructInfoForHttp, NULL);


}

// Checks the reference system for any invalid data
AUTO_COMMAND ACMD_CATEGORY(Debug);
void RefSystem_CheckIntegrity(void)
{
	int iDictionaryNum;

	PERFINFO_AUTO_START_FUNC();

	for (iDictionaryNum=0; iDictionaryNum < giNumReferenceDictionaries; iDictionaryNum++)
	{
		ReferenceDictionary *pDictionary = &gReferenceDictionaries[iDictionaryNum];
		RefDictIterator iterator;
		ReferenceData pRefData;
		
		PERFINFO_AUTO_START_STATIC(pDictionary->pName, &pDictionary->piVerify, 1);

		RefSystem_InitRefDictIterator(pDictionary->pName, &iterator);


		while ((pRefData = RefSystem_GetNextReferenceDataFromIterator(&iterator)))
		{
			ReferentInfoStruct *pRefInfo;
			int iSize, i;

			assert(RefSystem_FindRefInfoFromRefData(pDictionary, pRefData, &pRefInfo));

			if (pRefInfo->pReferent)
			{
				ReferentInfoStruct *pOtherRefInfo;
				assert(stashFindPointer(gReferentTable, pRefInfo->pReferent, &pOtherRefInfo));
				assert(pOtherRefInfo == pRefInfo);
			}

			if (pDictionary->bReferentsLocked && pRefInfo->pReferent)
			{
				//nothing to do
			}
			else
			{
				iSize = eSetGetMaxSize(&pRefInfo->setHandles);
				for (i = 0; i < iSize; i++)
				{	
					ReferentInfoStruct *pOtherRefInfo;
					void **pRef = eSetGetValueAtIndex(&pRefInfo->setHandles, i);
					if (pRef)
					{				
						assert(*pRef == pRefInfo->pReferent || pRefInfo->pReferent == NULL && *pRef == REFERENT_SET_BUT_ABSENT);
						assert(stashFindPointer(gNormalHandleTable, pRef, &pOtherRefInfo));
						assert(pOtherRefInfo == pRefInfo);
					}
				}			
			}
		}
		
		PERFINFO_AUTO_STOP();
	}

	//walk through referent table, handle tables, make sure every referentInfo was also in a dictionary table
	{
		StashTableIterator stashIterator;
		StashElement element;

		PERFINFO_AUTO_START("referents", 1);
		
		stashGetIterator(gReferentTable, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			ReferentInfoStruct *pInfo = stashElementGetPointer(element);

			assertmsgf(pInfo->pReferent == stashElementGetKey(element), "Key mismatch in referent table: %p and %p",
				pInfo->pReferent, stashElementGetKey(element));
		}
		
		PERFINFO_AUTO_STOP_START("normal handles", 1);

		stashGetIterator(gNormalHandleTable, &stashIterator);

		while (stashGetNextElement(&stashIterator, &element))
		{
			ReferentInfoStruct *pInfo = stashElementGetPointer(element);
			struct ReferenceHandle *pHandle = stashElementGetKey(element);

			if (pInfo->pDictionary->bReferentsLocked && pInfo->pReferent)
			{
				//do nothing
			}
			else
			{
				assert(eSetFind(&pInfo->setHandles, pHandle));
			}
		}
		
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

		
Referent RefSystem_ReferentFromHandle(ConstReferenceHandle *pHandle)
{
	Referent pPointee;
	ReferentInfoStruct *pRefInfo;

	pPointee = (*pHandle);

	if (!stashFindPointer(gNormalHandleTable, pHandle, &pRefInfo))
	{
		if (pPointee == NULL)
		{
			return NULL;
		}
		else
		{
			assertmsg(0, "Handle not in any handle stash table");
		}
	}

	assert(pRefInfo->pReferent == pPointee || pRefInfo->pReferent == NULL && pPointee == REFERENT_SET_BUT_ABSENT);

	if (pRefInfo->pDictionary->pDirectlyDecodeReferenceCB)
	{
		if (pRefInfo->pReferent != pRefInfo->pDictionary->pDirectlyDecodeReferenceCB(pRefInfo->pOpaqueRefData))
		{
			assertmsgf(0, "Nonmatching referents for handle %s\n", RefSystem_StringFromHandle(pHandle));
		}
	}

	if (pRefInfo->pDictionary->bReferentsLocked && pRefInfo->pReferent)
	{
		//do nothing
	}
	else
	{
		assert(eSetFind(&pRefInfo->setHandles, pHandle));
	}
			
	return pRefInfo->pReferent;
}


#if REFSYSTEM_TEST_HARNESS

#if TEST_HARNESS_STRINGVERSION

//from here down, this is stuff for the reference test harness
#define RTH_NUM_OBJECTS 3000
#define RTH_NUM_REFS_PER_OBJECT 100
#define RTH_NUM_SPACES 10000

//for each object, what space it is currenly occupying, or -1 if it is "off"
int sRTHSpaceNums[RTH_NUM_OBJECTS];

struct RTHObject;


typedef struct
{
	REF_TO(struct RTHObject) objRef;
	int iObjectNum;
} RTHSingleReference;

typedef struct RTHObject
{
	bool bActive;
	RTHSingleReference references[RTH_NUM_REFS_PER_OBJECT];
} RTHObject;

static RTHObject sRTHSpace[RTH_NUM_SPACES];

static int sNumActiveRTHObjects;

int FindFreeRTHSpaceNum()
{
	int s = rand() % RTH_NUM_SPACES;

	while (sRTHSpace[s].bActive)
	{
		s = (s + 1 ) % RTH_NUM_SPACES;
	}

	return s;
}

ReferenceData GetTempReferenceToObject(int iObjectNum)
{
	static char tempString[32];
	
	sprintf_s(tempString, 31, "<Object %d>", iObjectNum);	

	return tempString;
}


void RTH_Test(void)
{
	int i, j, count;

	DictionaryHandle dictHandle = RefSystem_GetDictionaryHandleFromNameOrHandle("TestHarness");

	for (i=0; i < RTH_NUM_OBJECTS; i++)
	{
		sRTHSpaceNums[i] = -1;

		for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
		{
			CLEAR_UNATTACHED_REF(sRTHSpace[i].references[j].objRef);
		}
	}

	for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
	{
		sRTHSpace[i].bActive = false;
	}

	sNumActiveRTHObjects = 0;

	for (count=0; count < 1000; count++)
//	while (1)
	{
		int iRand;

		if (rand() % 10000 == 0)
		{
			//delete all objects and then assert that all lists are empty
			for (i=0; i < RTH_NUM_OBJECTS; i++)
			{
				if (sRTHSpaceNums[i] != -1)
				{
					for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
					{
						REMOVE_HANDLE(sRTHSpace[sRTHSpaceNums[i]].references[j].objRef);
					}
					sRTHSpace[sRTHSpaceNums[i]].bActive = false;
					RefSystem_RemoveReferent((&sRTHSpace[sRTHSpaceNums[i]]), false);
					sRTHSpaceNums[i] = -1;
					sNumActiveRTHObjects--;

				}
			}

	
			RefSystem_CheckIntegrity(true);

			DEBUG_PRINTF("Removed all objects... things are shipshape\n");
		}

		//10% add an object, 5% remove an object, 20% remove and add handles, 65% move an object
		iRand = rand() % 100;

		if (iRand < 10) // add an object
		{
			if (sNumActiveRTHObjects < RTH_NUM_OBJECTS)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				int iSpaceNum = FindFreeRTHSpaceNum();
				RTHObject *pObject;

				while (sRTHSpaceNums[iObjectNum] != -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}


				sRTHSpaceNums[iObjectNum] = iSpaceNum;
				
				pObject = &sRTHSpace[iSpaceNum];
				pObject->bActive = true;
			

				sNumActiveRTHObjects++;

				RefSystem_AddReferent(dictHandle, GetTempReferenceToObject(iObjectNum), pObject);

				for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
				{
					pObject->references[i].iObjectNum = rand() % RTH_NUM_OBJECTS;

					if (!SET_HANDLE_FROM_REFDATA("TestHarness", GetTempReferenceToObject(pObject->references[i].iObjectNum), pObject->references[i].objRef))
					{
						assert(0);
					}
				}

				{
					int iTemp = rand() % RTH_NUM_REFS_PER_OBJECT;
					char *pString = REF_STRING_FROM_HANDLE(pObject->references[iTemp].objRef);
					DEBUG_PRINTF("Added object %d. Its reference %d is to %s\n", iObjectNum, iTemp, pString);
				}


			}
		}
		else if (iRand < 15) // remove an object
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}

				sRTHSpace[sRTHSpaceNums[iObjectNum]].bActive = false;
			
				for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
				{
					REMOVE_HANDLE(sRTHSpace[sRTHSpaceNums[iObjectNum]].references[j].objRef);
				}
	
				RefSystem_RemoveReferent(&sRTHSpace[sRTHSpaceNums[iObjectNum]], false);
				sRTHSpaceNums[iObjectNum] = -1;
				sNumActiveRTHObjects--;

				DEBUG_PRINTF("Removed object %d\n", iObjectNum);

			}
		}
		else if (iRand < 35) //remove and add handles
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				RTHObject *pObject;

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}

				pObject = &sRTHSpace[sRTHSpaceNums[iObjectNum]];

				for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
				{
					REMOVE_HANDLE(pObject->references[i].objRef);

					pObject->references[i].iObjectNum = rand() % RTH_NUM_OBJECTS;

					if (!SET_HANDLE_FROM_REFDATA(dictHandle, GetTempReferenceToObject(pObject->references[i].iObjectNum), pObject->references[i].objRef))
					{
						assert(0);
					}
				}
				
				DEBUG_PRINTF("Added and removed handles for object %d\n", iObjectNum);
			}
		}
		else // move an object
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				int iNewSpaceNum = FindFreeRTHSpaceNum();

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}
				
				memcpy(&sRTHSpace[iNewSpaceNum], &sRTHSpace[sRTHSpaceNums[iObjectNum]], sizeof(RTHObject));

				for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
				{
					if (IS_HANDLE_ACTIVE(sRTHSpace[sRTHSpaceNums[iObjectNum]].references[j].objRef))
					{
						MOVE_HANDLE(sRTHSpace[iNewSpaceNum].references[j].objRef,
							sRTHSpace[sRTHSpaceNums[iObjectNum]].references[j].objRef);
					}
				}
	
				RefSystem_MoveReferent(&sRTHSpace[iNewSpaceNum],
					&sRTHSpace[sRTHSpaceNums[iObjectNum]]);


				sRTHSpace[sRTHSpaceNums[iObjectNum]].bActive = false;
				sRTHSpaceNums[iObjectNum] = iNewSpaceNum;

				DEBUG_PRINTF("Moved object %d\n", iObjectNum);
			}
		}

		//now verify correctness
		RefSystem_CheckIntegrity(false);

		for (i=0; i < RTH_NUM_OBJECTS; i++)
		{
			if (sRTHSpaceNums[i] != -1)
			{
				int j;
				RTHObject *pObject = &sRTHSpace[sRTHSpaceNums[i]];

				for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
				{
					RTHObject *pReferent;

					if (sRTHSpaceNums[pObject->references[j].iObjectNum] == -1)
					{
						pReferent = NULL;
					}
					else
					{
						pReferent = &sRTHSpace[sRTHSpaceNums[pObject->references[j].iObjectNum]];
					}
					assert(GET_REF(pObject->references[j].objRef) == pReferent);
				}
			}
		}
	}
}

Referent RTHDirectlyDecodeReference(ConstReferenceData pRefData)
{
	char *pString = (char*)pRefData;
	int iObjNum;

	sscanf(pString, "<Object %d>", &iObjNum);

	if (sRTHSpaceNums[iObjNum] == -1)
	{
		return NULL;
	}

	return &sRTHSpace[sRTHSpaceNums[iObjNum]];
}


#else





//from here down, this is stuff for the reference test harness
#define RTH_NUM_OBJECTS 3000
#define RTH_NUM_REFS_PER_OBJECT 100
#define RTH_NUM_SPACES 10000

//for each object, what space it is currenly occupying, or -1 if it is "off"
int sRTHSpaceNums[RTH_NUM_OBJECTS];

struct RTHObject;

typedef struct
{
	int iObjectNum;
} RTHReferenceData;

typedef struct
{
	REF_TO(struct RTHObject) objRef;
	int iObjectNum;
} RTHSingleReference;

typedef struct RTHObject
{
	bool bActive;
	RTHSingleReference references[RTH_NUM_REFS_PER_OBJECT];
} RTHObject;

static RTHObject sRTHSpace[RTH_NUM_SPACES];

static int sNumActiveRTHObjects;

RTHReferenceData *GetTempReferenceToObject(int iObjectNum)
{
	static RTHReferenceData refData;

	refData.iObjectNum = iObjectNum;

	return &refData;
}


int FindFreeRTHSpaceNum()
{
	int s = rand() % RTH_NUM_SPACES;

	while (sRTHSpace[s].bActive)
	{
		s = (s + 1 ) % RTH_NUM_SPACES;
	}

	return s;
}


void RTH_Test(void)
{
	int i, j;

	for (i=0; i < RTH_NUM_OBJECTS; i++)
	{
		sRTHSpaceNums[i] = -1;

		for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
		{
			CLEAR_UNATTACHED_REF(sRTHSpace[i].references[j].objRef);
		}
	}

	for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
	{
		sRTHSpace[i].bActive = false;
	}

	sNumActiveRTHObjects = 0;

//	for (j=0; j < 100; j++)
	while (1)
	{
		int iRand;

		if (rand() % 10000 == 0)
		{
			//delete all objects and then assert that all lists are empty
			for (i=0; i < RTH_NUM_OBJECTS; i++)
			{
				if (sRTHSpaceNums[i] != -1)
				{
					sRTHSpace[sRTHSpaceNums[i]].bActive = false;
					RefSystem_RemoveBlock(sRTHSpace + sRTHSpaceNums[i], sizeof(RTHObject), false);
					sRTHSpaceNums[i] = -1;
					sNumActiveRTHObjects--;

				}
			}


			BSTSystem_AssertIsEmpty();
	
			RefSystem_CheckIntegrity(true);

			DEBUG_PRINTF("Removed all objects... things are shipshape\n");
		}

		//10% add an object, 5% remove an object, 20% remove and add handles, 65% move an object
		iRand = rand() % 100;

		if (iRand < 10) // add an object
		{
			if (sNumActiveRTHObjects < RTH_NUM_OBJECTS)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				int iSpaceNum = FindFreeRTHSpaceNum();
				RTHObject *pObject;

				while (sRTHSpaceNums[iObjectNum] != -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}


				sRTHSpaceNums[iObjectNum] = iSpaceNum;
				
				pObject = &sRTHSpace[iSpaceNum];
				pObject->bActive = true;
			

				sNumActiveRTHObjects++;

				RefSystem_AddReferent("TestHarness", GetTempReferenceToObject(iObjectNum), pObject);

				for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
				{
					char tempString[32];
					pObject->references[i].iObjectNum = rand() % RTH_NUM_OBJECTS;
					sprintf_s(tempString, 31, "<Object %d>", pObject->references[i].iObjectNum);

					if (!SET_HANDLE_FROM_STRING("TestHarness", tempString, pObject->references[i].objRef))
					{
						assert(0);
					}
				}

				{
					int iTemp = rand() % RTH_NUM_REFS_PER_OBJECT;
					char *pString = REF_STRING_FROM_HANDLE(pObject->references[iTemp].objRef);
					DEBUG_PRINTF("Added an object. Its reference %d is to %s\n", iTemp, pString);	
				}


			}
		}
		else if (iRand < 15) // remove an object
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}

				sRTHSpace[sRTHSpaceNums[iObjectNum]].bActive = false;
				RefSystem_RemoveBlock(sRTHSpace + sRTHSpaceNums[iObjectNum], sizeof(RTHObject), false);
				sRTHSpaceNums[iObjectNum] = -1;
				sNumActiveRTHObjects--;

			}
		}
		else if (iRand < 35) //remove and add handles
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				RTHObject *pObject;

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}

				pObject = &sRTHSpace[sRTHSpaceNums[iObjectNum]];

				for (i=0; i < RTH_NUM_REFS_PER_OBJECT; i++)
				{
					REMOVE_HANDLE(pObject->references[i].objRef);

					pObject->references[i].iObjectNum = rand() % RTH_NUM_OBJECTS;

					if (!SET_HANDLE_FROM_REFDATA("TestHarness", GetTempReferenceToObject(pObject->references[i].iObjectNum), pObject->references[i].objRef))
					{
						assert(0);
					}
				}
			}
		}
		else // move an object
		{
			if (sNumActiveRTHObjects)
			{
				int iObjectNum = rand() % RTH_NUM_OBJECTS;
				int iNewSpaceNum = FindFreeRTHSpaceNum();

				while (sRTHSpaceNums[iObjectNum] == -1)
				{
					iObjectNum = (iObjectNum + 1) % RTH_NUM_OBJECTS;
				}
				
				memcpy(&sRTHSpace[iNewSpaceNum], &sRTHSpace[sRTHSpaceNums[iObjectNum]], sizeof(RTHObject));
				RefSystem_MoveBlock(&sRTHSpace[iNewSpaceNum], &sRTHSpace[sRTHSpaceNums[iObjectNum]], sizeof(RTHObject));
				sRTHSpace[sRTHSpaceNums[iObjectNum]].bActive = false;
				sRTHSpaceNums[iObjectNum] = iNewSpaceNum;
			}
		}

		//now verify correctness
		RefSystem_CheckIntegrity(false);

		for (i=0; i < RTH_NUM_OBJECTS; i++)
		{
			if (sRTHSpaceNums[i] != -1)
			{
				int j;
				RTHObject *pObject = &sRTHSpace[sRTHSpaceNums[i]];

				for (j=0; j < RTH_NUM_REFS_PER_OBJECT; j++)
				{
					RTHObject *pReferent;

					if (sRTHSpaceNums[pObject->references[j].iObjectNum] == -1)
					{
						pReferent = NULL;
					}
					else
					{
						pReferent = &sRTHSpace[sRTHSpaceNums[pObject->references[j].iObjectNum]];
					}
					assert(GET_REF(pObject->references[j].objRef) == pReferent);
				}
			}
		}
	}
}

Referent RTHDirectlyDecodeReference(ConstReferenceData pRefData)
{
	RTHReferenceData *pRTHRefData = (RTHReferenceData*)pRefData;

	int iObjNum = pRTHRefData->iObjectNum;
		
	assert(iObjNum >= 0 && iObjNum < RTH_NUM_OBJECTS);

	if (sRTHSpaceNums[iObjNum] == -1)
	{
		return NULL;
	}

	return &sRTHSpace[sRTHSpaceNums[iObjNum]];
}

ReferenceHashValue RTHGetHashFromReferenceData(ConstReferenceData pRefData)
{
	RTHReferenceData *pRTHRefData = (RTHReferenceData*)pRefData;

	return pRTHRefData->iObjectNum + 1;
}

ReferenceData RTHCopyReferenceData(ConstReferenceData pRefData)
{
	RTHReferenceData *pRTHRefData = (RTHReferenceData*)pRefData;

	RTHReferenceData *pNewRefData = (RTHReferenceData*)malloc(sizeof(RTHReferenceData));

	memcpy(pNewRefData, pRTHRefData, sizeof(RTHReferenceData));
	

	return pNewRefData;
}

void RTHFreeReferenceData(ConstReferenceData pRefData)
{
	free(pRefData);
}

//This function converts a reference data to a string, and APPENDS that string to the EString
bool RTHReferenceDataToString(char **ppEString, ConstReferenceData pRefData)
{
	char tempString[32];
	RTHReferenceData *pRTHRefData = (RTHReferenceData*)pRefData;


	sprintf_s(tempString, 31, "<Object %d>", pRTHRefData->iObjectNum);

	estrConcatString(ppEString, tempString, (int)strlen(tempString));
	return true;
}



//This function converts a string to a reference and returns the reference data, which can 
//be freed by calling RefCallBack_FreeReferenceData
ReferenceData RTHStringToReferenceData(const char *pString)
{
	int iObjectNum;
	RTHReferenceData *pNewRefData;
	int iRetVal = sscanf(pString, "<Object %d>", &iObjectNum);

	if (iRetVal != 1)
	{
		return NULL;
	}
	
	pNewRefData = (RTHReferenceData*)malloc(sizeof(RTHReferenceData));

	pNewRefData->iObjectNum = iObjectNum;


	return pNewRefData;
}
#endif
#endif


#if TEST_SIMPLE_POINTER_REFERENCES
typedef struct fooStruct
{
	int x, y;
} fooStruct;

typedef struct barStruct
{
	REF_TO(fooStruct) hMyFoo;
} barStruct;


void TestSimplePointerReferences()
{
	barStruct myBar;
	fooStruct *pMyFoo;

	CLEAR_UNATTACHED_REF(myBar.hMyFoo);

	printf("myBar's foo = %x\n", GET_REF(myBar.hMyFoo));

	pMyFoo = malloc(sizeof(fooStruct));

	printf("pMyFoo is %x\n", pMyFoo);

	ADD_SIMPLE_POINTER_REFERENCE(myBar.hMyFoo, pMyFoo);

	printf("myBar's foo = %x\n", GET_REF(myBar.hMyFoo));

	RefSystem_RemoveBlock(pMyFoo, sizeof(fooStruct), true);

	printf("myBar's foo = %x\n", GET_REF(myBar.hMyFoo));
}


#endif


//how many "browsable" dictionaries there are. For the code which browses ref dictionaries from the MCP
int RefSystem_GetNumDictionariesForBrowsing(void)
{
	int i;
	int iCount = 0;

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		if (gReferenceDictionaries[i].pParseTable && gReferenceDictionaries[i].bUsesStringsAsReferenceData)
		{
			iCount++;
		}
	}

	return iCount;
}
bool RefSystem_DictionaryCanBeBrowsed(DictionaryHandleOrName dictHandle)
{
	ReferenceDictionary *pDictionary = RefDictionaryFromNameOrHandle(dictHandle);

	return pDictionary && pDictionary->pParseTable && pDictionary->bUsesStringsAsReferenceData;
}


//returns the dictionary name of the nth browsable dictionary
const char *RefSystem_GetNthDictionaryForBrowsingName(int iDictNum)
{
	int i;
	int iCount = 0;

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		if (gReferenceDictionaries[i].pParseTable && gReferenceDictionaries[i].bUsesStringsAsReferenceData)
		{
			if (iCount == iDictNum)
			{
				return gReferenceDictionaries[i].pName;
			}
			iCount++;
		}
	}

	assertmsg(0, "Ref system corruption");

	return "";
}

DictionaryHandle RefSystem_RegisterNullDictionary_dbg(const char *pName
												  MEM_DBG_PARMS)
{
	ReferenceDictionary *pDictionary;

	DictionaryHandle hHandle = RefSystem_RegisterDictionary_dbg(pName,
		NullDictDirectlyDecodeReference,
		NullDictGetHashFromReferenceData,
		NullDictCompareReferenceData,
		NullDictReferenceDataToString,
		NullDictStringToReferenceData,
		NullDictCopyReferenceData,
		NullDictFreeReferenceData, NULL, 0
		MEM_DBG_PARMS_CALL);

	pDictionary = RefDictionaryFromNameOrHandle(pName);
	assert(pDictionary);

	return hHandle;
}



typedef struct
{
	int iScore;
	const char *pString;
} ScoredString;

//assuming iNumToReturn is very small, so just doing insertion sorting
void RefSystem_GetSimilarNames_dbg(DictionaryHandleOrName dictHandle, const char *pInName, int iNumToReturn, const char ***pppOutList
								   MEM_DBG_PARMS)
{
	ReferenceDictionary *pDictionary = RefDictionaryFromNameOrHandle(dictHandle);
	ScoredString *pScoredStrings;
	int iCurCount = 0;
	RefDictIterator iterator;
	ReferentInfoStruct *pRefInfo;
	int i;

	if (!pDictionary || !pDictionary->bUsesStringsAsReferenceData)
	{
		return;
	}

	pScoredStrings = ScratchAlloc(sizeof(ScoredString) * iNumToReturn);

	RefSystem_InitRefDictIterator(dictHandle, &iterator);
	while ((pRefInfo = RefSystem_GetNextReferentInfoFromIterator(&iterator)))
	{
		if (pRefInfo->pReferent)
		{
			int iCurScore = levenshtein_distance(pInName, pRefInfo->pStringRefData);

			if (iCurScore >= 0)
			{
				//find the place that this needs to go right above
				i = iCurCount - 1;

				while (i >= 0 && pScoredStrings[i].iScore > iCurScore)
				{
					i--;
				}

				if (i < iNumToReturn - 1)
				{
					if (iNumToReturn - i - 2 > 0)
					{
						memmove(pScoredStrings + i + 2, pScoredStrings + i + 1, (iNumToReturn - i - 2) * sizeof(ScoredString));
					}
					pScoredStrings[i + 1].iScore = iCurScore;
					pScoredStrings[i + 1].pString = pRefInfo->pStringRefData;

					if (iCurCount < iNumToReturn)
					{
						iCurCount++;
					}
				}
			}
		}
	}
				
	for (i=0; i < iCurCount; i++)
	{
		seaPush(pppOutList, pScoredStrings[i].pString);
	}

	ScratchFree(pScoredStrings);
}

void RefSystem_ReloadFile(const char *pchWhere, S32 iWhen, const char *pchWhat, DictionaryHandle pWhich)
{
	loadstart_printf("Reloading %s... ", pchWhat);
	fileWaitForExclusiveAccess(pchWhere);
	errorLogFileIsBeingReloaded(pchWhere);
	ParserReloadFileToDictionary(pchWhere, pWhich);
	loadend_printf("done. (%d total)", RefSystem_GetDictionaryNumberOfReferents(pWhich));
}


AUTO_COMMAND ACMD_CATEGORY(DEBUG);
char *DumpRefSystemStashTableStats(void)
{
	static char *spOutString = NULL;
	int i;

	estrCopy2(&spOutString, "");
	estrConcatf(&spOutString, "For each table, count elements, max size\r\n");
	estrConcatf(&spOutString, "NormalHandles: %u %u\r\n",
		stashGetCount(gNormalHandleTable), stashGetMaxSize(gNormalHandleTable));
	estrConcatf(&spOutString, "Referents: %u %u\r\n",
		stashGetCount(gReferentTable), stashGetMaxSize(gReferentTable));

	for (i=0; i < giNumReferenceDictionaries; i++)
	{

		StashTableIterator stashIterator;
		StashElement element;
		ReferentInfoStruct *pInfo;
		int iHandleCount = 0;

		estrConcatf(&spOutString, "%s: %u %u\r\n",
			gReferenceDictionaries[i].pName, 
			stashGetCount(gReferenceDictionaries[i].refDataTable), stashGetMaxSize(gReferenceDictionaries[i].refDataTable));



		stashGetIterator(gReferenceDictionaries[i].refDataTable, &stashIterator);

	
		while (stashGetNextElement(&stashIterator, &element))
		{
			pInfo = stashElementGetPointer(element);

			iHandleCount += eSetGetCount(&pInfo->setHandles);
		}

		estrConcatf(&spOutString, "%s: %d handles\n",
			gReferenceDictionaries[i].pName, iHandleCount);
	}

	printf("%s", spOutString);
	return spOutString;
}




void RefSystem_SetResourceDictOnRefDict(const char *refdict_name,ResourceDictionary *pResourceDict)
{
	ReferenceDictionary	*pDictionary;

	pDictionary = RefDictionaryFromNameOrHandle(refdict_name);
	if (pDictionary)
		pDictionary->pResourceDict = pResourceDict;
}

AUTO_STRUCT;
typedef struct StashTableOverview
{
	int iNumEntries;
	int iSize;
	int iWasted;
} StashTableOverview;

AUTO_STRUCT;
typedef struct RefSystemDictionaryOverview
{
	char *pName; 
	StashTableOverview refDataTable; AST(EMBEDDED_FLAT(refData))
	StashTableOverview refPreviousTable; AST(EMBEDDED_FLAT(refPrevious))
} RefSystemDictionaryOverview;

AUTO_STRUCT;
typedef struct RefSystemOverview
{
	StashTableOverview handleTable; AST(EMBEDDED_FLAT(handleTable))
	StashTableOverview referentTable; AST(EMBEDDED_FLAT(referentTable))

	RefSystemDictionaryOverview **ppDictionaries;
} RefSystemOverview;

void FillStashTableOverview(StashTableOverview *pOverview, StashTable table)
{
	pOverview->iNumEntries = stashGetCount(table);
	pOverview->iSize = stashGetMaxSize(table);
	pOverview->iWasted = pOverview->iSize - pOverview->iNumEntries;
}


RefSystemOverview *GetRefSystemOverview(void)
{
	static RefSystemOverview overview = {0};
	int i;
	StructReset(parse_RefSystemOverview, &overview);

	FillStashTableOverview(&overview.handleTable, gNormalHandleTable);
	FillStashTableOverview(&overview.referentTable, gReferentTable);

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		RefSystemDictionaryOverview *pDictOverview = StructCreate(parse_RefSystemDictionaryOverview);

		pDictOverview->pName = strdup(gReferenceDictionaries[i].pName);

		FillStashTableOverview(&pDictOverview->refDataTable, gReferenceDictionaries[i].refDataTable);
		if (gReferenceDictionaries[i].refPreviousTable)
		{
			FillStashTableOverview(&pDictOverview->refPreviousTable, gReferenceDictionaries[i].refPreviousTable);
		}

		eaPush(&overview.ppDictionaries, pDictOverview);
	}


	return &overview;
}


bool ProcessRefSystemOverviewIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	bool bRetVal;
	RefSystemOverview *pInfo = GetRefSystemOverview();

	bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
		pInfo, parse_RefSystemOverview, iAccessLevel, 0, pStructInfo, eFlags);

	return bRetVal;
}


void RefSystem_ResizeAllStashTables(void)
{
	int i;
	stashTableFindCurrentBestSize(gReferentTable);

	for (i=0; i < giNumReferenceDictionaries; i++)
	{
		stashTableFindCurrentBestSize(gReferenceDictionaries[i].refDataTable);
	}
}

bool RefSystem_IsReferentStringValid(DictionaryHandleOrName dictHandle, const char *pString)
{
	char ns[RESOURCE_NAME_MAX_SIZE];

	if(!SAFE_DEREF(pString)){
		return true;
	}
	
	ns[0] = 0;

	if( resExtractNameSpace_s( pString, SAFESTR(ns), NULL, 0 )) {
		if( !resNameSpaceGetByName( ns )) {
			return true;
		}
	}

	return RefSystem_ReferentFromString( dictHandle, pString ) != NULL;
}

#include "ReferenceSystem_c_ast.c"
