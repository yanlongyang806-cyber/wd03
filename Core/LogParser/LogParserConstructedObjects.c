#include "LogParserConstructedObjects.h"
#include "LogParserConstructedObjects_h_ast.h"
#include "LogParserConstructedObjects_c_ast.h"
#include "estring.h"
#include "LogParsing_h_ast.h"
#include "stringCache.h"
#include "StringUtil.h"
#include "hashFunctions.h"
#include "objPath.h"
#include "Expression.h"
#include "timing.h"
#include "NameValuePair.h"
#include "objPath_h_ast.h"
#include "WorldGrid.h"
#include "MapDescription_h_ast.h"
#include "GlobalEnums_h_ast.h"
#include "TimedCallback.h"
#include "cmdParse.h"
#include "wininclude.h"
#include "HashFunctions.h"

typedef struct LPCOProceduralLogToProcessSoon
{
	LPCOSimpleProceduralLogDef *pLogDef;
	LogParserConstructedObject *pLPCO;
} LPCOProceduralLogToProcessSoon;

typedef struct LPCOTypeProceduralLogToProcessSoon
{
	LPCOTypeTimedBroadcastInput *pTimedBroadcastInput;
	StashTable pLPCOTypeTable;
} LPCOTypeProceduralLogToProcessSoon;

static LPCOTypeProceduralLogToProcessSoon **sppLPCOTypeProceduralLogsToProcess = NULL;
static LPCOProceduralLogToProcessSoon **sppLPCOProceduralLogsToProcess = NULL;
static LogParserConstructedObject **sppLPCOsWhichNeedTheParsingCategoryCallback = NULL;
static LogParserConstructedObject **sppLPCOsWhichNeedProceduralDataRecalc = NULL;
void AddOrUpdateDataInNameValuePairList(NameValuePair ***pppList, const char *pDataName, const char *pDataValue);
void AddPreexistingLPCOToTables(LogParserConstructedObject *pLPCO, const char *pLPCOTypeName /*pooled*/);


//__CATEGORY stuff for the log parser
//(seconds) How many seconds at a time to spend writing out LPCOs
int iPeriodicLPCOWrite_MaxLength = 1;
AUTO_CMD_INT(iPeriodicLPCOWrite_MaxLength, PeriodicLPCOWrite_MaxLength) ACMD_AUTO_SETTING(LogParser, LOGPARSER);

//(seconds) how often to spend some time writing out LPCOs
int iPeriodicLPCOWrite_Interval = 15;
AUTO_CMD_INT(iPeriodicLPCOWrite_Interval, PeriodicLPCOWrite_Interval) ACMD_AUTO_SETTING(LogParser, LOGPARSER);

AUTO_STRUCT;
typedef struct ListOfLPCOsForLoading
{
	LogParserConstructedObject **ppObject;
} ListOfLPCOsForLoading;






ParseTable *GetDuplicateTPIForLPCO(char *pObjTypeName);

StashTable sAllLPCOTypes = NULL;

static int sDirtyInt = 1;

static const char *spGameServerPooled = NULL;

//if this is set, then LPCOs will be read in at startup time, and periodically and in batches written out during running
static char sDirForLPCOs[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(sDirForLPCOs, DirForLPCOs);

static void WriteOutLPCO_Periodic(LogParserConstructedObject *pLPCO)
{
	char fileName[CRYPTIC_MAX_PATH];
	static ListOfLPCOsForLoading *spList = NULL;

	if (!pLPCO->pPeriodicWriteFileName)
	{
		U32 iHash = hashString(pLPCO->pName, true);
		char *pTemp = NULL;
		estrCopy2(&pTemp, pLPCO->pName);
		estrMakeAllAlphaNumAndUnderscores(&pTemp);

		sprintf(fileName, "%s\\%s\\%u\\%u\\%s.lpco", sDirForLPCOs, pLPCO->pTypeName, iHash % 0xff, (iHash >> 16) % 0xff, pTemp);
		pLPCO->pPeriodicWriteFileName = strdup(fileName);
		mkdirtree_const(fileName);
	}

	if (!spList)
	{
		spList = StructCreate(parse_ListOfLPCOsForLoading);
	}

	eaPush(&spList->ppObject, pLPCO);
	ParserWriteTextFile(fileName, parse_ListOfLPCOsForLoading, spList, 0, 0);
	eaRemove(&spList->ppObject, 0);
}

static void ReadPeriodicWrittenLPCOs(void)
{
	ListOfLPCOsForLoading *pList = StructCreate(parse_ListOfLPCOsForLoading);
	ParserLoadFiles(sDirForLPCOs, ".lpco", NULL, 0, parse_ListOfLPCOsForLoading, pList);
	
	FOR_EACH_IN_EARRAY(pList->ppObject, LogParserConstructedObject, pObject)
	{
		AddPreexistingLPCOToTables(pObject, pObject->pTypeName);
	}
	FOR_EACH_END;

	eaDestroy(&pList->ppObject);
	StructDestroy(parse_ListOfLPCOsForLoading, pList);
}



	



static void PeriodicWriteOutLPCOs(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	static StashTableIterator sListIterator = {0};
	static StashTableIterator sObjectIterator = {0};

	static ListOfLPCOsForLoading *spList = NULL;
	static StashTable sCurTypeTable = NULL;

	S64 iStopTime = timeGetTime() + iPeriodicLPCOWrite_MaxLength * 1000;
	StashElement outerElement;
	StashElement innerElement;
	int iCounter = 0;

	if (!sAllLPCOTypes)
	{
		return;
	}

	if (!spList)
	{
		spList = StructCreate(parse_ListOfLPCOsForLoading);
	}

	while (1)
	{
		if (!sListIterator.pTable)
		{
			stashGetIterator(sAllLPCOTypes, &sListIterator);
		}

		if (!sObjectIterator.pTable)
		{
			if (!stashGetNextElement(&sListIterator, &outerElement))
			{
				//no more types, always stop now
				sListIterator.pTable = NULL;
				return;
			}

			sCurTypeTable = stashElementGetPointer(outerElement);
			stashGetIterator(sCurTypeTable, &sObjectIterator);
		}
	
		while (stashGetNextElement(&sObjectIterator, &innerElement))
		{
			LogParserConstructedObject *pObject = stashElementGetPointer(innerElement);

			if (pObject->bDirtyBit)
			{
				WriteOutLPCO_Periodic(pObject);
				pObject->bDirtyBit = false;
			}

			if (iCounter++ % 512 == 0 && timeGetTime() > iStopTime)
			{
				return;
			}
		}

		sObjectIterator.pTable = NULL;
		
	}
}







AUTO_RUN;
void LPCO_AutoSetup(void)
{
	spGameServerPooled = allocAddString("GameServer");
}


AUTO_EXPR_FUNC(util) ACMD_NAME(StrToFloat);
float LPCOTrackedValue_StrToFloat(const char *pString)
{
	float fVal = 0;
	if (StringToFloat(pString, &fVal))
	{
		return fVal;
	}
	return 0.0f;
}

AUTO_EXPR_FUNC(util) ACMD_NAME(StrToInt);
int LPCOTrackedValue_StrToInt(const char *pString)
{
	return atoi(pString);
}


AUTO_EXPR_FUNC(util) ACMD_NAME(FloatToStr);
char *LPCOTrackedValue_FloatToStr(ExprContext *pContext, float f)
{
	char *pTemp = exprContextAllocScratchMemory(pContext, 64);
	snprintf_s(pTemp, 64, "%f", f);
	return pTemp;
}


static StashTable gMapNameToType = NULL;

AUTO_EXPR_FUNC(util) ACMD_NAME(MapTypeFromMapName);
const char *LPCO_MapTypeFromMapName(const char *pMapName)
{
	const char *pTypeName;

	if (!pMapName || !pMapName[0])
	{
		return "none";
	}

	if(!gMapNameToType)
	{
		gMapNameToType = stashTableCreateWithStringKeys(10, StashDeepCopyKeys);
	}

	PERFINFO_AUTO_START_FUNC();

	if(!stashFindPointer(gMapNameToType, pMapName, (char **)&pTypeName))
	{
		pTypeName = StaticDefineIntRevLookup(ZoneMapTypeEnum, zmapInfoGetMapType(worldGetZoneMapByPublicName(pMapName)));
		stashAddPointer(gMapNameToType, pMapName, pTypeName, false);
	}

	PERFINFO_AUTO_STOP();

	return pTypeName;
}

// Returns 1 if the string is not empty, 0 otherwise
AUTO_EXPR_FUNC(util) ACMD_NAME(IsNonEmpty);
int exprFuncIsNonEmpty(ExprContext* context, const char *s)
{
	return (s && s[0]);
}

static __forceinline void SetLPCOWasModified(LogParserConstructedObject *pLPCO)
{
	pLPCO->bDirtyBit = true;

	if (pLPCO->iDirtyInt == sDirtyInt)
	{
		return;
	}

	pLPCO->iDirtyInt = sDirtyInt;

	if (pLPCO->pProceduralData)
	{
		eaPush(&sppLPCOsWhichNeedProceduralDataRecalc, pLPCO);
	}

	if (pLPCO->bParsingCategorySetDirectly)
	{
		eaPush(&sppLPCOsWhichNeedTheParsingCategoryCallback, pLPCO);
	}
}



void LPCOType_SetProceduralLogToBeLoggedSoon(StashTable pLPCOTypeTable, LPCOTypeTimedBroadcastInput *pInput)
{
	LPCOTypeProceduralLogToProcessSoon *pLogToProcess = malloc(sizeof(LPCOProceduralLogToProcessSoon));
	pLogToProcess->pLPCOTypeTable = pLPCOTypeTable;
	pLogToProcess->pTimedBroadcastInput = pInput;

	eaPush(&sppLPCOTypeProceduralLogsToProcess, pLogToProcess);
}

void LPCO_SetProceduralLogToBeLoggedSoon(LogParserConstructedObject *pLPCO, LPCOSimpleProceduralLogDef *pDef)
{
	LPCOProceduralLogToProcessSoon *pLogToProcess = malloc(sizeof(LPCOProceduralLogToProcessSoon));
	pLogToProcess->pLPCO = pLPCO;
	pLogToProcess->pLogDef = pDef;

	eaPush(&sppLPCOProceduralLogsToProcess, pLogToProcess);
}

bool GameServerLPCOMatchesFilter(LogParserConstructedObject *pObj, LogParserGameServerFilter *pFilter)
{
	NameValuePair *pPair;
	char *pMapName = NULL;
	char *pMachineName = NULL;
	int i;



	if (eaSize(&pFilter->ppMapNames))
	{
		bool bFound = false;

		pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "MapName");
		if (pPair)
		{
			pMapName = pPair->pValue;
		}

		if (!pMapName)
		{
			return -1;
		}

		for (i=0; i < eaSize(&pFilter->ppMapNames); i++)
		{
			if (stricmp(pFilter->ppMapNames[i]->pMapName, "ALL") == 0 
				|| stricmp(pFilter->ppMapNames[i]->pMapName, pMapName) == 0)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			return 0;
		}
	}

	if (eaSize(&pFilter->ppMachineNames))
	{
		bool bFound = false;

		pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "MachineName");
		if (pPair)
		{
			pMachineName = pPair->pValue;
		}
		else
		{
			return -1;
		}

		for (i=0; i < eaSize(&pFilter->ppMachineNames); i++)
		{
			if (stricmp(pFilter->ppMachineNames[i]->pMachineName, pMachineName) == 0)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			return 0;
		}
	}

	if (pFilter->eMapType != ZMTYPE_UNSPECIFIED)
	{
		ZoneMapType eMyType;
		pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "MapType");
		if (pPair)
		{
			eMyType = StaticDefineIntGetInt(ZoneMapTypeEnum, pPair->pValue);
			if (eMyType != pFilter->eMapType)
			{
				return 0;
			}
		}
		else
		{
			return -1;
		}
	}

	if (pFilter->iMinPlayers || pFilter->iMaxPlayers)
	{
		LPCODataGroup *pGroup = eaIndexedGetUsingString(&pObj->ppDataGroups, "NumPlayers");
		
		if (!pGroup)
		{
			return -1;
		}

		if (pGroup->fMostRecentVal < pFilter->iMinPlayers || pGroup->fMostRecentVal > pFilter->iMaxPlayers)
		{
			return 0;
		}
	}

	if (pFilter->iMinEntities || pFilter->iMaxEntities)
	{
		LPCODataGroup *pGroup = eaIndexedGetUsingString(&pObj->ppDataGroups, "NumEntities");
		
		if (!pGroup)
		{
			return -1;
		}

		if (pGroup->fMostRecentVal < pFilter->iMinEntities || pGroup->fMostRecentVal > pFilter->iMaxEntities)
		{
			return 0;
		}
	}

	return 1;
}

//returns true if this type of log is handled
bool LPCO_ParsingCategoryCallback(LogParserConstructedObject *pLPCO)
{
	if (pLPCO->pTypeName == spGameServerPooled)
	{
		int i;

		for (i=0; i < eaSize(&gStandAloneOptions.ppExclusionGameServerFilters); i++)
		{
			switch (GameServerLPCOMatchesFilter(pLPCO, gStandAloneOptions.ppExclusionGameServerFilters[i]))
			{
			case -1:
			case 1:
				AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, "ParsingCategory", BASECATEGORY_UNKNOWN);
				return true;
			}
		}

		for (i=0; i < eaSize(&gStandAloneOptions.ppGameServerFilterCategories); i++)
		{
			switch (GameServerLPCOMatchesFilter(pLPCO, gStandAloneOptions.ppGameServerFilterCategories[i]))
			{
			case -1:
				AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, "ParsingCategory", BASECATEGORY_UNKNOWN);
				return true;
			case 1:
				AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, "ParsingCategory", gStandAloneOptions.ppGameServerFilterCategories[i]->pFilterName);
				return true;

			}
		}


		AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, "ParsingCategory", BASECATEGORY_OTHER);
		return true;
	}

	return false;
}

void LPCO_UpdateProceduralData(LogParserConstructedObject *pLPCO)
{
	int i;
	
	static ExprContext *pExprContext = NULL;
	static ExprFuncTable *exprFuncTable;

	if (!pLPCO->pProceduralData)
	{
		return;
	}

	if (!pExprContext)
	{
		exprFuncTable = exprContextCreateFunctionTable();
		exprContextAddFuncsToTableByTag(exprFuncTable, "util");
		exprContextAddFuncsToTableByTag(exprFuncTable, "lpco");
		exprContextAddFuncsToTableByTag(exprFuncTable, "LpcoTrackedValue");

		pExprContext = exprContextCreate();

		exprContextSetUserPtrIsDefault(pExprContext, true);


	}

	exprContextSetUserPtr(pExprContext, pLPCO, parse_LogParserConstructedObject);
	exprContextSetPointerVar(pExprContext, "me", pLPCO, parse_LogParserConstructedObject, false, true);

	for (i=0; i < eaSize(&pLPCO->pProceduralData->ppList); i++)
	{
		LPCOProceduralSimpleDataInput *pProcData = pLPCO->pProceduralData->ppList[i];
		MultiVal answer = {0};
		char *pTempString;
		estrStackCreate(&pTempString);

		if (!pProcData->pExpression)
		{
			pProcData->pExpression = exprCreate();
			exprGenerateFromString(pProcData->pExpression, pExprContext, pProcData->pExpressionString, NULL);
		}


		exprContextSetSilentErrors(pExprContext, true);
		exprEvaluate(pProcData->pExpression, pExprContext, &answer);

		if (answer.type == MULTI_INVALID)
		{
			MultiValSetInt(&answer, 0);
		}


		MultiValToEString(&answer, &pTempString);

		AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, pProcData->pDataNameInLPCO, pTempString);
		
		estrDestroy(&pTempString);
	
	}
}



void LPCOSystem_PostLogProcessUpdate(U32 iTime)
{
	int i;
	static const char *pTimedCallbackString = NULL;

	if(!pTimedCallbackString)
	{
		pTimedCallbackString = allocAddString("LPCOTimedCallback");
	}

	PERFINFO_AUTO_START_FUNC();

	for (i=0; i < eaSize(&sppLPCOsWhichNeedTheParsingCategoryCallback); i++)
	{
		LPCO_ParsingCategoryCallback(sppLPCOsWhichNeedTheParsingCategoryCallback[i]);
	}

	eaDestroy(&sppLPCOsWhichNeedTheParsingCategoryCallback);

	for (i=0; i < eaSize(&sppLPCOsWhichNeedProceduralDataRecalc); i++)
	{
		LPCO_UpdateProceduralData(sppLPCOsWhichNeedProceduralDataRecalc[i]);
	}

	eaDestroy(&sppLPCOsWhichNeedProceduralDataRecalc);


	sDirtyInt++;

	for (i=0; i < eaSize(&sppLPCOProceduralLogsToProcess); i++)
	{
		LogParserConstructedObject *pLPCO = sppLPCOProceduralLogsToProcess[i]->pLPCO;
		LPCOSimpleProceduralLogDef *pLogDef = sppLPCOProceduralLogsToProcess[i]->pLogDef;

		char *pFixedUpLogString = NULL;



		if (Graph_objPathGetEString(pLogDef->pLogString, parse_LogParserConstructedObject, pLPCO, NULL, NULL,
					  &pFixedUpLogString, NULL))
		{
			ParsedLog *pNewLog;
			NameValuePair *pPair;

			pNewLog = StructCreate(parse_ParsedLog);
	
			pPair = eaIndexedGetUsingString(&pLPCO->ppSimpleData, "ParsingCategory");
			if (pPair)
			{
				pNewLog->pParsingCategory = (char*)allocAddString(pPair->pValue);
			}
			else
			{
				pNewLog->pParsingCategory = BASECATEGORY_UNKNOWN;
			}
			
			pNewLog->iTime = iTime;

			GetNameValuePairsFromString(pFixedUpLogString, &pNewLog->ppPairs, NULL);

			pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
			pNewLog->pObjInfo->pAction = pLogDef->pActionName;

			LogParser_AddProceduralLog(pNewLog, pFixedUpLogString);
		}

		estrDestroy(&pFixedUpLogString);
	}
	eaDestroyEx(&sppLPCOProceduralLogsToProcess, NULL);

	for (i=0; i < eaSize(&sppLPCOTypeProceduralLogsToProcess); i++)
	{
		StashTable pLPCOTypeTable = sppLPCOTypeProceduralLogsToProcess[i]->pLPCOTypeTable;
		LPCOTypeTimedBroadcastInput *pTimedBroadcastInput = sppLPCOTypeProceduralLogsToProcess[i]->pTimedBroadcastInput;

		char *pFixedUpLogString = NULL;
		ParsedLog *pNewLog;

		pNewLog = StructCreate(parse_ParsedLog);
		estrStackCreate(&pFixedUpLogString);
	
		estrPrintf(&pFixedUpLogString, "LPCOTypeName %s Count %d CallbackName %s", pTimedBroadcastInput->pLPCOTypeName, stashGetOccupiedSlots(pLPCOTypeTable), pTimedBroadcastInput->pCallbackName);

		pNewLog->pParsingCategory = BASECATEGORY_UNKNOWN;

		pNewLog->iTime = iTime;
		GetNameValuePairsFromString(pFixedUpLogString, &pNewLog->ppPairs, NULL);
		pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
		pNewLog->pObjInfo->pAction = pTimedCallbackString;

		LogParser_AddProceduralLog(pNewLog, pFixedUpLogString);

		estrDestroy(&pFixedUpLogString);
	}
	for (i=0; i < eaSize(&sppLPCOTypeProceduralLogsToProcess); i++)
	{
		if(sppLPCOTypeProceduralLogsToProcess[i]->pTimedBroadcastInput->bClear)
			stashTableClear(sppLPCOTypeProceduralLogsToProcess[i]->pLPCOTypeTable);
	}
	eaDestroyEx(&sppLPCOTypeProceduralLogsToProcess, NULL);
	PERFINFO_AUTO_STOP();
}

LogParserConstructedObject *FindExistingLPCOFromNameAndTypeName(const char *pLPCOName /*not necessarily pooled*/, const char *pLPCOTypeName /*pooled*/)
{
	StashTable singleTypeTable = NULL;
	LogParserConstructedObject *pLPCO;

	if (!sAllLPCOTypes)
	{
		return NULL;
	}

	if (!stashFindPointer(sAllLPCOTypes, pLPCOTypeName, &singleTypeTable))
	{
		return NULL;
	}

	if (stashFindPointer(singleTypeTable, pLPCOName, &pLPCO))
	{
		return pLPCO;
	}

	return NULL;
}


LPCOProceduralDataList *LPCO_FindProceduralDataList(LogParserConstructedObject *pLPCO)
{
	static StashTable sProceduralDataListTable = NULL;
	LPCOProceduralDataList *pList;

	if (!sProceduralDataListTable)
	{
		int i;

		sProceduralDataListTable = stashTableCreateAddress(16);

		for (i=0; i < eaSize(&gLogParserConfig.ppLPCOProceduralSimpleDatas); i++)
		{

			if (!stashFindPointer(sProceduralDataListTable, gLogParserConfig.ppLPCOProceduralSimpleDatas[i]->pLPCOTypeName, &pList))
			{
				pList = StructCreate(parse_LPCOProceduralDataList);
				stashAddPointer(sProceduralDataListTable, gLogParserConfig.ppLPCOProceduralSimpleDatas[i]->pLPCOTypeName, pList, false);
			}

			eaPush(&pList->ppList, gLogParserConfig.ppLPCOProceduralSimpleDatas[i]);
		}
	}

	if (stashFindPointer(sProceduralDataListTable, pLPCO->pTypeName, &pList))
	{
		return pList;
	}

	return NULL;
}

StashTable GetLPCOTypeTableFromTypeName(char *pLPCOTypeName /*pooled*/)
{
	StashTable singleTypeTable = NULL;

	if (!sAllLPCOTypes)
	{
		sAllLPCOTypes = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(sAllLPCOTypes, pLPCOTypeName, &singleTypeTable))
	{
		char resourceName[256];
		sprintf(resourceName, "Logged_%s", pLPCOTypeName);
		singleTypeTable = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
		resRegisterDictionaryForStashTable(resourceName, RESCATEGORY_OTHER, 0, singleTypeTable, GetDuplicateTPIForLPCO(resourceName));
		stashAddPointer(sAllLPCOTypes, pLPCOTypeName, singleTypeTable, false);
	}
	return singleTypeTable;
}

LogParserConstructedObject *GetLPCOFromNameAndTypeName(char *pLPCOName /*not necessarily pooled*/, char *pLPCOTypeName /*pooled*/)
{
	StashTable singleTypeTable = NULL;
	LogParserConstructedObject *pLPCO;

	singleTypeTable = GetLPCOTypeTableFromTypeName(pLPCOTypeName);

	if (stashFindPointer(singleTypeTable, pLPCOName, &pLPCO))
	{
		return pLPCO;
	}

	pLPCO = StructCreate(parse_LogParserConstructedObject);
	pLPCO->pName = allocAddString(pLPCOName);
	pLPCO->pTypeName = pLPCOTypeName;
	pLPCO->bParsingCategorySetDirectly = LPCO_ParsingCategoryCallback(pLPCO);
	stashAddPointer(singleTypeTable, pLPCO->pName, pLPCO, false);
	pLPCO->pProceduralData = LPCO_FindProceduralDataList(pLPCO);

	return pLPCO;
}

void AddPreexistingLPCOToTables(LogParserConstructedObject *pLPCO, const char *pLPCOTypeName /*pooled*/)
{
	StashTable singleTypeTable = NULL;

	if (!sAllLPCOTypes)
	{
		sAllLPCOTypes = stashTableCreateAddress(16);
	}

	if (!stashFindPointer(sAllLPCOTypes, pLPCOTypeName, &singleTypeTable))
	{
		char resourceName[256];
		sprintf(resourceName, "Logged_%s", pLPCOTypeName);
		singleTypeTable = stashTableCreateWithStringKeys(16, StashDeepCopyKeys);
		resRegisterDictionaryForStashTable(resourceName, RESCATEGORY_OTHER, 0, singleTypeTable, GetDuplicateTPIForLPCO(resourceName));
		stashAddPointer(sAllLPCOTypes, pLPCOTypeName, singleTypeTable, false);
	}

	stashAddPointer(singleTypeTable, pLPCO->pName, pLPCO, false);
}


void AddStringToLPCOStringGroup(LogParserConstructedObject *pLPCO, char *pStringGroupName, char *pString)
{
	int i;

	LPCOStringGroup *pGroup = NULL;

	const char *pPooledStringGroupName = allocAddString(pStringGroupName);
	LPCOStringGroupMember *pMember;
	U32 iNewStringHash = hashStringInsensitive(pString);

	for (i=0; i < eaSize(&pLPCO->ppStringGroups); i++)
	{
		if (pLPCO->ppStringGroups[i]->pName == pPooledStringGroupName)
		{
			pGroup = pLPCO->ppStringGroups[i];
			break;
		}
	}
	
	if (!pGroup)
	{
		pGroup = StructCreate(parse_LPCOStringGroup);
		pGroup->pName = pPooledStringGroupName;

		eaPush(&pLPCO->ppStringGroups, pGroup);
	}

	if ((pMember = eaIndexedGetUsingString(&pGroup->ppMembers, pString)))
	{	
		pMember->iCount++;
		return;
	}



	pMember = StructCreate(parse_LPCOStringGroupMember);
	pMember->pString = strdup(pString);
	pMember->iCount = 1;

	eaPush(&pGroup->ppMembers, pMember);
	pGroup->iCount = eaSize(&pGroup->ppMembers);
}

void AddFloatToLPCODataGroup(LogParserConstructedObject *pLPCO, char *pDataGroupName, float fVal)
{
	int i;

	LPCODataGroup *pGroup = NULL;

	const char *pPooledDataGroupName = allocAddString(pDataGroupName);


	for (i=0; i < eaSize(&pLPCO->ppDataGroups); i++)
	{
		if (pLPCO->ppDataGroups[i]->pName == pPooledDataGroupName)
		{
			pGroup = pLPCO->ppDataGroups[i];
			break;
		}
	}
	
	if (!pGroup)
	{
		pGroup = StructCreate(parse_LPCODataGroup);
		pGroup->pName = pPooledDataGroupName;

		pGroup->iCount = 0; //default is -1 so that nonexistant data sticks out. Thus have to initialize count and sum to 0
		pGroup->fSum = 0;

		eaPush(&pLPCO->ppDataGroups, pGroup);
	}

	pGroup->iCount++;
	pGroup->fSum += fVal;
	pGroup->fAverage = pGroup->fSum / pGroup->iCount;
	pGroup->fMostRecentVal = fVal;
}


void LPCO_AddStringGroupToLPCO(LPCOStringGroupInput *pStringGroup, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{

		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pStringToAdd = NULL;
		char *pStringToAddResult = NULL;


		LogParserConstructedObject *pLPCO;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);
		estrStackCreate(&pStringToAdd);
		estrStackCreate(&pStringToAddResult);

		if (!Graph_objPathGetEString(pStringGroup->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pStringGroup->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto AddStringFailed;
		}

		if (!Graph_objPathGetEString(pStringGroup->pStringToAdd, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pStringToAdd, &pStringToAddResult))
		{
			Errorf("Invalid xpath syntax in LPCO string to add %s (error string: %s)", pStringGroup->pStringToAdd, pStringToAddResult ? pStringToAddResult : "none");
			
			goto AddStringFailed;
		}

		if (!pStringToAdd || !pStringToAdd[0])
		{
			estrPrintf(&pStringToAdd, "(empty)");
		}
		else
		{
			estrReplaceOccurrences(&pStringToAdd, "\"", "(quote)");
			estrTrimLeadingAndTrailingWhitespace(&pStringToAdd);
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pStringGroup->pLPCOTypeName);

		AddStringToLPCOStringGroup(pLPCO, pStringGroup->pStringGroupName, pStringToAdd);

		SetLPCOWasModified(pLPCO);


	AddStringFailed:
		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);
		estrDestroy(&pStringToAdd);
		estrDestroy(&pStringToAddResult);
	}

	PERFINFO_AUTO_STOP();
}

void LPCO_AddDataGroupToLPCO(LPCODataGroupInput *pDataGroup, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{

		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pFloatToAdd = NULL;
		char *pFloatToAddResult = NULL;

		float fVal;
		int i;
		bool bFoundAlias = false;
	
		LogParserConstructedObject *pLPCO;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);
		estrStackCreate(&pFloatToAdd);
		estrStackCreate(&pFloatToAddResult);


		if (!Graph_objPathGetEString(pDataGroup->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pDataGroup->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto AddDataFailed;
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pDataGroup->pLPCOTypeName);

		if (pDataGroup->pDataField && pDataGroup->pDataField[0])
		{
			if (!Graph_objPathGetEString(pDataGroup->pDataField, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pFloatToAdd, &pFloatToAddResult))
			{
				Errorf("Invalid xpath syntax in LPCO data to add %s (error string: %s)", pDataGroup->pDataField, pFloatToAddResult ? pFloatToAddResult : "none");
				
				goto AddDataFailed;
			}

			//check for aliases
			for (i=0; i < eaSize(&pDataGroup->ppAliases); i++)
			{
				if (stricmp(pDataGroup->ppAliases[i]->pName, pFloatToAdd) == 0)
				{
					fVal = pDataGroup->ppAliases[i]->fVal;
					bFoundAlias = true;
					break;
				}
			}


			if (!bFoundAlias)
			{
				if (!StringToFloat(pFloatToAdd, &fVal))
				{
					if (!StringIsAllWhiteSpace(pFloatToAdd) || pDataGroup->bErrorOnEmptyInput)
					{
						Errorf("Bad float syntax in LPCO data add: %s", pFloatToAdd);
					}
					goto AddDataFailed;
				}
			}
		}
		else if (pDataGroup->pDataExpressionString && pDataGroup->pDataExpressionString[0])
		{
			static ExprContext *spExprContext = NULL;
			MultiVal answer = {0};

			if (!spExprContext)
			{
				ExprFuncTable* exprFuncTable = exprContextCreateFunctionTable();

				spExprContext = exprContextCreate();
				exprContextAddFuncsToTableByTag(exprFuncTable, "util");
				exprContextAddFuncsToTableByTag(exprFuncTable, "LPCOTrackedValue");

				exprContextSetFuncTable(spExprContext, exprFuncTable);

				// Make invalid XPaths resolve as empty strings.
				exprContextAllowInvalidPaths(spExprContext, true);
			}

			exprContextSetPointerVar(spExprContext, "lpco", pLPCO, parse_LogParserConstructedObject, false, true);
			exprContextSetPointerVar(spExprContext, "log", pLog, parse_ParsedLog, false, true);

			if (!pDataGroup->pDataExpression)
			{
				exprContextSetSilentErrors(spExprContext, false);

				pDataGroup->pDataExpression = exprCreate();
				exprGenerateFromString(pDataGroup->pDataExpression, spExprContext, pDataGroup->pDataExpressionString, NULL);
			
				exprContextSetSilentErrors(spExprContext, true);
			}



			exprEvaluate(pDataGroup->pDataExpression, spExprContext, &answer);
	

			fVal = MultiValGetFloat(&answer, NULL);

		}
		else
		{
			fVal = 1.0f;
		}



		AddFloatToLPCODataGroup(pLPCO, pDataGroup->pDataGroupName, fVal);

		SetLPCOWasModified(pLPCO);

		for (i=0; i < eaSize(&pDataGroup->ppProceduralLogs); i++)
		{
			LPCO_SetProceduralLogToBeLoggedSoon(pLPCO, pDataGroup->ppProceduralLogs[i]);
		}


	AddDataFailed:
		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);
		estrDestroy(&pFloatToAdd);
		estrDestroy(&pFloatToAddResult);
	}
	PERFINFO_AUTO_STOP();
}

void AddOrUpdateDataInNameValuePairList(NameValuePair ***pppList, const char *pDataName, const char *pDataValue)
{
	NameValuePair *pCurData;
	int iCurIndex = eaIndexedFindUsingString(pppList, pDataName);
	
	assert(pDataName);

	if (!pDataValue)
	{
		pDataValue = "";
	}


	if (iCurIndex >= 0)
	{
		int iOldLen;
		int iNewLen;

		pCurData = (*pppList)[iCurIndex];

		if (!pCurData->pValue)
		{
			pCurData->pValue = strdup(pDataValue);
		}
		else
		{

			if (strcmp(pDataValue, pCurData->pValue) == 0)
			{
				return;
			}

			iOldLen = (int)strlen(pCurData->pValue);
			iNewLen = (int)strlen(pDataValue);

			if (iNewLen > iOldLen)
			{
				SAFE_FREE(pCurData->pValue);
				pCurData->pValue = malloc(iNewLen + 1);
			}

			memcpy(pCurData->pValue, pDataValue, iNewLen + 1);
		}
	}
	else
	{
		pCurData = StructCreate(parse_NameValuePair);

		pCurData->pName = strdup(pDataName);
		pCurData->pValue = strdup(pDataValue);

		eaPush(pppList, pCurData);
	}
}

void LPCO_AddSimpleDataToLPCO(LPCOSimpleDataInput *pSimpleData, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{
		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pDataString = NULL;
		char *pDataStringResult = NULL;


		LogParserConstructedObject *pLPCO;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);

		if (!Graph_objPathGetEString(pSimpleData->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pSimpleData->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto Failed;
		}

		if (!Graph_objPathGetEString(pSimpleData->pSourceDataName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pDataString, &pDataStringResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pSimpleData->pSourceDataName, pDataStringResult ? pDataStringResult : "none");
			
			goto Failed;
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pSimpleData->pLPCOTypeName);

		if (pSimpleData->pTranslationCommand && pSimpleData->pTranslationCommand[0])
		{
			char *pCommandString;
			int iRetVal;

			estrStackCreate(&pCommandString);

			estrPrintf(&pCommandString, "%s %s", pSimpleData->pTranslationCommand, pDataString);
			estrClear(&pDataString);
			iRetVal = cmdParseAndReturn(pCommandString, &pDataString, CMD_CONTEXT_HOWCALLED_LOGPARSER);
			estrDestroy(&pCommandString);

			if (!iRetVal)
			{
				goto Failed;
			}

		}
		AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, pSimpleData->pDataNameInLPCO, pDataString);

		SetLPCOWasModified(pLPCO);


	Failed:
		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);

		estrDestroy(&pDataString);
		estrDestroy(&pDataStringResult);
	}
	PERFINFO_AUTO_STOP();

}

void LPCO_AddSimpleDataWildCardToLPCO(LPCOSimpleDataWildCardInput *pSimpleDataWildCard, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{

		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pQueryResultString = NULL;

		char *pTemp = NULL;

		WildCardQueryResult **ppQueryResults = NULL;

		int i;


		LogParserConstructedObject *pLPCO;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);

		if (!Graph_objPathGetEString(pSimpleDataWildCard->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pSimpleDataWildCard->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto Failed;
		}

		if (!objDoWildCardQuery(pSimpleDataWildCard->pSourceWildCardString, parse_ParsedLog, pLog, &ppQueryResults, &pQueryResultString))
		{
			Errorf("Wildcard query failure in LPCO name %s: %s", pSimpleDataWildCard->pLPCOName, pQueryResultString ? pQueryResultString : "none");
			
			goto Failed;
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pSimpleDataWildCard->pLPCOTypeName);

		estrStackCreate(&pTemp);

		for (i=0; i < eaSize(&ppQueryResults); i++)
		{
			estrPrintf(&pTemp, "%s%s", pSimpleDataWildCard->pPrefix && pSimpleDataWildCard->pPrefix[0] ? pSimpleDataWildCard->pPrefix : "",
				ppQueryResults[i]->pKey);

			AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, pTemp, ppQueryResults[i]->pValue);
		}

		SetLPCOWasModified(pLPCO);

	Failed:

		eaDestroyStruct(&ppQueryResults, parse_WildCardQueryResult);

		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);


		estrDestroy(&pQueryResultString);
		estrDestroy(&pTemp);
	}
	
	PERFINFO_AUTO_STOP();
}


static __forceinline LPCOSimpleList *LPCO_FindSimpleList(LogParserConstructedObject *pLPCO, char *pListName)
{
	int i;

	for (i=0; i < eaSize(&pLPCO->ppSimpleLists); i++)
	{
		if (pLPCO->ppSimpleLists[i]->pName == pListName)
		{
			return pLPCO->ppSimpleLists[i];
		}
	}

	return NULL;
}


LPCOSimpleList *LPCO_FindOrCreateSimpleList(LogParserConstructedObject *pLPCO, char *pListName)
{
	LPCOSimpleList *pList = LPCO_FindSimpleList(pLPCO, pListName);

	if (!pList)
	{
		pList = StructCreate(parse_LPCOSimpleList);
		pList->pName  = pListName;
		eaPush(&pLPCO->ppSimpleLists, pList);
	}

	return pList;
}

void LPCO_SimpleList_AddItem(LPCOSimpleList *pList, char *pName)
{
	int iIndex = eaIndexedFindUsingString(&pList->ppElements, pName);

	if (iIndex == -1)
	{
		LPCOSimpleListElement *pElement = StructCreate(parse_LPCOSimpleListElement);
		pElement->pName = strdup(pName);
		eaPush(&pList->ppElements, pElement);
	}
}

bool LPCO_SimpleList_IsItemInList(LPCOSimpleList *pList, char *pName)
{
	int iIndex = eaIndexedFindUsingString(&pList->ppElements, pName);

	if (iIndex == -1)
	{
		return false;
	}

	return true;
}

void LPCO_SimpleList_RemoveItem(LPCOSimpleList *pList, char *pName)
{
	int iIndex = eaIndexedFindUsingString(&pList->ppElements, pName);

	if (iIndex != -1)
	{
		StructDestroy(parse_LPCOSimpleListElement, pList->ppElements[iIndex]);
		eaRemove(&pList->ppElements, iIndex);
	}
}



void LPCO_SimpleList_Reset(LPCOSimpleList *pList)
{
	eaDestroyStruct(&pList->ppElements, parse_LPCOSimpleListElement);
}

void LPCO_SimpleList_Set(LPCOSimpleList *pList, char *pValuesString, char *pSeparators)
{
	char **ppNames = NULL;
	int i;
	
	eaDestroyStruct(&pList->ppElements, parse_LPCOSimpleListElement);
	DivideString(pValuesString, pSeparators, &ppNames, DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE);

	for (i=0; i < eaSize(&ppNames); i++)
	{
		LPCOSimpleListElement *pElement = StructCreate(parse_LPCOSimpleListElement);
		pElement->pName = ppNames[i];
		eaPush(&pList->ppElements, pElement);
	}

	eaDestroy(&ppNames);
}





void LPCO_AddSimpleListToLPCO(LPCOSimpleListInput *pSimpleListDef, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{

		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pDataString = NULL;
		char *pDataStringResult = NULL;
		LPCOSimpleList *pSimpleList;



		LogParserConstructedObject *pLPCO;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);

		if (!Graph_objPathGetEString(pSimpleListDef->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pSimpleListDef->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto Failed;
		}

		if (pSimpleListDef->eListOp != LIST_CLEAR)
		{

			if (!Graph_objPathGetEString(pSimpleListDef->pSourceDataName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pDataString, &pDataStringResult))
			{
				Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pSimpleListDef->pSourceDataName, pDataStringResult ? pDataStringResult : "none");
				
				goto Failed;
			}
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pSimpleListDef->pLPCOTypeName);

	

		switch (pSimpleListDef->eListOp)
		{
		case LIST_ADD:
			pSimpleList = LPCO_FindOrCreateSimpleList(pLPCO, pSimpleListDef->pListName);
			LPCO_SimpleList_AddItem(pSimpleList, pDataString);
			break;

		case LIST_REMOVE:
			pSimpleList = LPCO_FindSimpleList(pLPCO, pSimpleListDef->pListName);
			if (pSimpleList)
			{
				LPCO_SimpleList_RemoveItem(pSimpleList, pDataString);
			}
			break;

		case LIST_CLEAR:
			pSimpleList = LPCO_FindSimpleList(pLPCO, pSimpleListDef->pListName);
			if (pSimpleList)
			{
				LPCO_SimpleList_Reset(pSimpleList);
			}
			break;

		case LIST_SET:
			pSimpleList = LPCO_FindOrCreateSimpleList(pLPCO, pSimpleListDef->pListName);
			LPCO_SimpleList_Set(pSimpleList, pDataString, pSimpleListDef->pSeparators);
			break;
		}

		SetLPCOWasModified(pLPCO);


	//	AddOrUpdateDataInNameValuePairList(&pLPCO->ppSimpleData, pSimpleData->pDataNameInLPCO, pDataString);

	Failed:
		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);

		estrDestroy(&pDataString);
		estrDestroy(&pDataStringResult);

	}
	PERFINFO_AUTO_STOP();


}


LPCOTrackedValue *LPCO_FindExistingTrackedValue(LogParserConstructedObject *pLPCO, char *pName)
{
	int i = eaIndexedFindUsingString(&pLPCO->ppTrackedValues, pName);
	if (i == -1)
	{
		return NULL;
	}

	return pLPCO->ppTrackedValues[i];
}

void LPCO_FillInPairsForTrackedValueInstance(LogParserConstructedObject *pLPCO, LPCOTrackedValueInstance *pTrackedValue, 
	LPCOTrackedValueDef *pTrackedValueDef, ParsedLog *pLog)
{
	int i;

	char *pTempString = NULL;
	estrStackCreate(&pTempString);

	for (i=0; i < eaSize(&pTrackedValueDef->ppPairDefs); i++)
	{
		bool bResult;

		LPCOTrackedValuePairDef *pPairDef = pTrackedValueDef->ppPairDefs[i];

		if (pPairDef->pPairValue && pPairDef->pPairValue[0])
		{
			bResult = objPathGetEString(pPairDef->pPairValue, parse_LogParserConstructedObject, pLPCO, &pTempString);
		}
		else
		{
			bResult = objPathGetEString(pPairDef->pLogPairValue, parse_ParsedLog, pLog, &pTempString);
		}

		if (bResult)
		{
			AddOrUpdateDataInNameValuePairList(&pTrackedValue->ppPairs, pPairDef->pPairName, pTempString);
		}
		else if (pPairDef->pPairDefault && pPairDef->pPairDefault[0])
		{
			AddOrUpdateDataInNameValuePairList(&pTrackedValue->ppPairs, pPairDef->pPairName, pPairDef->pPairDefault);
		}

		estrClear(&pTempString);
	}

	estrDestroy(&pTempString);
}

AUTO_EXPR_FUNC(LPCOTrackedValue) ACMD_NAME(PROCEDURAL_LOG_1);
void LPCOTrackedValue_PROCEDURAL_LOG_1(U32 iTime, const char *pActionName, const char *pCategoryName, const char *pName1, const char *pValue1)
{
	ParsedLog *pNewLog = StructCreate(parse_ParsedLog);
	NameValuePair *pPair;
	pNewLog->iTime = iTime;

	pNewLog->pParsingCategory = (char*)allocAddString(pCategoryName);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName1);
	pPair->pValue = strdup(pValue1);

	eaPush(&pNewLog->ppPairs, pPair);

	pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
	pNewLog->pObjInfo->pAction = allocAddString(pActionName);

	LogParser_AddProceduralLog(pNewLog, "(1)");
}

AUTO_EXPR_FUNC(LPCOTrackedValue) ACMD_NAME(PROCEDURAL_LOG_2);
void LPCOTrackedValue_PROCEDURAL_LOG_2(U32 iTime, const char *pActionName, const char *pCategoryName, const char *pName1, const char *pValue1, const char *pName2, const char *pValue2)
{
	ParsedLog *pNewLog = StructCreate(parse_ParsedLog);
	NameValuePair *pPair;
	pNewLog->iTime = iTime;

	pNewLog->pParsingCategory = (char*)allocAddString(pCategoryName);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName1);
	pPair->pValue = strdup(pValue1);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName2);
	pPair->pValue = strdup(pValue2);

	eaPush(&pNewLog->ppPairs, pPair);

	pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
	pNewLog->pObjInfo->pAction = allocAddString(pActionName);

	LogParser_AddProceduralLog(pNewLog, "(2)");
}


AUTO_EXPR_FUNC(LPCOTrackedValue) ACMD_NAME(PROCEDURAL_LOG_3);
void LPCOTrackedValue_PROCEDURAL_LOG_3(U32 iTime, const char *pActionName, const char *pCategoryName, const char *pName1, const char *pValue1, const char *pName2, const char *pValue2, const char *pName3, const char *pValue3)
{
	ParsedLog *pNewLog = StructCreate(parse_ParsedLog);
	NameValuePair *pPair;
	pNewLog->iTime = iTime;

	pNewLog->pParsingCategory = (char*)allocAddString(pCategoryName);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName1);
	pPair->pValue = strdup(pValue1);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName2);
	pPair->pValue = strdup(pValue2);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName3);
	pPair->pValue = strdup(pValue3);

	eaPush(&pNewLog->ppPairs, pPair);

	pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
	pNewLog->pObjInfo->pAction = allocAddString(pActionName);

	LogParser_AddProceduralLog(pNewLog, "(3)");
}

AUTO_EXPR_FUNC(LPCOTrackedValue) ACMD_NAME(PROCEDURAL_LOG_4);
void LPCOTrackedValue_PROCEDURAL_LOG_4(U32 iTime, const char *pActionName, const char *pCategoryName, const char *pName1, const char *pValue1, const char *pName2, const char *pValue2, const char *pName3, const char *pValue3, const char *pName4, const char *pValue4)
{
	ParsedLog *pNewLog = StructCreate(parse_ParsedLog);
	NameValuePair *pPair;
	pNewLog->iTime = iTime;

	pNewLog->pParsingCategory = (char*)allocAddString(pCategoryName);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName1);
	pPair->pValue = strdup(pValue1);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName2);
	pPair->pValue = strdup(pValue2);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName3);
	pPair->pValue = strdup(pValue3);

	eaPush(&pNewLog->ppPairs, pPair);

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup(pName4);
	pPair->pValue = strdup(pValue4);

	eaPush(&pNewLog->ppPairs, pPair);

	pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
	pNewLog->pObjInfo->pAction = allocAddString(pActionName);

	LogParser_AddProceduralLog(pNewLog, "(4)");
}

bool LPCO_TryToGetFloatValFromTrackedValPair(LPCOTrackedValueInstance *pInstance, char *pName, float *pOutVal)
{
	NameValuePair *pPair = eaIndexedGetUsingString(&pInstance->ppPairs, pName);
	if (!pPair)
	{
		return false;
	}

	return StringToFloat(pPair->pValue, pOutVal);
}

void LPCO_ProcessAlgorithmicDeltaLogs(LogParserConstructedObject *pLPCO, U32 iTime, LPCOTrackedValue *pTrackedValue, LPCOTrackedValuePairDef *pPairDef, float fParentDelta)
{
	float fOldVal;
	float fNewVal;
	int iLogNum;
	const char *pCategoryString;
	NameValuePair *pPair;

	if (!eaSize(&pPairDef->ppProceduralLogDefs))
	{
		return;
	}



	if (!LPCO_TryToGetFloatValFromTrackedValPair(pTrackedValue->ppValues[1], pPairDef->pPairName, &fOldVal))
	{
		return;
	}

	if (!LPCO_TryToGetFloatValFromTrackedValPair(pTrackedValue->ppValues[0], pPairDef->pPairName , &fNewVal))
	{
		return;
	}

	pPair = eaIndexedGetUsingString(&pLPCO->ppSimpleData, "ParsingCategory");
	if (pPair)
	{
		pCategoryString = (char*)allocAddString(pPair->pValue);
	}
	else
	{
		pCategoryString = BASECATEGORY_UNKNOWN;
	}

	for (iLogNum = 0; iLogNum < eaSize(&pPairDef->ppProceduralLogDefs); iLogNum++)
	{
		LPCOTrackedValuePairDeltaProceduralLogDef *pLogDef = pPairDef->ppProceduralLogDefs[iLogNum];

		float fValToLog = fNewVal - fOldVal;
		char *pLogString = NULL;
		int i;
		char *pFieldString;
		ParsedLog *pNewLog;

		estrStackCreate(&pFieldString);

		estrStackCreate(&pLogString);

		if (pLogDef->bDivideByTrackedValDelta)
		{
			fValToLog /= fParentDelta;
		}

		if (pLogDef->fScale)
		{
			fValToLog *= pLogDef->fScale;
		}

		estrPrintf(&pLogString, "%s %f ",
			pLogDef->pDeltaDataNameInLog, fValToLog);

		for (i=0; i < eaSize(&pLogDef->ppExtraFields); i++)
		{
			if (pLogDef->ppExtraFields[i]->pFieldName[0] != '_')
			{
				if (pLogDef->ppExtraFields[i]->pFieldValue[0] == '.')
				{
					if (objPathGetEString(pLogDef->ppExtraFields[i]->pFieldValue, parse_LogParserConstructedObject, pLPCO, &pFieldString))
					{
						estrConcatf(&pLogString, "%s \"%s\" ", pLogDef->ppExtraFields[i]->pFieldName, pFieldString);
						estrClear(&pFieldString);
					}
				}
				else
				{
					estrConcatf(&pLogString, "%s \"%s\" ", pLogDef->ppExtraFields[i]->pFieldName, pLogDef->ppExtraFields[i]->pFieldValue);
				}
			}
		}

		pNewLog = StructCreate(parse_ParsedLog);
		pNewLog->iTime = iTime;
		pNewLog->pParsingCategory = pCategoryString;

		GetNameValuePairsFromString(pLogString, &pNewLog->ppPairs, NULL);

		pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
		pNewLog->pObjInfo->pAction = pLogDef->pActionName;

		LogParser_AddProceduralLog(pNewLog, pLogString);


		estrDestroy(&pFieldString);
		estrDestroy(&pLogString);
	}
}















void LPCO_AddTrackedValueToLPCO(LPCOTrackedValueDef *pTrackedValueDef, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{

		static ExprContext *spExprContext = NULL;

		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pDataString = NULL;
		char *pDataStringResult = NULL;
		bool bJustCreated = false;


		LogParserConstructedObject *pLPCO;
		LPCOTrackedValue *pTrackedValue;
		LPCOTrackedValueInstance *pNewValue;

		estrStackCreate(&pLPCOName);
		estrStackCreate(&pLPCONameResult);

		if (!Graph_objPathGetEString(pTrackedValueDef->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pTrackedValueDef->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto Failed;
		}

		if (!Graph_objPathGetEString(pTrackedValueDef->pSourceDataName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pDataString, &pDataStringResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pTrackedValueDef->pSourceDataName, pDataStringResult ? pDataStringResult : "none");
			
			goto Failed;
		}

		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pTrackedValueDef->pLPCOTypeName);

		pTrackedValue = LPCO_FindExistingTrackedValue(pLPCO, pTrackedValueDef->pDataNameInLPCO);

		if (pTrackedValue)
		{
			if (!pTrackedValueDef->bUseThisValueEvenIfNotNew && stricmp(pTrackedValue->ppValues[0]->pValue, pDataString) == 0)
			{
				goto Failed;
			}

			if (pTrackedValueDef->fMinInterval)
			{
				float fNewVal;
				float fOldVal;

				if (!StringToFloat(pTrackedValue->ppValues[0]->pValue, &fOldVal))
				{
					goto Failed;
				}

				if (!StringToFloat(pDataString, &fNewVal))
				{
					goto Failed;
				}

				if (ABS(fNewVal - fOldVal) < pTrackedValueDef->fMinInterval)
				{
					goto Failed;
				}
			}
		}
		else
		{
			pTrackedValue = StructCreate(parse_LPCOTrackedValue);
			pTrackedValue->pName = pTrackedValueDef->pDataNameInLPCO;
			eaPush(&pLPCO->ppTrackedValues, pTrackedValue);
			bJustCreated = true;
		}

		pNewValue = StructCreate(parse_LPCOTrackedValueInstance);
		pNewValue->pValue = strdup(pDataString);

		estrCopy2(&pTrackedValue->pCurVal, pNewValue->pValue);

		LPCO_FillInPairsForTrackedValueInstance(pLPCO, pNewValue, pTrackedValueDef, pLog);

		if (bJustCreated)
		{
			pTrackedValue->pFirstValue = StructClone(parse_LPCOTrackedValueInstance, pNewValue);
		}

		eaInsert(&pTrackedValue->ppValues, pNewValue, 0);

		if (eaSize(&pTrackedValue->ppValues) > pTrackedValueDef->iNumValuesToSave)
		{
			StructDestroy(parse_LPCOTrackedValueInstance, pTrackedValue->ppValues[eaSize(&pTrackedValue->ppValues)-1]);
			eaRemove(&pTrackedValue->ppValues, eaSize(&pTrackedValue->ppValues)-1);
		}

		if (eaSize(&pTrackedValueDef->ppExpressionStrings))
		{
			int i;

			if (!spExprContext)
			{
				ExprFuncTable* exprFuncTable = exprContextCreateFunctionTable();

				spExprContext = exprContextCreate();
				exprContextAddFuncsToTableByTag(exprFuncTable, "util");
				exprContextAddFuncsToTableByTag(exprFuncTable, "LPCOTrackedValue");

				exprContextSetFuncTable(spExprContext, exprFuncTable);

			}

			exprContextSetPointerVar(spExprContext, "lpco", pLPCO, parse_LogParserConstructedObject, false, true);
			exprContextSetPointerVar(spExprContext, "trackedVal", pTrackedValue, parse_LPCOTrackedValue, false, true);
			exprContextSetPointerVar(spExprContext, "log", pLog, parse_ParsedLog, false, true);

			if (!pTrackedValueDef->ppExpressions)
			{
				eaSetSize(&pTrackedValueDef->ppExpressions, eaSize(&pTrackedValueDef->ppExpressionStrings));
			
				exprContextSetSilentErrors(spExprContext, false);

				for (i=0; i < eaSize(&pTrackedValueDef->ppExpressionStrings); i++)
				{
					pTrackedValueDef->ppExpressions[i] = exprCreate();
					exprGenerateFromString(pTrackedValueDef->ppExpressions[i], spExprContext, pTrackedValueDef->ppExpressionStrings[i], NULL);
				}
			
				exprContextSetSilentErrors(spExprContext, true);
			}

			for (i=0; i < eaSize(&pTrackedValueDef->ppExpressions); i++)
			{
				MultiVal answer = {0};

				exprEvaluate(pTrackedValueDef->ppExpressions[i], spExprContext, &answer);
			}
		}

		if (eaSize(&pTrackedValue->ppValues) > 1)
		{
			float fOldVal;
			float fNewVal;
			int i;

			if (StringToFloat(pTrackedValue->ppValues[0]->pValue, &fNewVal) && StringToFloat(pTrackedValue->ppValues[1]->pValue, &fOldVal) && fOldVal != fNewVal)
			{
				float fDelta = fNewVal - fOldVal;

				for (i=0; i < eaSize(&pTrackedValueDef->ppPairDefs); i++)
				{
					LPCO_ProcessAlgorithmicDeltaLogs(pLPCO, pLog->iTime, pTrackedValue, pTrackedValueDef->ppPairDefs[i], fDelta);
				}
			}
		}

		SetLPCOWasModified(pLPCO);


	Failed:
		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);

		estrDestroy(&pDataString);
		estrDestroy(&pDataStringResult);
	}
	PERFINFO_AUTO_STOP();

}


LPCOFSM *GetLPCOFSMFromLPCOAndName(LogParserConstructedObject *pLPCO, const char *pFSMName)
{
	int i;

	LPCOFSM *pRetVal;

	for (i=0; i < eaSize(&pLPCO->ppFSMs); i++)
	{
		if (pLPCO->ppFSMs[i]->pName == pFSMName)
		{
			return pLPCO->ppFSMs[i];
		}
	}

	pRetVal = StructCreate(parse_LPCOFSM);
	pRetVal->pName = pFSMName;

	eaPush(&pLPCO->ppFSMs, pRetVal);

	return pRetVal;
	
}

LPCOFSMState *LPCOFSM_FindState(LPCOFSM *pFSM, const char *pStateName)
{
	LPCOFSMState *pNewState;
	int iCurNumStates = eaSize(&pFSM->ppStates);
	pNewState = eaIndexedGetUsingString(&pFSM->ppStates, pStateName);

	if (pNewState)
	{
		return pNewState;
	}

	pNewState = StructCreate(parse_LPCOFSMState);
	pNewState->pName = pStateName;
	eaPush(&pFSM->ppStates, pNewState);

	return pNewState;
}

void LPCOFSM_DoStateTransition(LogParserConstructedObject *pLPCO, LPCOFSM *pFSM, const char *pNewStateName, U32 iTime, LPCOFSMStateDef *pStateDef)
{
	LPCOFSMState *pNewState;
	LPCOFSMState *pOldState = NULL;
	char *pNewComment = NULL;
	int i;

	if (pFSM->pCurStateName == pNewStateName)
	{
		return;
	}

	//if people have corrupt old files, there may be a pCurStateName but no pOldState
	if (pFSM->pCurStateName && (pOldState = eaIndexedGetUsingString(&pFSM->ppStates, pFSM->pCurStateName)))
	{
		char *pDurationString = NULL;

		pOldState->iTimesLeft++;
		pOldState->iLastVisitLength = iTime - pFSM->iTimeEnteredCurState;
		pOldState->iTotalTime += pOldState->iLastVisitLength;
		if (pOldState->iTimesLeft == 1)
		{
			pOldState->iFirstVisitLength = iTime - pFSM->iTimeEnteredCurState;
		}

		pOldState->iAverageTime = pOldState->iTotalTime / pOldState->iTimesLeft;

		estrStackCreate(&pDurationString);
		timeSecondsDurationToShortEString(iTime - pFSM->iTimeEnteredCurState, &pDurationString);

		estrPrintf(&pNewComment, "Entered state %s at %s, after %s in state %s",
			pNewStateName, timeGetLocalDateStringFromSecondsSince2000(iTime), pDurationString,
			pOldState->pName);
		estrDestroy(&pDurationString);
	
	}
	else
	{
		estrPrintf(&pNewComment, "Entered state %s at %s",
			pNewStateName, timeGetLocalDateStringFromSecondsSince2000(iTime));
	}


	pNewState = LPCOFSM_FindState(pFSM, pNewStateName);

	pNewState->iTimesEntered++;



	pFSM->iTimeEnteredCurState = iTime;
	pFSM->pCurStateName = pNewStateName;

	

	eaPush(&pFSM->ppComments, pNewComment);

	if (eaSize(&pFSM->ppComments) > 50)
	{
		estrDestroy(&(pFSM->ppComments[0]));
		eaRemove(&pFSM->ppComments, 0);
	}

	for (i=0; i < eaSize(&pStateDef->ppProceduralLogs); i++)
	{
		LPCOSimpleProceduralLogDef *pLogDef = pStateDef->ppProceduralLogs[i];

		if (eaSize(&pLogDef->ppStateName) || eaSize(&pLogDef->ppLastStateName))
		{
			if (eaFind(&pLogDef->ppStateName, pNewState->pName) == -1 
				&& (!pOldState || eaFind(&pLogDef->ppLastStateName, pOldState->pName) == -1))
			{
				continue;
			}
		}
		
		LPCO_SetProceduralLogToBeLoggedSoon(pLPCO, pLogDef);

	}
	
}

void LPCO_PossibleFSMTransition(LPCOFSMStateDef *pFSMStateDef, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{
		char *pLPCOName = NULL;
		char *pLPCONameResult = NULL;

		char *pStateName = NULL;
		char *pStateNameResult = NULL;

		LogParserConstructedObject *pLPCO;
		LPCOFSM *pLPCOFSM;

		if (!Graph_objPathGetEString(pFSMStateDef->pParent->pLPCOName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pLPCOName, &pLPCONameResult))
		{
			Errorf("Invalid xpath syntax in LPCO name %s (error string: %s)", pFSMStateDef->pParent->pLPCOName, pLPCONameResult ? pLPCONameResult : "none");
			
			goto PossibleTransitionFailed;
		}
	
		if (pFSMStateDef->pXPathStateName && pFSMStateDef->pXPathStateName[0])
		{
			if (!Graph_objPathGetEString(pFSMStateDef->pXPathStateName, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pStateName, &pStateNameResult))
			{
				Errorf("Invalid xpath syntax in LPCO FSM state name %s (error string: %s)", pFSMStateDef->pParent->pLPCOName, pStateNameResult ? pStateNameResult : "none");

				goto PossibleTransitionFailed;
			}
		}


		pLPCO = GetLPCOFromNameAndTypeName(pLPCOName, pFSMStateDef->pParent->pLPCOTypeName);

		pLPCOFSM = GetLPCOFSMFromLPCOAndName(pLPCO, pFSMStateDef->pParent->pFSMName);

		if (pFSMStateDef->pXPathStateName && pFSMStateDef->pXPathStateName[0])
		{
			LPCOFSM_DoStateTransition(pLPCO, pLPCOFSM, allocAddString(pStateName), pLog->iTime, pFSMStateDef);
		}
		else
		{
			LPCOFSM_DoStateTransition(pLPCO, pLPCOFSM, pFSMStateDef->pLiteralStateName, pLog->iTime, pFSMStateDef);
		}


		SetLPCOWasModified(pLPCO);


	PossibleTransitionFailed:
		estrDestroy(&pStateName);
		estrDestroy(&pStateNameResult);

		estrDestroy(&pLPCOName);
		estrDestroy(&pLPCONameResult);
	}
	PERFINFO_AUTO_STOP();

}

U32 ComputeNextCallbackTime(U32 iFrequency, U32 iOffset)
{
	U32 iTime = timeSecondsSince2000();

	return LogParserRoundTime(iTime + iFrequency, iFrequency, iOffset);
}

static U32 LPCOTypeBroadcast(void *pUserData)
{
	LPCOTypeTimedBroadcastInput *pTimedBroadcastInput = (LPCOTypeTimedBroadcastInput*) pUserData;
	StashTable singleTypeTable = GetLPCOTypeTableFromTypeName(pTimedBroadcastInput->pLPCOTypeName);
	U32 iNextCallbackTime = ComputeNextCallbackTime(pTimedBroadcastInput->iFrequencyInSeconds, pTimedBroadcastInput->iOffset);

	LPCOType_SetProceduralLogToBeLoggedSoon(singleTypeTable, pTimedBroadcastInput);

	return iNextCallbackTime;
}

void LPCO_InitSystem(void)
{
	int i, j;

	for (i=0; i < eaSize(&gLogParserConfig.ppLPCOStringGroupInputs); i++)
	{
		LPCOStringGroupInput *pStringGroup = gLogParserConfig.ppLPCOStringGroupInputs[i];

		RegisterActionSpecificCallback(pStringGroup->pActionName, NULL, pStringGroup->pStringGroupName, 
			LPCO_AddStringGroupToLPCO, pStringGroup, &pStringGroup->useThisLog, ASC_LPCO_STRINGGROUP_ACTION);
	}

	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCODataGroupInputs); i++)
	{
		LPCODataGroupInput *pDataGroup = gLogParserConfig.ppLPCODataGroupInputs[i];

		RegisterActionSpecificCallback(pDataGroup->pActionName, NULL, pDataGroup->pDataGroupName, LPCO_AddDataGroupToLPCO, 
			pDataGroup, &pDataGroup->useThisLog, ASC_LPCO_DATAGROUP_ACTION);
	}

	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCOTypeTimedBroadcastInputs); i++)
	{
		LPCOTypeTimedBroadcastInput *pLPCOTypeTimedBroadcastInput = gLogParserConfig.ppLPCOTypeTimedBroadcastInputs[i];
		U32 iNextCallbackTime = ComputeNextCallbackTime(pLPCOTypeTimedBroadcastInput->iFrequencyInSeconds, pLPCOTypeTimedBroadcastInput->iOffset);

		RegisterLogParserTimedCallback(pLPCOTypeTimedBroadcastInput->pCallbackName, LPCOTypeBroadcast, pLPCOTypeTimedBroadcastInput, iNextCallbackTime);
	}

	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCOFSMs); i++)
	{
		LPCOFSMDef *pFSMDef = gLogParserConfig.ppLPCOFSMs[i];

		for (j=0; j < eaSize(&pFSMDef->ppStates); j++)
		{
			pFSMDef->ppStates[j]->pParent = pFSMDef;
			RegisterActionSpecificCallback(pFSMDef->ppStates[j]->pActionName, NULL, pFSMDef->ppStates[j]->pLiteralStateName, 
				LPCO_PossibleFSMTransition, pFSMDef->ppStates[j], &pFSMDef->ppStates[j]->useThisLog, ASC_LPCO_FSMDEF_ACTION);
		}
	}

	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCOSimpleDataInputs); i++)
	{
		LPCOSimpleDataInput *pSimpleData = gLogParserConfig.ppLPCOSimpleDataInputs[i];

		RegisterActionSpecificCallback(pSimpleData->pActionName, NULL, pSimpleData->pDataNameInLPCO, LPCO_AddSimpleDataToLPCO, 
			pSimpleData, &pSimpleData->useThisLog, ASC_LPCO_SIMPLEDATA_ACTION);
	}

	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCOSimpleDataWildCardInputs); i++)
	{
		LPCOSimpleDataWildCardInput *pSimpleDataWildCard = gLogParserConfig.ppLPCOSimpleDataWildCardInputs[i];

		RegisterActionSpecificCallback(pSimpleDataWildCard->pActionName, NULL, pSimpleDataWildCard->pPrefix, 
			LPCO_AddSimpleDataWildCardToLPCO, pSimpleDataWildCard, &pSimpleDataWildCard->useThisLog, ASC_LPCO_SIMPLEDATAWILDCARD_ACTION);
	}


	for (i = 0; i < eaSize(&gLogParserConfig.ppLPCOSimpleListInputs); i++)
	{
		LPCOSimpleListInput *pSimpleList = gLogParserConfig.ppLPCOSimpleListInputs[i];

		RegisterActionSpecificCallback(pSimpleList->pActionName, NULL, pSimpleList->pListName, LPCO_AddSimpleListToLPCO,
			pSimpleList, &pSimpleList->useThisLog, ASC_LPCO_SIMPLELIST_ACTION);
	}


	for (i=0; i < eaSize(&gLogParserConfig.ppLPCOTrackedValueInputs); i++)
	{
		LPCOTrackedValueDef *pTrackedValue = gLogParserConfig.ppLPCOTrackedValueInputs[i];

		RegisterActionSpecificCallback(pTrackedValue->pActionName, NULL, pTrackedValue->pDataNameInLPCO, LPCO_AddTrackedValueToLPCO, 
			pTrackedValue, &pTrackedValue->useThisLog, ASC_LPCO_TRACKEDVALUE_ACTION);
	}

	if (sDirForLPCOs[0] && iPeriodicLPCOWrite_Interval)
	{
		ReadPeriodicWrittenLPCOs();
		TimedCallback_Add(PeriodicWriteOutLPCOs, NULL, (float)iPeriodicLPCOWrite_Interval);
	}
}
	
bool FindDefaultFieldsToShowForLPCO(char *pObjTypeName, char **ppOutString)
{
	int i;
	bool bFoundOne = false;

	if (strStartsWith(pObjTypeName, "logged_"))
	{
		pObjTypeName += 7;
	}

	for (i=0; i < eaSize(&gLogParserConfig.ppLPCOSimpleDataInputs); i++)
	{
		if (stricmp(pObjTypeName, gLogParserConfig.ppLPCOSimpleDataInputs[i]->pLPCOTypeName) == 0)
		{
			if (gLogParserConfig.ppLPCOSimpleDataInputs[i]->bIsDefaultForMonitoring)
			{
				estrConcatf(ppOutString, " %sSimpleData[%s].value ", bFoundOne ? ", " : "",
					gLogParserConfig.ppLPCOSimpleDataInputs[i]->pDataNameInLPCO);
				bFoundOne = true;
			}
		}
	}

	for (i=0; i < eaSize(&gLogParserConfig.ppLPCODataGroupInputs); i++)
	{
		if (stricmp(pObjTypeName, gLogParserConfig.ppLPCODataGroupInputs[i]->pLPCOTypeName) == 0)
		{
			if (gLogParserConfig.ppLPCODataGroupInputs[i]->bAverageIsDefaultForMonitoring 
				|| gLogParserConfig.ppLPCODataGroupInputs[i]->bCountIsDefaultForMonitoring
				|| gLogParserConfig.ppLPCODataGroupInputs[i]->bSumIsDefaultForMonitoring)
			{
				char *pStringToAdd = NULL;
				estrPrintf(&pStringToAdd, " DataGroups[%s].%s ",gLogParserConfig.ppLPCODataGroupInputs[i]->pDataGroupName,
					gLogParserConfig.ppLPCODataGroupInputs[i]->bCountIsDefaultForMonitoring ? "count" : (gLogParserConfig.ppLPCODataGroupInputs[i]->bSumIsDefaultForMonitoring ? "sum" : "average"));

				if (estrLength(ppOutString) == 0 || !strstri(*ppOutString, pStringToAdd))
				{
					estrConcatf(ppOutString, " %s %s", bFoundOne ? "," : "", pStringToAdd);
					bFoundOne = true;
				}

				estrDestroy(&pStringToAdd);
			}
		}
	}

	for (i=0; i < eaSize(&gLogParserConfig.ppLPCOTrackedValueInputs); i++)
	{
		if (stricmp(pObjTypeName, gLogParserConfig.ppLPCOTrackedValueInputs[i]->pLPCOTypeName) == 0)
		{
			if (gLogParserConfig.ppLPCOTrackedValueInputs[i]->bIsDefaultForMonitoring)
			{
				estrConcatf(ppOutString, " %sTrackedValues[%s].CurVal ",
					bFoundOne ? ", " : "", gLogParserConfig.ppLPCOTrackedValueInputs[i]->pDataNameInLPCO);
			}
		}
	}




	if (bFoundOne)
	{
		estrConcatf(ppOutString, "\"");
	}

	return bFoundOne;
}
















//has to come before GetDuplicateTPIForLPCO
#include "LogParserConstructedObjects_h_ast.c"


ParseTable *GetDuplicateTPIForLPCO(char *pObjTypeName)
{

	char *pDefaultFieldsToShow = NULL;
	ParseTable *pRetVal = calloc(sizeof(parse_LogParserConstructedObject), 1);
	memcpy(pRetVal, parse_LogParserConstructedObject, sizeof(parse_LogParserConstructedObject));
	pRetVal[0].name = allocAddString(pObjTypeName);

	estrPrintf(&pDefaultFieldsToShow, " HTML_DEF_FIELDS_TO_SHOW = \"");
	if (FindDefaultFieldsToShowForLPCO(pObjTypeName, &pDefaultFieldsToShow))
	{
		AppendFormatString(pRetVal, pDefaultFieldsToShow);
	}

	estrDestroy(&pDefaultFieldsToShow);


	return pRetVal;
}

void LPCO_GetStatusFileName(char *pString, int pString_size)
{
	sprintf_s(SAFESTR2(pString), "%s/LogParser_LPCOStatus.txt", LogParserLocalDataDir());
}

AUTO_STRUCT;
typedef struct LPCOTypedList
{
	char *pTypeName; AST(KEY, POOL_STRING)
	LogParserConstructedObject **ppObjects;
} LPCOTypedList;

AUTO_STRUCT;
typedef struct LPCOListOfLists
{
	LPCOTypedList **ppLists;
} LPCOListOfLists;

//the acutal objects in a listOfLists should never be destroyed. At load time, they are populated into
//the stash tables, and at save time they are just pointers to the objects owned by the stash tables
AUTO_FIXUPFUNC;
TextParserResult LPCOTypedList_Fixup(LPCOTypedList *pList, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		eaDestroy(&pList->ppObjects);
	}

	return true;
}

void LPCO_PopulateListOfLists(LPCOListOfLists *pList)
{
	StashElement outerElement;
	StashTableIterator outerIterator;

	stashGetIterator(sAllLPCOTypes, &outerIterator);
	
	while (stashGetNextElement(&outerIterator, &outerElement))
	{
		StashElement innerElement;
		StashTableIterator innerIterator;
		StashTable singleTypeTable;
		LPCOTypedList *pSingleTypeList = StructCreate(parse_LPCOTypedList);
		pSingleTypeList->pTypeName = stashElementGetKey(outerElement);
		singleTypeTable = stashElementGetPointer(outerElement);

		eaPush(&pList->ppLists, pSingleTypeList);

		stashGetIterator(singleTypeTable, &innerIterator);
	
		while (stashGetNextElement(&innerIterator, &innerElement))
		{
			LogParserConstructedObject *pObject = stashElementGetPointer(innerElement);
			eaPush(&pSingleTypeList->ppObjects, pObject);
		}
	}
}

void LPCO_PopulateFromListOfLists(LPCOListOfLists *pList)
{
	int i, j;
	bool gbLogParserLPCOReadHasAlerted = false;
	for (i=0; i < eaSize(&pList->ppLists); i++)
	{
		LPCOTypedList *pSingleTypeList = pList->ppLists[i];

		for (j=0; j < eaSize(&pSingleTypeList->ppObjects); j++)
		{
			if(pSingleTypeList->ppObjects[j] && pSingleTypeList->ppObjects[j]->pName)
			{
				AddPreexistingLPCOToTables(pSingleTypeList->ppObjects[j], pSingleTypeList->pTypeName);
			}
			else if(!gbLogParserLPCOReadHasAlerted)
			{
				ErrorOrAlert("LOGPARSER_LPCO_READERROR", "Bad data loading LPCO of type %s.", pSingleTypeList->pTypeName);
				gbLogParserLPCOReadHasAlerted = true;
			}
		}
	}
}



void LPCO_DumpStatusFile(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	LPCOListOfLists list = {0};
	LPCO_GetStatusFileName(SAFESTR(fileName));

	LPCO_PopulateListOfLists(&list);

	ParserWriteTextFile(fileName, parse_LPCOListOfLists, &list, 0, 0);

	StructDeInit(parse_LPCOListOfLists, &list);
}

void LPCO_LoadFromStatusFile(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	LPCOListOfLists list = {0};
	LPCO_GetStatusFileName(SAFESTR(fileName));

	if (!fileExists(fileName))
	{
		return;
	}

	ParserReadTextFile(fileName, parse_LPCOListOfLists, &list, 0);

	LPCO_PopulateFromListOfLists(&list);

	StructDeInit(parse_LPCOListOfLists, &list);
}

void LPCODestructor(LogParserConstructedObject *pLPCO)
{
	StructDestroy(parse_LogParserConstructedObject, pLPCO);
}



void LPCO_ResetSystem(void)
{
	
	StashElement outerElement;
	StashTableIterator outerIterator;

	stashGetIterator(sAllLPCOTypes, &outerIterator);
	
	while (stashGetNextElement(&outerIterator, &outerElement))
	{
		
		StashTable singleTypeTable = stashElementGetPointer(outerElement);
		stashTableClearEx(singleTypeTable, NULL, LPCODestructor);
	}
}


void LPCO_FullyResetSystem(void)
{
	LPCO_ResetSystem();
	LPCO_InitSystem();
}


int PlayerLPCOMatchesFilter(LogParserConstructedObject *pObj, LogParserPlayerLogFilter *pFilter)
{
	NameValuePair *pPair;
	int iAccessLevel = -1;
	int iLevel = -1;
	int iTeamSize = -1;
	char *pClass = NULL;
	LPCOTrackedValue *pTrackedValue;
	int i;
	LPCOSimpleList *pPowerList;
	char *pMapName = NULL;

	if (eaSize(&pFilter->ppPlayersToRequire))
	{
		bool bFound = false;

		for (i=0; i < eaSize(&pFilter->ppPlayersToRequire); i++)
		{
			if (stricmp(pFilter->ppPlayersToRequire[i]->pPlayerName, pObj->pName) == 0)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			return 0;
		}
	}

	pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "AccessLevel");
	if (pPair)
	{
		iAccessLevel = atoi(pPair->pValue);
	}

	pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "TeamSize");
	if (pPair)
	{
		iTeamSize = atoi(pPair->pValue);
	}

	pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "Class");
	if (pPair)
	{
		pClass = pPair->pValue;
	}

	pPair = eaIndexedGetUsingString(&pObj->ppSimpleData, "MapName");
	if (pPair)
	{
		pMapName = pPair->pValue;
	}


	pPowerList = LPCO_FindSimpleList(pObj, "Powers");




	pTrackedValue = eaIndexedGetUsingString(&pObj->ppTrackedValues, "Level");
	if (pTrackedValue)
	{
		iLevel = atoi(pTrackedValue->ppValues[0]->pValue);
	}



	if (iAccessLevel == -1)
	{
		//if we couldn't read an access level, then only continue if the min and max filters are default
		if (pFilter->iMinAccessLevel != 0 || pFilter->iMaxAccessLevel != ACCESS_INTERNAL)
		{
			return -1;
		}
	}
	else
	{
		if (iAccessLevel < pFilter->iMinAccessLevel || iAccessLevel > pFilter->iMaxAccessLevel)
		{
			return 0;
		}
	}

	if (iLevel == -1)
	{
		if (pFilter->iMinLevel || pFilter->iMaxLevel)
		{
			return -1;
		}
	}
	else
	{
		if (pFilter->iMinLevel && iLevel < pFilter->iMinLevel)
		{
			return 0;
		}

		if (pFilter->iMaxLevel && iLevel > pFilter->iMaxLevel)
		{
			return 0;
		}
	}

	if (iTeamSize == -1)
	{
		if (pFilter->iMinTeamSize || pFilter->iMaxTeamSize)
		{
			return -1;
		}
	}
	else
	{
		if (pFilter->iMinTeamSize && iTeamSize < pFilter->iMinTeamSize)
		{
			return 0;
		}

		if (pFilter->iMaxTeamSize && iTeamSize > pFilter->iMaxTeamSize)
		{
			return 0;
		}
	}

	if (eaSize(&pFilter->ppPowersToRequire) && !pPowerList)
	{
		return -1;
	}

	for (i=0; i < eaSize(&pFilter->ppPowersToRequire); i++)
	{
		if (!pPowerList)
		{
			return 0;
		}

		if (!LPCO_SimpleList_IsItemInList(pPowerList, pFilter->ppPowersToRequire[i]->pPowerName))
		{
			return 0;
		}
	}

	if (eaSize(&pFilter->ppMapNamesToRequire))
	{
		bool bFound = false;

		if (!pMapName)
		{
			return -1;
		}

		for (i=0; i < eaSize(&pFilter->ppMapNamesToRequire); i++)
		{
			if (stricmp(pFilter->ppMapNamesToRequire[i]->pMapName, "ALL") == 0 
				|| stricmp(pFilter->ppMapNamesToRequire[i]->pMapName, pMapName) == 0)
			{
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			return 0;
		}
	}

	if (eaSize(&pFilter->ppRoleNames))
	{
		int iIndex;

		if (!pClass)
		{
			return -1;
		}

		iIndex = eaFindString(&pFilter->ppRoleNames, pClass);

		if (iIndex == -1)
		{
			return 0;
		}
	}




	return 1;
}



	
#include "LogParserConstructedObjects_c_ast.c"
