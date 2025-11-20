#include "LogParserUniqueCounter.h"
#include "textparser.h"
#include "LogParserUniqueCounter_h_ast.h"
#include "earray.h"
#include "estring.h"
#include "ResourceInfo.h"
#include "LogParser.h"
#include "TimedCallback.h"
#include "StringUtil.h"
#include "crypt.h"
#include "NameValuePair.h"
#include "LogParserUniqueCounter_c_ast.h"
#include "file.h"

AUTO_STRUCT;
typedef struct LogParserUniqueCounter_List
{
	LogParserUniqueCounter **ppUniqueCounters;
} LogParserUniqueCounter_List;

static LogParserUniqueCounter_List *spCounters = NULL;

char *LogParserUniqueCounter_GetStatusFilename(void)
{
	static char *spRetVal = NULL;

	if (!spRetVal)
	{
		estrPrintf(&spRetVal, "%s/LogParser_UniqueCounters.txt", LogParserLocalDataDir());
	}

	return spRetVal;
}

AUTO_FIXUPFUNC;
TextParserResult fixupLogParserUniqueCounter(LogParserUniqueCounter *pCounter, enumTextParserFixupType eType, void *pExtraData)
{
	int i;

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		pCounter->sFoundThings = stashTableCreateInt(16);
		for (i = 0; i < ea32Size(&pCounter->pFoundThingsArray); i++)
		{
			stashIntAddPointer(pCounter->sFoundThings, pCounter->pFoundThingsArray[i], NULL, false);
		}
		return 1;


	case FIXUPTYPE_DESTRUCTOR:
		stashTableDestroySafe(&pCounter->sFoundThings);
		return 1;
	}

	return 0;
}



LogParserUniqueCounter *LogParserUniqueCounter_FindByName(char *pName)
{
	if (spCounters)
	{
		return eaIndexedGetUsingString(&spCounters->ppUniqueCounters, pName);
	}

	return NULL;
}

static bool ValidateDefinition(LogParserUniqueCounterDefinition *pDefinition)
{
	if (!pDefinition->pName || !pDefinition->pActionName || !pDefinition->pFieldToCount || !pDefinition->pOutActionName
		|| !(pDefinition->iCountingPeriod || pDefinition->iCountingPeriod_Hours))
	{
		devassertmsgf(0, "LogParserUniqueCounterDefinition %s is missing one or more essential fields",
			pDefinition->pName);
		return false;
	}

	return true;
}

static void UniqueCounter_AddLog(LogParserUniqueCounter *pCounter, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	char *pCountField = NULL;
	char *pCountFieldXpathResultEString = NULL; 
	U32 iIntToCount;

	estrStackCreate(&pCountField);
	estrStackCreate(&pCountFieldXpathResultEString);

	if (!Graph_objPathGetEString(pCounter->pDefinition->pFieldToCount, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pCountField, &pCountFieldXpathResultEString))
	{
		//we fail silently on an error which we suspect is just due to absent keyed data, because it is most likely
		//due to added or removed ProjSpecificParsedLogObjInfo, which is legal (as opposed to, for instance, misspelled field names)
		if (!strStartsWith(pCountFieldXpathResultEString, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT))
		{
			Errorf("Invalid xpath syntax in graph data point name %s (error string: %s)", pCounter->pDefinition->pFieldToCount, pCountFieldXpathResultEString ? pCountFieldXpathResultEString : "none");
		}

		goto AddLogFailed;
	}

	if (pCounter->pDefinition->bFieldToCountIsInteger)
	{
		if (!StringToUint_Paranoid(pCountField, &iIntToCount))
		{
			Errorf("Invalid integer %s while doing unique count", pCountField);
			goto AddLogFailed;
		}
	}
	else
	{
		iIntToCount = cryptAdler32String(pCountField);
	}

	if (!pCounter->sFoundThings)
	{
		pCounter->sFoundThings = stashTableCreateInt(16);
	}

	if (stashIntAddPointer(pCounter->sFoundThings, iIntToCount, NULL, false))
	{
		pCounter->iCount++;
		ea32Push(&pCounter->pFoundThingsArray, iIntToCount);
	}
	
	

	AddLogFailed:
		estrDestroy(&pCountField);
		estrDestroy(&pCountFieldXpathResultEString);

}

void UniqueCounter_EmitLog(LogParserUniqueCounter *pCounter, U32 iLogTime)
{
	int iCount = 0;
	ParsedLog *pNewLog;
	NameValuePair *pPair;


	iCount = pCounter->iCount;
	pCounter->iCount = 0;
	if (pCounter->sFoundThings)
	{
		stashTableClear(pCounter->sFoundThings);
		ea32Destroy(&pCounter->pFoundThingsArray);
	}

	pNewLog = StructCreate(parse_ParsedLog);
			
	pNewLog->iTime = iLogTime;

	pPair = StructCreate(parse_NameValuePair);
	pPair->pName = strdup("count");
	pPair->pValue = strdupf("%d", iCount);
	eaPush(&pNewLog->ppPairs, pPair);

	pNewLog->pObjInfo = StructCreate(parse_ParsedLogObjInfo);
	pNewLog->pObjInfo->pAction = pCounter->pDefinition->pOutActionName;

	LogParser_AddProceduralLog(pNewLog, strdupf("count %d", iCount));
}

void UniqueCounter_CheckAllForLogsToEmit(void)
{
	U32 iCurTime = timeSecondsSince2000();

	FOR_EACH_IN_EARRAY(spCounters->ppUniqueCounters, LogParserUniqueCounter, pCounter)
	{
		if (pCounter->iNextCompletionTime < iCurTime)
		{
			UniqueCounter_EmitLog(pCounter, pCounter->iNextCompletionTime);

			//if the shard was down for a while, skip over a bunch of time as necessary
			do
			{
				pCounter->iNextCompletionTime += pCounter->pDefinition->iCountingPeriod;
			}
			while (pCounter->iNextCompletionTime < iCurTime);
		}
	}
	FOR_EACH_END;
}

void SetFirstCompletionTime(LogParserUniqueCounter *pCounter)
{
	U32 iCurTime = timeSecondsSince2000();
	U32 iMostRecentMidnight = FindNextSS2000WhichMatchesLocalHourOfDay(iCurTime, 0);
	if (iMostRecentMidnight > iCurTime)
	{
		iMostRecentMidnight -= 24 * 60 * 60;
	}

	pCounter->iNextCompletionTime = iMostRecentMidnight + pCounter->pDefinition->iCountingPeriod;
	while (pCounter->iNextCompletionTime < iCurTime)
	{
		pCounter->iNextCompletionTime += pCounter->pDefinition->iCountingPeriod;
	}
}



void LogParserUniqueCounter_InitAllFromConfig(void)
{
	LogParserUniqueCounter_List *pLoadedList = StructCreate(parse_LogParserUniqueCounter_List);
	char *pFileName = LogParserUniqueCounter_GetStatusFilename();

	spCounters = StructCreate(parse_LogParserUniqueCounter_List);
	resRegisterDictionaryForEArray("UniqueCounters", RESCATEGORY_OTHER, 0, 
		&spCounters->ppUniqueCounters, parse_LogParserUniqueCounter);

	if (fileExists(pFileName))
	{
		ParserReadTextFile(pFileName, parse_LogParserUniqueCounter_List, pLoadedList, 0);
	}

	FOR_EACH_IN_EARRAY(gLogParserConfig.ppUniqueCounters, LogParserUniqueCounterDefinition, pDefinition)
	{
		LogParserUniqueCounter *pCounter;

		if (LogParserUniqueCounter_FindByName(pDefinition->pName))
		{
			continue;
		}

		if (!ValidateDefinition(pDefinition))
		{
			continue;
		}

		if (pDefinition->iCountingPeriod_Hours && !pDefinition->iCountingPeriod)
		{
			pDefinition->iCountingPeriod = pDefinition->iCountingPeriod_Hours * 60 * 60;
		}

		pCounter = eaIndexedRemoveUsingString(&pLoadedList->ppUniqueCounters, pDefinition->pName);

		if (pCounter)
		{
			pCounter->pDefinition = pDefinition;
		}
		else
		{
			pCounter = StructCreate(parse_LogParserUniqueCounter);
			pCounter->pName = strdup(pDefinition->pName);
			pCounter->pDefinition = pDefinition;

			SetFirstCompletionTime(pCounter);
		}

		eaPush(&spCounters->ppUniqueCounters, pCounter);

		RegisterActionSpecificCallback(pDefinition->pActionName, NULL, pCounter->pName, UniqueCounter_AddLog, pCounter, &pCounter->pDefinition->useThisLog,
			ASC_UNIQUECOUNTER);
		
	}
	FOR_EACH_END;

	UniqueCounter_CheckAllForLogsToEmit();

	//any unique counters we no longer have definitions for might as well get destroyed
	StructDestroy(parse_LogParserUniqueCounter_List, pLoadedList);
}

void LogParserUniqueCounter_WriteOut(void)
{
	ParserWriteTextFile(LogParserUniqueCounter_GetStatusFilename(), 
		parse_LogParserUniqueCounter_List, spCounters, 0, 0);
}

//vaguely primeish number so it doesn't get in sync with other things
#define WRITEOUT_FREQUENCY 678
#define CHECK_FOR_EMIT_FREQUENCY 58

void LogParserUniqueCounter_Tick(void)
{
	static U32 siNextWriteOutTime = 0;
	static U32 siNextEmitTime = 0;
	U32 iCurTime = timeSecondsSince2000();

	if (!siNextWriteOutTime)
	{
		siNextWriteOutTime = iCurTime + WRITEOUT_FREQUENCY;
		siNextEmitTime = iCurTime + CHECK_FOR_EMIT_FREQUENCY;
		return;
	}

	if (iCurTime >= siNextEmitTime)
	{
		UniqueCounter_CheckAllForLogsToEmit();
		siNextEmitTime += CHECK_FOR_EMIT_FREQUENCY;
	}

	if (iCurTime >= siNextWriteOutTime)
	{
		LogParserUniqueCounter_WriteOut();
		siNextWriteOutTime+= WRITEOUT_FREQUENCY;
	}
}





#include "LogParserUniqueCounter_h_ast.c"
#include "LogParserUniqueCounter_c_ast.c"