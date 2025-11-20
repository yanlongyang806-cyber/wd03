#include "LogParser.h"
#include "estring.h"
#include "LogParsing_h_ast.h"
#include "StringCache.h"
#include "logging.h"
#include "httpJpegLibrary.h"
#include "ResourceInfo.h"
#include "LogParserGraphs_h_ast.h"
#include "Expression.h"
#include "StringUtil.h"
#include "timing.h"
#include "UtilitiesLib.h"
#include "LogParser_h_ast.h"
#include "LogParserGraphs.h"
#include "fileutil.h"
#include "wininclude.h"
#include "MedianTracker.h"
#include "HttpXPathSupport.h"
#include "fileUtil2.h"
#include "LogParserGraphs_c_ast.h"
#include "HttpLib.h"
#include "mathUtil.h"
#include "cmdParse.h"
#include "GenericFileServing.h"
#include "LogParserUtils.h"
#include "TimedCallback.h"

bool GetFlotDataStringForGraph(char **ppOutEstring, Graph *pGraph, char **ppErrorEString, UrlArgumentList *pArgList);
void WriteOutFlotTextFileForGraph(char *pFileName, Graph *pGraph, char *pDataString);
bool GetFullyWrappedFlotStringForGraph(Graph *pGraph, char **ppOutString, char **ppErrorString, bool bStandaloneHTMLFileMode, char *pPreCalcedDataString, U32 iTime, UrlArgumentList *pArgList);
void FindOldDataForGraph(Graph *pGraph);

char gGraphDirName_Internal[CRYPTIC_MAX_PATH] = "";
AUTO_CMD_STRING(gGraphDirName_Internal, GraphDirName);

bool gbLogParserGraphReadHasAlerted = false;

//the literal string "total", pooled
static const char *POOLED_TOTAL_STRING;

static char *GetGraphDirName(void)
{
	if (!gGraphDirName_Internal[0])
	{
		sprintf(gGraphDirName_Internal, "%s/LogParserGraphs", LogParserLocalDataDir());
	}

	
	return gGraphDirName_Internal;
}

static char *spGnuPlotColorNames[] = 
{
"grey10",
"gray20",
"grey20",
"gray30",
"grey30",
"gray40",
"grey40",
"gray50",
"grey50",
"gray60",
"grey60",
"gray70",
"grey70",
"gray80",
"grey80",
"gray90",
"grey90",
"gray100",
"grey100",
"gray",
"grey",
"light-gray",
"light-grey",
"dark-gray",
"dark-grey",
"red",
"light-red",
"dark-red",
"yellow",
"light-yellow",
"dark-yellow",
"green",
"light-green",
"dark-green",
"spring-green",
"forest-green",
"sea-green",
"blue",
"light-blue",
"dark-blue",
"midnight-blue",
"navy",
"medium-blue",
"royalblue",
"skyblue",
"cyan",
"light-cyan",
"dark-cyan",
"magenta",
"light-magenta",
"dark-magenta",
"turquoise",
"light-turquoise",
"dark-turquoise",
"pink",
"light-pink",
"dark-pink",
"coral",
"light-coral",
"orange-red",
"salmon",
"light-salmon",
"dark-salmon",
"aquamarine",
"khaki",
"dark-khaki",
"goldenrod",
"light-goldenrod",
"dark-goldenrod",
"gold",
"beige",
"brown",
"orange",
"dark-orange",
"violet",
"dark-violet",
"plum",
"purple",
};

// Extra stylesheet to use when displaying graphs
static const char sExtraStylesheet[] = "<link rel=\"stylesheet\" type=\"text/css\" href=\"/static/css/graph.css\">";

// Script that will disable normal server monitor auto-refresh, since Log Parser graphs do it differently.
static const char sExtraScript[] = "<script type=\"text/javascript\">disable_auto_refresh = true;</script>";

//returns the next "rounded off" time to clear a graph.
static U32 GetNextClearTime(U32 iTime, U32 iLifeSpan)
{
	struct tm t = {0};
	U32 iStartingTime;

	timeMakeLocalTimeStructFromSecondsSince2000(iTime, &t);


	t.tm_hour = 0;
	t.tm_min = 0;
	t.tm_sec = 0;

	iStartingTime = timeGetSecondsSince2000FromLocalTimeStruct(&t);
	iStartingTime -= t.tm_wday * 24 * 60 * 60;

	while (iStartingTime <= iTime)
	{
		iStartingTime += iLifeSpan;
	}

	return iStartingTime;
	
}


static char *FindColorNameFromCategoryName(const char *pCategoryName)
{
	int i;

	if (stricmp(pCategoryName, "black") == 0)
	{
		return "#000000";
	}
	else if (stricmp(pCategoryName, "white") == 0)
	{
		return "#FFFFFF";
	}
	for (i=0; i < ARRAY_SIZE(spGnuPlotColorNames); i++)
	{
		if (stricmp(pCategoryName, spGnuPlotColorNames[i]) == 0)
		{
			return spGnuPlotColorNames[i];
		}
	}

	return NULL;
}


StashTable sGraphsByGraphName = 0;

//the list of non-long-term graphs that is written out and read back in to 
//maintain the logparser's state
static GraphList sAllNonLongTermGraphList = {0};

static void Graph_JpegCB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData);

//for gnuplot paths, we want full paths, not just "./tmp"
static const char *fullTempDir(void)
{
	if (isProductionMode())
	{
		static char outBuffer[CRYPTIC_MAX_PATH] = "";

		if (!outBuffer[0])
		{
			fileGetcwd(SAFESTR(outBuffer));
			strcat(outBuffer, "/tmp");
			forwardSlashes(outBuffer);
		}
		
		return outBuffer;

	}
	else
	{
		return fileTempDir();
	}
}

static bool GraphUsesTime(Graph *pGraph)
{
	if (stricmp(pGraph->pDefinition->pBarField, ".time") == 0)
	{
		return true;
	}

	if (strStartsWith(pGraph->pDefinition->pBarField, "TIME_ROUNDED("))
	{
		return true;
	}

	return false;

}

static void Graph_ClearData(Graph *pGraph)
{
	if (pGraph->pOnlyCategory)
	{
		StructDestroy(parse_GraphCategory, pGraph->pOnlyCategory);
		pGraph->pOnlyCategory = NULL;
	}
	else if (pGraph->categoriesTable)
	{
		stashTableDestroy(pGraph->categoriesTable);
		pGraph->categoriesTable = NULL;
		eaDestroyStruct(&pGraph->ppCategories, parse_GraphCategory);

	}

	eaDestroy(&pGraph->ppCategoryNames);
	eaDestroy(&pGraph->ppDataPointNames);

	pGraph->iTotalDataCount = 0;
	pGraph->fDataMax = pGraph->fDataMin = pGraph->fDataSum = 0.0f;
	pGraph->iUniqueDataPoints = 0;

	if (gbStandAlone)
	{
		pGraph->iMostRecentDataTime = 0;
	}
}


AUTO_FIXUPFUNC;
TextParserResult GraphDataPoint_ParserFixup(GraphDataPoint *pPoint, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		if (pPoint->pTracker)
		{
			MedianTracker_Destroy(pPoint->pTracker);
			pPoint->pTracker = NULL;
		}
		break;
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult GraphCategory_ParserFixup(GraphCategory *pCategory, enumTextParserFixupType eType, void *pExtraData)
{
	int i;

	switch (eType)
	{
	case FIXUPTYPE_CONSTRUCTOR:
		pCategory->dataPointsTable = stashTableCreateAddress(4);
		break;

	case FIXUPTYPE_POST_TEXT_READ:
		for (i=0; i < eaSize(&pCategory->ppDataPoints); i++)
		{
			if(pCategory->ppDataPoints[i]->pDataPointName)
			{
				stashAddPointer(pCategory->dataPointsTable, pCategory->ppDataPoints[i]->pDataPointName, pCategory->ppDataPoints[i], false);
			}
			else
			{
				if(!gbLogParserGraphReadHasAlerted)
				{
					ErrorOrAlert("LOGPARSER_GRAPH_READERROR", "Bad data loading graph category %s.", pCategory->pCategoryName);
					gbLogParserGraphReadHasAlerted = true;
				}
				return PARSERESULT_ERROR;
			}
		}
		break;

	case FIXUPTYPE_DESTRUCTOR:
		stashTableDestroy(pCategory->dataPointsTable);
		break;
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult Graph_ParserFixup(Graph *pGraph, enumTextParserFixupType eType, void *pExtraData)
{
	int i;

	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		for (i=0; i < eaSize(&pGraph->ppCategories); i++)
		{
			if (!pGraph->categoriesTable)
			{
				pGraph->categoriesTable = stashTableCreateAddress(4);
			}
			if(pGraph->ppCategories[i]->pCategoryName)
			{
				stashAddPointer(pGraph->categoriesTable, pGraph->ppCategories[i]->pCategoryName, pGraph->ppCategories[i], false);
			}
			else
			{
				if(!gbLogParserGraphReadHasAlerted)
				{
					ErrorOrAlert("LOGPARSER_GRAPH_READERROR", "Bad data loading graph %s.", pGraph->pGraphName);
					gbLogParserGraphReadHasAlerted = true;
				}
				return PARSERESULT_ERROR;
			}
		}
		break;

	case FIXUPTYPE_DESTRUCTOR:
		if (pGraph->categoriesTable)
		{
			stashTableDestroy(pGraph->categoriesTable);
		}
		break;
	}
	return PARSERESULT_SUCCESS;
}



static void Graph_InternalSaveGraphCB(char *pData, int iDataSize, int iLifeSpan, char *pMessage, void *pUserData)
{
	Graph *pGraph = (Graph*)pUserData;

	if (pData)
	{
		pGraph->pLastFullGraphStoredJpeg = malloc(iDataSize);
		memcpy(pGraph->pLastFullGraphStoredJpeg, pData, iDataSize);
		pGraph->iLastFullGraphDataSize = iDataSize;
	}
}


//returns true if the graph has, because of its data type, an implied number of categories. For instance,
//both GRAPHDATATYPE_MIN_MAX_AVG and GRAPHDATATYPE_MEDIANS imply 3 "categories".
static bool GraphHasImpliedNumberofCategories(Graph *pGraph, int *pOutNumCategories, bool bForCSV)
{
	if (pGraph->pDefinition->eDataType == GRAPHDATATYPE_MIN_MAX_AVG
		|| pGraph->pDefinition->eDataType == GRAPHDATATYPE_MEDIANS)
	{
		if (pOutNumCategories)
		{
			//CSVs have a "count" field which is not graphed
			*pOutNumCategories = bForCSV ? 4 : 3;
		}
		return true;
	}

	return false;
}

static char *MinMaxAvgCatNames[] = { "MIN", "AVG", "MAX", "Count" };
static char *MediansCatNames[] = { "10th", "50th", "90th", "Count" };

static char *GetImpliedCategoryName(Graph *pGraph, int iCategoryNum, bool bForCSV)
{
	if (pGraph->pDefinition->eDataType == GRAPHDATATYPE_MIN_MAX_AVG)
	{
		return MinMaxAvgCatNames[iCategoryNum];
	}
	else
	{
		return MediansCatNames[iCategoryNum];
	}
}
		

static int Graph_GetNumOutputCategories(Graph *pGraph, bool bForCSV)
{
	int iRetVal;

	if (pGraph->iTotalDataCount == 0)
	{
		return 0;
	}

	if (GraphHasImpliedNumberofCategories(pGraph, &iRetVal, bForCSV))
	{
		return iRetVal;
	}
	else if (pGraph->pOnlyCategory)
	{
		return 1;
	}
	else 
	{
		return eaSize(&pGraph->ppCategoryNames);
	}
}

static char *Graph_GetNthOutputCategoryName(Graph *pGraph, int iCatNum, bool bForCSV)
{
	if (GraphHasImpliedNumberofCategories(pGraph, NULL, bForCSV))
	{
		return GetImpliedCategoryName(pGraph, iCatNum, bForCSV);
	}
	else if (pGraph->pOnlyCategory)
	{
		return pGraph->pGraphName;
	}
	else
	{
		return pGraph->ppCategoryNames[iCatNum];
	}
}


AUTO_COMMAND;
void DebugDumpGraph(char *pGraphName)
{
	Graph *pGraph;
	char *pString = NULL;

	if (!stashFindPointer(sGraphsByGraphName, pGraphName, &pGraph))
	{
		return;
	}


	ParserWriteText(&pString, parse_Graph, pGraph, 0, 0, 0);
	estrConcatf(&pString, "\n\nNum categories in stash table: %d\n", stashGetCount(pGraph->categoriesTable));

	log_printf(LOG_BUG, "Debug dump of %s: %s\n", pGraph->pGraphName, pString);

	estrDestroy(&pString);
}




GraphCategory *Graph_GetNthOutputCategory(Graph *pGraph, int iCatNum, bool bForCSV)
{
	if (pGraph->pOnlyCategory)
	{
		return pGraph->pOnlyCategory;
	}
	else
	{
		GraphCategory *pRetVal = NULL;

		if(!pGraph->ppCategoryNames[iCatNum] && !gbLogParserGraphReadHasAlerted)
		{
			DebugDumpGraph(pGraph->pGraphName);
			CRITICAL_PROGRAMMER_ALERT("LOGPARSER_GRAPH_CATEGORY_CORRUPTION", "Null category name for graph %s.",
				pGraph->pGraphName);
			gbLogParserGraphReadHasAlerted = true;
		}
		else if (!stashFindPointer(pGraph->categoriesTable, pGraph->ppCategoryNames[iCatNum], &pRetVal) && !gbLogParserGraphReadHasAlerted)
		{
			DebugDumpGraph(pGraph->pGraphName);
			CRITICAL_PROGRAMMER_ALERT("LOGPARSER_GRAPH_CATEGORY_CORRUPTION", "Graph corruption for graph %s, category %d, while writing CSV file.",
				pGraph->pGraphName, iCatNum);
			gbLogParserGraphReadHasAlerted = true;
		}
		return pRetVal;
	}
}

static void Graph_WriteDataPointIntoEStringFromNthOutputCategory(char **ppEString, Graph *pGraph, GraphDataPoint *pDataPoint, int iCatNum)
{
	switch (pGraph->pDefinition->eDataType)
	{
	case GRAPHDATATYPE_SUM:
		estrPrintf(ppEString, "%f", pDataPoint->fSum);
		break;

	case GRAPHDATATYPE_COUNT:
		estrPrintf(ppEString, "%d", pDataPoint->iCount);
		break;

	case GRAPHDATATYPE_AVERAGE:
		estrPrintf(ppEString, "%f", pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f);
		break;
	case GRAPHDATATYPE_MIN_MAX_AVG:
		switch (iCatNum)
		{
		case 0:
			estrPrintf(ppEString, "%f", pDataPoint->fMin);
			break;
		case 1:
			estrPrintf(ppEString, "%f", pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f);
			break;
		case 2:
			estrPrintf(ppEString, "%f", pDataPoint->fMax);
			break;
		case 3:
			estrPrintf(ppEString, "%d", pDataPoint->iCount);
			break;
		}
		break;
	case GRAPHDATATYPE_MEDIANS:
		switch (iCatNum)
		{
		case 0:
			estrPrintf(ppEString, "%f", MedianTracker_GetMedian(pDataPoint->pTracker, 0.1f));
			break;
		case 1:
			estrPrintf(ppEString, "%f", MedianTracker_GetMedian(pDataPoint->pTracker, 0.5f));
			break;
		case 2:
			estrPrintf(ppEString, "%f", MedianTracker_GetMedian(pDataPoint->pTracker, 0.9f));
			break;
		case 3:
			estrPrintf(ppEString, "%d", pDataPoint->iCount);
			break;
		}
		break;
	}
}


//tries to sort as ints. If that doesn't work, sort as floats. If that doesn't work, sort as strings
static int SortNames(const char** a, const char** b, const void *unused)
{
	bool AIsFloat, BIsFloat;
	float fA, fB;

	bool AIsInt, BIsInt;
	U32 iA, iB;

	AIsInt = StringToInt(*a, &iA);
	BIsInt = StringToInt(*b, &iB);

	if (AIsInt && BIsInt)
	{
		if (iA < iB)
		{
			return -1;
		}
		else if (iA > iB)
		{
			return 1;
		}

		return 0;
	}


	AIsFloat = StringToFloat(*a, &fA);
	BIsFloat = StringToFloat(*b, &fB);

	if (!(AIsFloat && BIsFloat))
	{
		return strcmp(*a, *b);
	}


	if (fA < fB)
	{
		return -1;
	}
	else if (fA > fB)
	{
		return 1;
	}

	return 0;
}


//for line graphs with multiple named categories, which must be 
//ints-in-string-form, sort the category names numerically
static void Graph_SortDataPointNamesForOutput(Graph *pGraph)
{
	if (pGraph->bNamesSorted)
	{
		return;
	}

	if (eaSize(&pGraph->ppDataPointNames) > 1)
	{
		eaStableSort(pGraph->ppDataPointNames, NULL, SortNames);
	}

	pGraph->bNamesSorted = true;
}


#define WRITESTRANDCOMMA(pString) { estrCopy2(&pTempEString, (pString)); estrMakeAllAlphaNumAndUnderscores(&pTempEString); estrConcatf(ppOutEString, "%s, ", pTempEString); }

void Graph_WriteCSV(Graph *pGraph, char **ppOutEString)
{
	char *pTempEString = NULL;
	char *pTempChar;
	int iDataPointNum;
	int iCatNum;

	gbLogParserGraphReadHasAlerted = false;

	if(!(pGraph->pOnlyCategory || eaSize(&pGraph->ppCategoryNames)))
	{
		return;
	}

	estrStackCreate(&pTempEString);

	//first, write column headers. The first column will be "names" of data points, the other columns are the categories
	//so first we get the name of the BarField
	pTempChar = strrchr(pGraph->pDefinition->pBarField, '.');
	if (pTempChar)
	{
		WRITESTRANDCOMMA(pTempChar);
	}
	else
	{
		WRITESTRANDCOMMA(pGraph->pDefinition->pBarField);
	}

	Graph_SortDataPointNamesForOutput(pGraph);


	for (iCatNum = 0; iCatNum < Graph_GetNumOutputCategories(pGraph, true); iCatNum++)
	{
		WRITESTRANDCOMMA(Graph_GetNthOutputCategoryName(pGraph, iCatNum, true));
	}

	estrConcatf(ppOutEString, "\n");

	for (iDataPointNum = 0; iDataPointNum < eaSize(&pGraph->ppDataPointNames); iDataPointNum++)
	{
		if (stricmp(pGraph->pDefinition->pBarField, ".time") == 0)
		{
			U32 iTime = 0;
			sscanf(pGraph->ppDataPointNames[iDataPointNum], "%u", &iTime);

			estrConcatf(ppOutEString, "%s, ", timeGetLocalSystemStyleStringFromSecondsSince2000(iTime));
		}
		else
		{
			WRITESTRANDCOMMA(pGraph->ppDataPointNames[iDataPointNum]);
		}


		for (iCatNum = 0; iCatNum < Graph_GetNumOutputCategories(pGraph, true); iCatNum++)
		{
			GraphDataPoint *pDataPoint;
			GraphCategory *pCategory = Graph_GetNthOutputCategory(pGraph, iCatNum, true);

			if (!pCategory || !stashFindPointer(pCategory->dataPointsTable, pGraph->ppDataPointNames[iDataPointNum], &pDataPoint))
			{
				estrConcatf(ppOutEString, " , ");
			}
			else
			{
				Graph_WriteDataPointIntoEStringFromNthOutputCategory(&pTempEString, pGraph, pDataPoint, iCatNum);
				estrConcatf(ppOutEString, "%s, ", pTempEString);
			}
		}

		estrConcatf(ppOutEString, "\n");
	}

	estrDestroy(&pTempEString);
}

AUTO_COMMAND;
char *WriteCSVCmd(char *pGraphName, char *pOutFileName)
{
	Graph *pGraph;
	FILE *pOutFile;
	char *pEString = NULL;

	if (!stashFindPointer(sGraphsByGraphName, pGraphName, &pGraph))
	{
		return "Graph doesn't exist";
	}
	mkdirtree_const(pOutFileName);
	pOutFile = fopen(pOutFileName, "wt");
	if (!pOutFile)
	{
		return "Couldn't create output file";
	}

	Graph_WriteCSV(pGraph, &pEString);
	fprintf(pOutFile, "%s", pEString);
	estrDestroy(&pEString);
	fclose(pOutFile);

	return "Written";
}



static void Graph_CheckLifetime(Graph *pGraph, U32 iTime)
{
	if (pGraph->iGraphClearTime && pGraph->iGraphClearTime < iTime )
	{
		char outFileName[CRYPTIC_MAX_PATH];
		char *pDataString = NULL;
		char *pErrorString = NULL;

		printf("For graph %s, passed clear time %u\n", pGraph->pGraphName, pGraph->iGraphClearTime);

		ea32Insert(&pGraph->ppTimesOfOldGraphs, pGraph->iGraphClearTime, 0);
	
		if (pGraph->pDefinition->bLongTermGraph && pGraph->iTotalDataCount)
		{
			LogParserLongTermData longTermData;

			ParsedLog fakeLog = {0};
			ParsedLogObjInfo fakeObjInfo = {0};

			fakeLog.iTime = timeSecondsSince2000();
			fakeLog.pObjInfo = &fakeObjInfo;
			fakeObjInfo.pAction = allocAddString("LongTermData");
			fakeObjInfo.pLongTermData = &longTermData;

			longTermData.pGraphName = pGraph->pGraphName;

			longTermData.pDataType = "Min";
			longTermData.fData = pGraph->fDataMin;
			servLogWithStruct(LOG_LONGTERMLOGS, "LongTermData", &longTermData, parse_LogParserLongTermData);
			ProcessSingleLog("LongTermLogs", &fakeLog, true);

			longTermData.pDataType = "Max";
			longTermData.fData = pGraph->fDataMax;
			servLogWithStruct(LOG_LONGTERMLOGS, "LongTermData", &longTermData, parse_LogParserLongTermData);
			ProcessSingleLog("LongTermLogs", &fakeLog, true);


			longTermData.pDataType = "Average";
			longTermData.fData = pGraph->fDataSum / pGraph->iTotalDataCount;
			servLogWithStruct(LOG_LONGTERMLOGS, "LongTermData", &longTermData, parse_LogParserLongTermData);
			ProcessSingleLog("LongTermLogs", &fakeLog, true);

		}

		if (!GetFlotDataStringForGraph(&pDataString, pGraph, &pErrorString, NULL))
		{
			Errorf("Error %s getting data string while saving old graph data for %s", pErrorString, pGraph->pGraphName);
			estrDestroy(&pErrorString);
		}
		else
		{
			FILE *pOutFile;
			sprintf(outFileName, "%s/%s/OldData/%uData.txt", GetGraphDirName(), pGraph->pGraphName, pGraph->iGraphClearTime);

			makeDirectoriesForFile(outFileName);
			pOutFile = fopen(outFileName, "wt");

			if (!pOutFile)
			{
				Errorf("Can't open file %s", outFileName);
			}
			else
			{
				char *pDateString = NULL;
				char *pCSVString = NULL;

				fprintf(pOutFile, "%s", pDataString);
				fclose(pOutFile);
			
				estrCopy2(&pDateString, timeGetLocalDateStringFromSecondsSince2000(pGraph->iGraphClearTime));

				estrMakeAllAlphaNumAndUnderscores(&pDateString);

				sprintf(outFileName, "%s/%s/OldGraphs/%s.html", GetGraphDirName(), pGraph->pGraphName, pDateString);


				makeDirectoriesForFile(outFileName);
				
				WriteOutFlotTextFileForGraph(outFileName, pGraph, pDataString);

				sprintf(outFileName, "%s/%s/OldCSVs/%s.txt", GetGraphDirName(), pGraph->pGraphName, pDateString);
				makeDirectoriesForFile(outFileName);

				printf("Writing file %s\n", outFileName);

				Graph_WriteCSV(pGraph, &pCSVString);

				pOutFile = fopen(outFileName, "wt");

				if (pOutFile)
				{
					fprintf(pOutFile, "%s", pCSVString);
					fclose(pOutFile);
				}

				estrDestroy(&pDateString);
				estrDestroy(&pCSVString);
			}
		}

		estrDestroy(&pErrorString);
		estrDestroy(&pDataString);

		pGraph->iGraphCreationTime = iTime;
		pGraph->iGraphClearTime = GetNextClearTime(iTime, pGraph->pDefinition->iGraphLifespan);

		Graph_ClearData(pGraph);
	}
}

// Calculate the rounded field value for a magic field.
static struct RoundedFieldValue RoundMagicField(char *pBarFieldName, ParsedLog *pLog)
{
	struct RoundedFieldValue value;

	if (stricmp(pBarFieldName, "HOUR_OF_DAY") == 0)
	{
		SYSTEMTIME	t;
		if (pLog->iTime)
		{
			timerLocalSystemTimeFromSecondsSince2000(&t,pLog->iTime);
			t.wMinute = 0;
			t.wSecond = 0;
			t.wMilliseconds = 0;
			value.iTime = timerSecondsSince2000FromLocalSystemTime(&t);
		}
		else
			value.iTime = 0;
	}

	else if (strStartsWith(pBarFieldName, "TIME_ROUNDED("))
	{
		int iRoundAmount = atoi(pBarFieldName + 13);

		if (iRoundAmount)
		{
			value.iTime = LogParserRoundTime(pLog->iTime, iRoundAmount, 0);
		}
		else
			value.iTime = 0;
	}

	else devassert(0);

	return value;
}

// Format a rounded field value.
static void FormatRoundedFieldValue(char *pBarFieldName, struct RoundedFieldValue *pValue, char **estrValue)
{
	if (stricmp(pBarFieldName, "HOUR_OF_DAY") == 0)
	{
		SYSTEMTIME t;
		timerLocalSystemTimeFromSecondsSince2000(&t, pValue->iTime);
		estrPrintf(estrValue, "%02d:00-%02d:00", t.wHour, t.wHour + 1);
	}

	else if (strStartsWith(pBarFieldName, "TIME_ROUNDED("))
	{
		SYSTEMTIME	t;
		timerLocalSystemTimeFromSecondsSince2000(&t, pValue->iTime);
		estrPrintf(estrValue, "%u", pValue->iTime);
	}

	else devassert(0);
}

static bool CheckForMagicBarField(char *pBarFieldName, ParsedLog *pLog, char **ppOutBarName)
{
	struct RoundedFieldValue value;

	if (stricmp(pBarFieldName, "HOUR_OF_DAY") == 0 || strStartsWith(pBarFieldName, "TIME_ROUNDED("))
	{
		value = RoundMagicField(pBarFieldName, pLog);
		FormatRoundedFieldValue(pBarFieldName, &value, ppOutBarName);
		return true;
	}

	return false;
}

//insert into data point names alphabetically
static void AddDataPointName(char ***pppDataPointNames, const char *pDataPointNamePooled)
{
	if (eaFind(pppDataPointNames, pDataPointNamePooled) == -1)
	{
		int i;
		bool bInserted = false;

		for (i=0; i < eaSize(pppDataPointNames); i++)
		{
			if (stricmp((*pppDataPointNames)[i], pDataPointNamePooled) > 0)
			{
				eaInsert(pppDataPointNames, DECONST(char *, pDataPointNamePooled), i);
				bInserted = true;
				break;
			}
		}

		if (!bInserted)
		{
			eaPush(pppDataPointNames, DECONST(char *, pDataPointNamePooled));
		}
	}
}

//normally, we assume a graph has categories if and only if it has something in pCategoryField in the GraphDefinition.
//
//However, there are some cases where it "magically" gets categories anyhow
static bool GraphHasCategoriesNoMatterWhat(Graph *pGraph)
{
	if (pGraph->pOnlyCategory)
	{
		return false;
	}

	if (pGraph->categoriesTable)
	{
		return true;
	}

	if (pGraph->pDefinition->bUseShardNamesForCategoriesAtClusterLevel)
	{
		if (LogParser_ClusterLevel_GotShardNames())
		{
			return true;
		}
	}

	return false;
}

typedef struct
{
	Graph *pGraph;
	const char *pCategoryNamePooled;
	const char *pDataPointNamePooled;
} MaybeAddZeroCache;	


static bool Graph_AddDataPointToCategoryInGraph(Graph *pGraph, ParsedLog *pLog, const char *pCategoryName,
	char *pDataPointName, float fCurVal, bool magic, bool bIsInternallyAddedPoint);

static void MaybeAddZeroCB(TimedCallback *pCallback, float fSecondsSince, MaybeAddZeroCache *pCache)
{
	Graph *pGraph = pCache->pGraph;
	GraphCategory *pCategory;
	GraphDataPoint *pMostRecent;


	if (stashFindPointer(pGraph->categoriesTable, pCache->pCategoryNamePooled, &pCategory))
	{
		pMostRecent = eaTail(&pCategory->ppDataPoints);
		if (pMostRecent->pDataPointName == pCache->pDataPointNamePooled)
		{
			U32 iMostRecentTime;
			if (StringToUint_Paranoid(pMostRecent->pDataPointName, &iMostRecentTime))
			{
				Graph_AddDataPointToCategoryInGraph(pGraph, NULL, pCache->pCategoryNamePooled, 
					STACK_SPRINTF("%u", iMostRecentTime + pGraph->pDefinition->iSecondsOfInactivityBeforeSetToZero), 0.0f, false, true);
			}
		}
	}
	free(pCache);
}

static bool Graph_AddDataPointToCategoryInGraph(Graph *pGraph, ParsedLog *pLog, const char *pCategoryName,
	char *pDataPointName, float fCurVal, bool magic, bool bIsInternallyAddedPoint)
{
	const char *pCategoryNamePooled;
	const char *pDataPointNamePooled;
	GraphCategory *pCategory;
	GraphDataPoint *pDataPoint;

	if (pCategoryName)
	{
		//at this point, pCategoryNameEString was already filled in back when we
		//did our initial checks of xpath syntax

		pCategoryNamePooled = allocAddString(pCategoryName);

		if (!pGraph->categoriesTable)
		{
			pGraph->categoriesTable = stashTableCreateAddress(4);
		}

		if (!stashFindPointer(pGraph->categoriesTable, pCategoryNamePooled, &pCategory))
		{
			pCategory = StructCreate(parse_GraphCategory);
			pCategory->pCategoryName = pCategoryNamePooled;
			pCategory->pColorName = FindColorNameFromCategoryName(pCategory->pCategoryName);

			stashAddPointer(pGraph->categoriesTable, pCategoryNamePooled, pCategory, false);
			eaPush(&pGraph->ppCategories, pCategory);

			eaPushUnique(&pGraph->ppCategoryNames, DECONST(char*, pCategoryNamePooled));
		}

	}
	else
	{
		if (!pGraph->pOnlyCategory)
		{
			pGraph->pOnlyCategory = StructCreate(parse_GraphCategory);
		}

		pCategory = pGraph->pOnlyCategory;
	}

	//special case... if we have the iSecondsOfInactivityBeforeSetToZero option set, then we know that we are a 
	//time-based graph with zero meaning "off". Therefore, if we get a data point that is non-zero, and our prevous data
	//was sufficiently long ago, then we want to add a fake zero point immediately before the current point, 
	//so that there won't be this big diagonal line along the grpah, as it goes from zero half an hour ago, to non-zero now.
	//
	//
	if (pGraph->pDefinition->iSecondsOfInactivityBeforeSetToZero && fCurVal != 0 && eaSize(&pCategory->ppDataPoints))
	{
		U32 iCurTime;
		U32 iLastTime;

		GraphDataPoint *pMostRecent = eaTail(&pCategory->ppDataPoints);

		if (pMostRecent->fSum == 0.0f && StringToUint_Paranoid(pMostRecent->pDataPointName, &iLastTime) && StringToUint_Paranoid(pDataPointName, &iCurTime)
			&& ((int)(iCurTime - iLastTime)) > pGraph->pDefinition->iSecondsOfInactivityBeforeSetToZero * 2)
		{
			Graph_AddDataPointToCategoryInGraph(pGraph, NULL, pCategoryName, STACK_SPRINTF("%u", iCurTime - 1),
				0.0f, false, true);
		}
	}

	if (StringIsAllWhiteSpace(pDataPointName))
	{
		pDataPointNamePooled = allocAddString("unnamed");
	}
	else
	{
		pDataPointNamePooled = allocAddString(pDataPointName);
	}

	// Add this data point if it does not yet exist.
	if (!stashFindPointer(pCategory->dataPointsTable, pDataPointNamePooled, &pDataPoint))
	{

		// Fill in missing points, if requested.
		if (pGraph->pDefinition->bFillMissedMagicPoints && magic && stashGetCount(pCategory->dataPointsTable) > 0)
		{
			ParsedLog dummy = {0};
			struct RoundedFieldValue value;
			bool do_fill;

			// Find the rounded time for the current point.
			// Note: This assumes that the data point name is time-based.
			value = RoundMagicField(pGraph->pDefinition->pBarField, pLog);

			// Don't do filling if this data point comes before all of the other data points we already have.
			// Note: Presently, only iTime rounding is supported.
			devassert(value.iTime != pCategory->minDataPoint.iTime);
			do_fill = value.iTime > pCategory->minDataPoint.iTime;
			pCategory->minDataPoint.iTime = MIN(pCategory->minDataPoint.iTime, value.iTime);

			// Loop over each previous missed point.
			for(; do_fill;)
			{
				char *pointName = NULL;
				const char *pooledPointName;
				bool success;

				// Look for a previous point.
				dummy.iTime = value.iTime - 1;
				value = RoundMagicField(pGraph->pDefinition->pBarField, &dummy);
				dummy.iTime = value.iTime;
				estrStackCreate(&pointName);
				CheckForMagicBarField(pGraph->pDefinition->pBarField, &dummy, &pointName);
				pooledPointName = allocAddString(pointName);
				estrDestroy(&pointName);
				success = stashFindPointer(pCategory->dataPointsTable, pooledPointName, NULL);

				// Stop if there is one.
				if (success)
					break;														// Exit.

				// Create a filler point for this missing point.
				pDataPoint = StructCreate(parse_GraphDataPoint);
				pDataPoint->pDataPointName = pooledPointName;
				pDataPoint->fSum = pGraph->pDefinition->fFillMissedMagicPointsSum;
				pDataPoint->iCount = pGraph->pDefinition->iFillMissedMagicPointsCount;
				stashAddPointer(pCategory->dataPointsTable, pooledPointName, pDataPoint, false);
				eaPush(&pCategory->ppDataPoints, pDataPoint);
				AddDataPointName(&pGraph->ppDataPointNames, pooledPointName);
			}
		}

		// Create the new data point.
		pGraph->iUniqueDataPoints++;
		pDataPoint = StructCreate(parse_GraphDataPoint);
		pDataPoint->pDataPointName = pDataPointNamePooled;

		stashAddPointer(pCategory->dataPointsTable, pDataPointNamePooled, pDataPoint, false);
		eaPush(&pCategory->ppDataPoints, pDataPoint);

		pGraph->bNamesSorted = false;

		//insert into data point names alphabetically
		AddDataPointName(&pGraph->ppDataPointNames, pDataPointNamePooled);
	}
	else if (pGraph->pDefinition->bOneValuePerDataPoint)
	{
		return false;
	}

	pDataPoint->fSum += fCurVal;
	pDataPoint->iCount++;

	if (pDataPoint->iCount == 1)
	{
		pDataPoint->fMax = pDataPoint->fMin = fCurVal;
	}
	else
	{
		if (fCurVal > pDataPoint->fMax)
		{
			pDataPoint->fMax = fCurVal;
		}
		else if (fCurVal < pDataPoint->fMin)
		{
			pDataPoint->fMin = fCurVal;
		}
	}



	if (pGraph->pDefinition->eDataType == GRAPHDATATYPE_MEDIANS)
	{
		if (!pDataPoint->pTracker)
		{
			pDataPoint->pTracker = MedianTracker_Create(1024);
		}

		MedianTracker_AddVal(pDataPoint->pTracker, fCurVal);
	}

	if (pGraph->pDefinition->iSecondsOfInactivityBeforeSetToZero && !bIsInternallyAddedPoint)
	{
		MaybeAddZeroCache *pCache = malloc(sizeof(MaybeAddZeroCache));
		pCache->pGraph = pGraph;
		pCache->pCategoryNamePooled = pCategoryNamePooled;
		pCache->pDataPointNamePooled = pDataPointNamePooled;

		TimedCallback_Run(MaybeAddZeroCB, pCache, pGraph->pDefinition->iSecondsOfInactivityBeforeSetToZero);
	}

	if (pGraph->pDefinition->bPeriodicCategoryTotal && stricmp(pCategory->pCategoryName, "total") != 0)
	{
		//if we already have a "total" data point at the current timestamp, don't add another one
		GraphCategory *pTotalCategory;
		bool bDontAdd = false;

		if (stashFindPointer(pGraph->categoriesTable, POOLED_TOTAL_STRING, &pTotalCategory))
		{
			GraphDataPoint *pMostRecentInTotal = eaTail(&pTotalCategory->ppDataPoints);
			if (pMostRecentInTotal)
			{
				if (pMostRecentInTotal->pDataPointName == pDataPointNamePooled)
				{
					bDontAdd = true;
				}
				else
				{
					U32 iMostRecentTotalTime;
					U32 iCurTime;

					if (StringToUint_Paranoid(pMostRecentInTotal->pDataPointName, &iMostRecentTotalTime) &&
						StringToUint_Paranoid(pDataPointNamePooled, &iCurTime)
						&& iCurTime > iMostRecentTotalTime)
					{
						//OK, will add new total
					}
					else
					{
						bDontAdd = true;
					}
				}
			}
		}

		if (!bDontAdd)
		{
			//now need to calucate total
			float fTotalSum = 0;
			int iCount = 0;

			FOR_EACH_IN_STASHTABLE(pGraph->categoriesTable, GraphCategory, pOtherCategory)
			{
				if (pOtherCategory != pTotalCategory)
				{
					GraphDataPoint *pMostRecent = eaTail(&pOtherCategory->ppDataPoints);

					if (pMostRecent)
					{
						fTotalSum += pMostRecent->fSum;
						iCount++;
					}
				}
			}
			FOR_EACH_END;

			//only create a total when we actually have more than one category
			if (iCount > 1)
			{	
				Graph_AddDataPointToCategoryInGraph(pGraph, pLog, "Total", pDataPointName, fTotalSum, false, true);
			}
		}
	}




	return true;
}

static void Graph_AddLogToGraph(Graph *pGraph, ParsedLog *pLog, ParseTable *pSubTableTPI, void *pSubObject)
{
	PERFINFO_AUTO_START_FUNC();
	{
		char *pDataPointNameEString = NULL;
		char *pDataPointNameXpathResultEString = NULL;
		const char *pDataPointNamePooled = NULL;

		MultiVal valueMulti = {0};

		float fCurVal = 0;
		bool bResult;
		bool magic;


		char *pCategoryNameEString = NULL;
		const char *pCategoryNamePooled = NULL;
		char *pCategoryNameXPathResultEString = NULL;
	
		char *pCountFieldXpathResultEString = NULL;

		if (gbStandAlone && !pGraph->bActiveInStandaloneMode)
		{
			PERFINFO_AUTO_STOP();
			return;
		}

	//the first thing we do is check all our xpaths for validity. Some types of invalidity cause us to fail and generate
	//an error. Others, we just fail silently
		if (pGraph->pDefinition->pCategoryField && pGraph->pDefinition->pCategoryField[0] || GraphHasCategoriesNoMatterWhat(pGraph))
		{
			estrStackCreate(&pCategoryNameEString);
			estrStackCreate(&pCategoryNameXPathResultEString);

			if (pGraph->pDefinition->pCategoryField && pGraph->pDefinition->pCategoryField[0])
			{
				if (!Graph_objPathGetEString(pGraph->pDefinition->pCategoryField, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pCategoryNameEString, &pCategoryNameXPathResultEString))
				{
					//we fail silently on an error which we suspect is just due to absent keyed data, because it is most likely
					//due to added or removed ProjSpecificParsedLogObjInfo, which is legal (as opposed to, for instance, misspelled field names)
					if (!strStartsWith(pCategoryNameXPathResultEString, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT))
					{
						Errorf("Invalid xpath syntax in graph category name %s (error string: %s)", pGraph->pDefinition->pCategoryField, pCategoryNameXPathResultEString ? pCategoryNameXPathResultEString : "none");
					}
					goto AddLogFailed;
				}
			}

			// Translate graph CategoryNames, if present.
			if (pCategoryNameEString && pGraph->ppCategoryNamesTableFromDef)
			{
				int intValue;
				if (StringToInt(pCategoryNameEString, &intValue) && intValue >= 0 && intValue < eaSize(&pGraph->ppCategoryNamesTableFromDef))
					estrCopy2(&pCategoryNameEString, pGraph->ppCategoryNamesTableFromDef[intValue]);
			}


			if (gbStandAlone && !gStandAloneOptions.bIncludeOtherLogs_bool && pCategoryNameEString && stricmp(pCategoryNameEString, BASECATEGORY_OTHER) == 0)
			{
				goto AddLogFailed;
			}

			//category names beginning with _ are always ignored
			if (pCategoryNameEString[0] == '_')
			{
				goto AddLogFailed;
			}

			if (pGraph->pDefinition->bUseShardNamesForCategoriesAtClusterLevel && LogParser_ClusterLevel_GotShardNames())
			{
				char *pShardName = LogParser_GetClusterNameFromID(pLog->iServerID);
				if (estrLength(&pCategoryNameEString))
				{
					estrInsertf(&pCategoryNameEString, 0, "%s_", pShardName);
				}
				else
				{
					estrCopy2(&pCategoryNameEString, pShardName);
				}
			}
			else
			{
				if(!pCategoryNameEString || !pCategoryNameEString[0])
					estrCopy2(&pCategoryNameEString, "Unknown");
			}


			if (pGraph->pDefinition->pCategoryCommand && pGraph->pDefinition->pCategoryCommand[0])
			{
				char *pCommandString;
				int iRetVal;

				estrStackCreate(&pCommandString);

				estrPrintf(&pCommandString, "%s %s", pGraph->pDefinition->pCategoryCommand, pCategoryNameEString);
				estrClear(&pCategoryNameEString);
				iRetVal = cmdParseAndReturn(pCommandString, &pCategoryNameEString, CMD_CONTEXT_HOWCALLED_LOGPARSER);
				estrDestroy(&pCommandString);

				if (!iRetVal)
				{
					goto AddLogFailed;
				}
			}
		}

		estrStackCreate(&pDataPointNameEString);
		estrStackCreate(&pDataPointNameXpathResultEString);

		if (!(magic = CheckForMagicBarField(pGraph->pDefinition->pBarField, pLog, &pDataPointNameEString)))
		{

			if (!Graph_objPathGetEString(pGraph->pDefinition->pBarField, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &pDataPointNameEString, &pDataPointNameXpathResultEString))
			{
				//we fail silently on an error which we suspect is just due to absent keyed data, because it is most likely
				//due to added or removed ProjSpecificParsedLogObjInfo, which is legal (as opposed to, for instance, misspelled field names)
				if (!strStartsWith(pDataPointNameXpathResultEString, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT))
				{
					Errorf("Invalid xpath syntax in graph data point name %s (error string: %s)", pGraph->pDefinition->pBarField, pDataPointNameXpathResultEString ? pDataPointNameXpathResultEString : "none");
				}

				goto AddLogFailed;
			}

			if (pGraph->pDefinition->iBarRounding)
			{
				int iInVal;
				if (!StringToInt(pDataPointNameEString, &iInVal))
				{
					Errorf("Can't round \"%s\" off... not an int", pDataPointNameEString);
					goto AddLogFailed;
				}

				estrPrintf(&pDataPointNameEString, "%d", RoundToNearestMultiple(iInVal, pGraph->pDefinition->iBarRounding));
			}

		}


		if (pGraph->pDefinition->pCountField && pGraph->pDefinition->pCountField[0])
		{

			estrStackCreate(&pCountFieldXpathResultEString);

			if (!Graph_objPathGetMultiVal(pGraph->pDefinition->pCountField, parse_ParsedLog, pLog, pSubTableTPI, pSubObject, &valueMulti, &pCountFieldXpathResultEString))
			{
				//we fail silently on an error which we suspect is just due to absent keyed data, because it is most likely
				//due to added or removed ProjSpecificParsedLogObjInfo, which is legal (as opposed to, for instance, misspelled field names)
				if (!strStartsWith(pCountFieldXpathResultEString, PARSERRESOLVE_KEYED_INDEX_NOT_FOUND_SHORT))
				{
					Errorf("invalid xpath syntax in graph count field %s (error string: %s)", pGraph->pDefinition->pCountField, pCountFieldXpathResultEString ? pCountFieldXpathResultEString : "none");
				}			
				goto AddLogFailed;
			}

			fCurVal = MultiValGetFloat(&valueMulti, &bResult);

			if (!bResult)
			{
				Errorf("MultiValGetFloat failed after getting graph value field %s", pGraph->pDefinition->pCountField);
				goto AddLogFailed;
			}
		}
		else 
		{
			fCurVal = 1.0f;
		}

		if (pGraph->iGraphCreationTime == 0)
		{
			pGraph->iGraphCreationTime = pLog->iTime;

			if (pGraph->pDefinition->iGraphLifespan)
			{
				pGraph->iGraphClearTime = GetNextClearTime(pLog->iTime, pGraph->pDefinition->iGraphLifespan);
			}
		}

		pGraph->iMostRecentDataTime = pLog->iTime;

	/*
		if (pGraph->pLastFullGraphStoredJpeg && !pGraph->pLiveImageLink)
		{
			estrPrintf(&pGraph->pLiveImageLink, "<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_LIVE_%s.jpg\" target=\"_blank\">%s</a></span>\n", pGraph->pGraphName, pGraph->pGraphName);
		}
		else if (!pGraph->pImageLink)
		{
			estrPrintf(&pGraph->pImageLink, "<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_%s.jpg\" target=\"_blank\">%s</a></span>\n", pGraph->pGraphName, pGraph->pGraphName);
		}*/


		if (pGraph->pDefinition->pCategoryField && pGraph->pDefinition->pCategoryField[0] || GraphHasCategoriesNoMatterWhat(pGraph))
		{
			Graph_AddDataPointToCategoryInGraph(pGraph, pLog, pCategoryNameEString, pDataPointNameEString, fCurVal, magic, false);
			if (pGraph->pDefinition->bAutoCreateTotal)
			{
				Graph_AddDataPointToCategoryInGraph(pGraph, pLog, "Total", pDataPointNameEString, fCurVal, magic, true);
			}
	
		}
		else
		{
			Graph_AddDataPointToCategoryInGraph(pGraph, pLog, NULL, pDataPointNameEString, fCurVal, magic, false);
		}

		pGraph->iTotalDataCount++;
		if (pGraph->iTotalDataCount == 1)
		{
			pGraph->fDataMax = pGraph->fDataMin = pGraph->fDataSum = fCurVal;
		}
		else
		{
			if (fCurVal < pGraph->fDataMin)
			{
				pGraph->fDataMin = fCurVal;
			}
			else if (fCurVal > pGraph->fDataMax)
			{
				pGraph->fDataMax = fCurVal;
			}

			pGraph->fDataSum += fCurVal;
		}


	AddLogFailed:
		estrDestroy(&pDataPointNameEString);
		estrDestroy(&pDataPointNameXpathResultEString);
		estrDestroy(&pCategoryNameEString);
		estrDestroy(&pCategoryNameXPathResultEString);
		estrDestroy(&pCountFieldXpathResultEString);
	}
	PERFINFO_AUTO_STOP();

}



static void Graph_GetTitleWithDates(Graph *pGraph, char **ppOutEString)
{
	char *pSimpleTitle = pGraph->pDefinition->pGraphTitle ? pGraph->pDefinition->pGraphTitle : pGraph->pGraphName;
	if (pGraph->iGraphCreationTime == 0)
	{
		estrCopy2(ppOutEString, pSimpleTitle);
	}
	else if (pGraph->bSavingStoredVersionRightNow)
	{
		estrCopy2(ppOutEString, pSimpleTitle);
		estrConcatf(ppOutEString, " (%s - ", timeGetLocalDateStringFromSecondsSince2000(pGraph->iGraphCreationTime));
		estrConcatf(ppOutEString, "%s)", timeGetLocalDateStringFromSecondsSince2000(pGraph->iGraphClearTime));
	}
	else
	{
		estrPrintf(ppOutEString, "%s (%s - )", pSimpleTitle, timeGetLocalDateStringFromSecondsSince2000(pGraph->iGraphCreationTime));
	}
}

static void WriteGnuPlotScriptAndDataFilesForBarGraph(Graph *pGraph, FILE *pScriptFile, FILE *pDataFile, char *pOutputFileName, char *pDataFileName)
{

	int iCatNum, iBarNum;
	char *pTitleEString = NULL;
	bool bPointNamesAreAllInts = true;
	int i;

	if (eaSize(&pGraph->ppCategoryNames))
	{
		fprintf(pDataFile, "UNUSED ");

		for (iCatNum = 0; iCatNum < eaSize(&pGraph->ppCategoryNames); iCatNum++)
		{
			int iCategoryInt;

			if (StringToInt(pGraph->ppCategoryNames[iCatNum], &iCategoryInt)
				&& iCategoryInt >= 0 && iCategoryInt < eaSize(&pGraph->ppCategoryNamesTableFromDef))
			{
				fprintf(pDataFile, "\"%s\" ", pGraph->ppCategoryNamesTableFromDef[iCategoryInt]);
			}
			else
			{
				fprintf(pDataFile, "\"%s\" ", pGraph->ppCategoryNames[iCatNum]);
			}
		}

		fprintf(pDataFile, "\n");
	}

	for (i=0; i < eaSize(&pGraph->ppDataPointNames); i++)
	{
		int temp;
		if (!StringToInt(pGraph->ppDataPointNames[i], &temp))
		{
			bPointNamesAreAllInts = false;
			break;
		}
	}

	if (bPointNamesAreAllInts)
	{
		Graph_SortDataPointNamesForOutput(pGraph);
	}


	for (iBarNum = 0; iBarNum < eaSize(&pGraph->ppDataPointNames); iBarNum++)
	{
		fprintf(pDataFile, "%s ", pGraph->ppDataPointNames[iBarNum]);

		if (pGraph->pOnlyCategory)
		{
			GraphDataPoint *pBar;
			if (stashFindPointer(pGraph->pOnlyCategory->dataPointsTable, pGraph->ppDataPointNames[iBarNum], &pBar))
			{
				switch (pGraph->pDefinition->eDataType)
				{
				case GRAPHDATATYPE_SUM:
					fprintf(pDataFile, "%f ", pBar->fSum);
					break;

				case GRAPHDATATYPE_COUNT:
					fprintf(pDataFile, "%d ", pBar->iCount);
					break;

				case GRAPHDATATYPE_AVERAGE:
					fprintf(pDataFile, "%f ", pBar->iCount ? pBar->fSum / (float)pBar->iCount : 0.f);
					break;
				}
			}
			else
			{
				fprintf(pDataFile, "0.0 ");
			}
		}
		else
		{
			for (iCatNum = 0; iCatNum < eaSize(&pGraph->ppCategoryNames); iCatNum++)
			{
				GraphCategory *pCategory;
				GraphDataPoint *pBar;

				if (!stashFindPointer(pGraph->categoriesTable, pGraph->ppCategoryNames[iCatNum], &pCategory))
				{
					assert(0);
				}

				if (stashFindPointer(pCategory->dataPointsTable, pGraph->ppDataPointNames[iBarNum], &pBar))
				{
					switch (pGraph->pDefinition->eDataType)
					{
					case GRAPHDATATYPE_SUM:
						fprintf(pDataFile, "%f ", pBar->fSum);
						break;

					case GRAPHDATATYPE_COUNT:
						fprintf(pDataFile, "%d ", pBar->iCount);
						break;

					case GRAPHDATATYPE_AVERAGE:
						fprintf(pDataFile, "%f ", pBar->iCount ? pBar->fSum / (float)pBar->iCount : 0.f);
						break;
					}
				}
				else
				{
					fprintf(pDataFile, "0.0 ");
				}
			}
		}

		fprintf(pDataFile, "\n");
	}

	estrStackCreate(&pTitleEString);
	Graph_GetTitleWithDates(pGraph, &pTitleEString);
	fprintf(pScriptFile, "set terminal jpeg small font arial size 1200,800\nset output \"%s\"\nset title \"%s\"\nset xtics rotate by -45\nset style data histogram\nset yrange [0:]\nset style fill solid border -1\n",
		pOutputFileName, pTitleEString );
	estrDestroy(&pTitleEString);

	if (pGraph->pOnlyCategory)
	{
		fprintf(pScriptFile, "plot \"%s\" using 2:xtic(1) notitle\n", pDataFileName);
	}
	else
	{
		for (iCatNum = 0; iCatNum < eaSize(&pGraph->ppCategoryNames); iCatNum++)
		{

			GraphCategory *pCategory;

			if (iCatNum == 0)
			{
				fprintf(pScriptFile, "plot \"%s\" using %d:xtic(1) title %d",
					pDataFileName, iCatNum + 2, iCatNum + 2);
			}
			else
			{
				fprintf(pScriptFile, ", \"\" using %d title %d",
					iCatNum + 2, iCatNum + 2);
			}

			
			if (stashFindPointer(pGraph->categoriesTable, pGraph->ppCategoryNames[iCatNum], &pCategory))
			{
				if (pCategory->pColorName)
				{
					fprintf(pScriptFile, " lt rgbcolor \"%s\" ", pCategory->pColorName);
				}				
			}
		}

		fprintf(pScriptFile, "\n");
	}
}



static bool WriteGnuPlotScriptAndDataFilesForLines(Graph *pGraph, FILE *pScriptFile, FILE *pDataFile, char *pOutputFileName, char *pDataFileName, char **ppErrorEString)
{
	int iCatNum, iPointNum;
	char *pTitleEString = NULL;

	int iNumCategories;
	bool bImpliedCategories = false;

	if (GraphHasImpliedNumberofCategories(pGraph, &iNumCategories, false))
	{
		iNumCategories = 3;
		bImpliedCategories = true;
		assertmsgf(pGraph->pDefinition->pCategoryField == NULL, "Graph %s has a data type which implies categories (like MIN_MAX_AVG or MEDIANS) but also has a category field. That is not allowed", pGraph->pGraphName);
	}
	else if (eaSize(&pGraph->ppCategoryNames))
	{
		iNumCategories = eaSize(&pGraph->ppCategoryNames);
	}
	else
	{
		iNumCategories = 1;
	}

	for (iCatNum = 0; iCatNum < iNumCategories; iCatNum++)
	{
		GraphCategory *pCategory;

		if (bImpliedCategories)
		{
			pCategory = pGraph->pOnlyCategory;
		}
		else if (eaSize(&pGraph->ppCategoryNames))
		{
			if (!stashFindPointer(pGraph->categoriesTable, pGraph->ppCategoryNames[iCatNum], &pCategory))
			{
				continue;
			}
		}
		else
		{
			pCategory = pGraph->pOnlyCategory;
		}

		for (iPointNum = 0; iPointNum < eaSize(&pGraph->ppDataPointNames); iPointNum++)
		{
			int iDataPointNameInt;
			GraphDataPoint *pDataPoint;

			if (stashFindPointer(pCategory->dataPointsTable, pGraph->ppDataPointNames[iPointNum], &pDataPoint))
			{
				if (StringToInt(pGraph->ppDataPointNames[iPointNum], &iDataPointNameInt))
				{
					fprintf(pDataFile, "%d ", iDataPointNameInt);
				}
				else
				{
					estrPrintf(ppErrorEString, "data point name %s found for a line graph... line graphs require integer data points", pGraph->ppDataPointNames[iPointNum]);
					return 0;
				}

				switch (pGraph->pDefinition->eDataType)
				{
				case GRAPHDATATYPE_SUM:
					fprintf(pDataFile, " %f\n", pDataPoint->fSum);
					break;

				case GRAPHDATATYPE_COUNT:
					fprintf(pDataFile, " %d\n", pDataPoint->iCount);
					break;

				case GRAPHDATATYPE_AVERAGE:
					fprintf(pDataFile, " %f\n", pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f);
					break;
				case GRAPHDATATYPE_MIN_MAX_AVG:
					switch (iCatNum)
					{
					case 0:
						fprintf(pDataFile, " %f\n", pDataPoint->fMin);
						break;
					case 1:
						fprintf(pDataFile, " %f\n", pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f);
						break;
					case 2:
						fprintf(pDataFile, " %f\n", pDataPoint->fMax);
						break;
					}
					break;
				case GRAPHDATATYPE_MEDIANS:
					switch (iCatNum)
					{
					case 0:
						fprintf(pDataFile, " %f\n", MedianTracker_GetMedian(pDataPoint->pTracker, 0.1f));
						break;
					case 1:
						fprintf(pDataFile, " %f\n", MedianTracker_GetMedian(pDataPoint->pTracker, 0.5f));
						break;
					case 2:
						fprintf(pDataFile, " %f\n", MedianTracker_GetMedian(pDataPoint->pTracker, 0.9f));
						break;
					}
					break;
				}
			}
		}

		fprintf(pDataFile, "\n\n");
	}



	estrStackCreate(&pTitleEString);
	Graph_GetTitleWithDates(pGraph, &pTitleEString);
	fprintf(pScriptFile, "set terminal jpeg small font arial size 1200,800\nset output \"%s\"\nset title \"%s\"\nset yrange [0:]\n",
		pOutputFileName, pTitleEString );
	estrDestroy(&pTitleEString);

	//special case for line graphs based on time... label axes better
	if (stricmp(pGraph->pDefinition->pBarField, ".time") == 0 || strStartsWith(pGraph->pDefinition->pBarField, "TIME_ROUNDED("))
	{
		NamedTime **ppTimes = NULL;
		int i;

		fprintf(pScriptFile, "set xtics (");


		timeGetLogicallyNamedIntervals(pGraph->iGraphCreationTime, pGraph->iMostRecentDataTime, 3, &ppTimes);

		for (i=0 ; i < eaSize(&ppTimes); i++)
		{
			fprintf(pScriptFile, "%s\"%s\" %u",
				i == 0 ? "" : ", ", ppTimes[i]->name, ppTimes[i]->iTime);
		}

		fprintf(pScriptFile, ")\n");

		eaDestroyEx(&ppTimes, NULL);

	}

	if (bImpliedCategories)
	{
		int i;	
	

		fprintf(pScriptFile, "plot \"%s\" index 0 smooth unique title \"%s\" with lines linewidth 3 ",
			pDataFileName, GetImpliedCategoryName(pGraph, 0, false));

		for (i=1; i < iNumCategories; i++)
		{
			fprintf(pScriptFile, ", \"\" index %d smooth unique title \"%s\" with lines linewidth 3 ",
				i, GetImpliedCategoryName(pGraph, i, false));
		}

		fprintf(pScriptFile, "\n");
	}
	else if (pGraph->pOnlyCategory)
	{
		fprintf(pScriptFile, "plot \"%s\" index 0 smooth unique notitle with lines linewidth 3 \n", pDataFileName);
	}
	else
	{
		for (iCatNum = 0; iCatNum < eaSize(&pGraph->ppCategoryNames); iCatNum++)
		{
			char *pCategoryNameToUse;
			int iCategoryInt;

			if (StringToInt(pGraph->ppCategoryNames[iCatNum], &iCategoryInt)
				&& iCategoryInt >= 0 && iCategoryInt < eaSize(&pGraph->ppCategoryNamesTableFromDef))
			{
				pCategoryNameToUse = pGraph->ppCategoryNamesTableFromDef[iCategoryInt];
			}
			else
			{
				pCategoryNameToUse = pGraph->ppCategoryNames[iCatNum];
			}

			if (iCatNum == 0)
			{
				fprintf(pScriptFile, "plot \"%s\" index %d smooth unique title \"%s\" with lines linewidth 3 ",
					pDataFileName, iCatNum, pCategoryNameToUse);
			}
			else
			{
				fprintf(pScriptFile, ", \"\" index %d smooth unique title \"%s\" with lines linewidth 3 ",
					iCatNum, pCategoryNameToUse);
			}
		}

		fprintf(pScriptFile, "\n");
	}

	return 1;
}

static void Graph_JpegCB(char *pName, UrlArgumentList *pUrlArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	char dupName[CRYPTIC_MAX_PATH + 200];
	
	char *pLastPeriod;
	Graph *pGraph;
	bool bForceLive = false;

	if (strStartsWith(pName, "LIVE_"))
	{
		bForceLive = true;
		strcpy(dupName, pName + 5);
	}
	else
	{
		strcpy(dupName, pName);
	}

	if (!strEndsWith(pName, ".jpg"))
	{
		pCB(NULL, 0, 0, "Bad syntax in heatmap jpeg name", pUserData);
		return;
	}


	pLastPeriod = strrchr(dupName, '.');
	*pLastPeriod = 0;

	if (!stashFindPointer(sGraphsByGraphName, dupName, &pGraph))
	{
		return;
	}
	else
	{
		char dataFileName[CRYPTIC_MAX_PATH];
		char scriptFileName[CRYPTIC_MAX_PATH];
		char outputFileName[CRYPTIC_MAX_PATH];
		char *pSystemString = NULL;
		FILE *pDataFile;
		FILE *pScriptFile;
		int iIndex = 0;
		char *pBuf;
		int iBufSize;

		if (pGraph->pLastFullGraphStoredJpeg && !bForceLive)
		{
			pCB(pGraph->pLastFullGraphStoredJpeg, pGraph->iLastFullGraphDataSize,
				pGraph->pDefinition->iGraphLifespan / 2, NULL, pUserData);
			return;
		}


		if (!eaSize(&pGraph->ppDataPointNames))
		{
			pCB(NULL, 0, 0, "No data in requested graph", pUserData);
			return;
		}

		sprintf(outputFileName, "%s/%sGraph.jpg", fullTempDir(), dupName);
		sprintf(dataFileName, "%s/%sGraphData.txt", fullTempDir(), dupName);
	
		mkdirtree(outputFileName);

		pDataFile = fopen(dataFileName, "wt");

		if (!pDataFile)
		{
			pCB(NULL, 0, 0, "Couldn't open temp file for data writing", pUserData);
			return;
		}
		
		sprintf(scriptFileName, "%s/%sGraphScript.txt", fullTempDir(), dupName);
		pScriptFile = fopen(scriptFileName, "wt");
	
		if (!pScriptFile)
		{
			fclose(pDataFile);
			pCB(NULL, 0, 0, "Couldn't open temp file for script writing", pUserData);
			return;
		}

		switch (pGraph->pDefinition->eDisplayType)
		{
		case GRAPHDISPLAYTYPE_BARGRAPH:
			WriteGnuPlotScriptAndDataFilesForBarGraph(pGraph, pScriptFile, pDataFile, outputFileName, dataFileName);
			break;
		case GRAPHDISPLAYTYPE_LINES:
			{
				char *pErrorString = NULL;
				if (!WriteGnuPlotScriptAndDataFilesForLines(pGraph, pScriptFile, pDataFile, outputFileName, dataFileName, &pErrorString))
				{
					fclose(pScriptFile);
					fclose(pDataFile);
					pCB(NULL, 0, 0, pErrorString, pUserData);
					estrDestroy(&pErrorString);
					return;
				}
			}
			break;
		}

		fclose(pScriptFile);
		fclose(pDataFile);
	

		estrStackCreate(&pSystemString);
		estrPrintf(&pSystemString, "%s\\wgnuplot %s", fileCoreToolsBinDir(), scriptFileName);
		backSlashes(pSystemString);

		printf("About to try to execute %s\n", pSystemString);
		system(pSystemString);
		estrDestroy(&pSystemString);

		pBuf = fileAlloc(outputFileName, &iBufSize);

		if (!pBuf || !iBufSize)
		{
			pCB(NULL, 0, 0, "Couldn't write graph file for unknown reason", pUserData);
			return;
		}

		pCB(pBuf, iBufSize, gbStandAlone ? 1 : 5, NULL, pUserData);

		if (pBuf)
		{
			free(pBuf);
		}
		return;
	}

	pCB(NULL, 0, 0, "Unknown graph requested", pUserData);
}


static void AddGraphToLists(Graph *pGraph)
{
	char **ppActionNames = NULL;
	char **ppFileNamesToMatch = NULL;
	int i;

	stashAddPointer(sGraphsByGraphName, pGraph->pGraphName, pGraph, false);

	DivideString(pGraph->pDefinition->pActionName, ",;", &ppActionNames,
		DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_ALLOCADD);
	
	for (i=0; i < eaSize(&ppActionNames); i++)
	{
		RegisterActionSpecificCallback(ppActionNames[i], NULL, pGraph->pGraphName, Graph_AddLogToGraph, pGraph, &pGraph->pDefinition->useThisLog,
			ASC_GRAPH);
	}

	DivideString(pGraph->pDefinition->pFilenamesNeeded, ",", &ppFileNamesToMatch, 
		DIVIDESTRING_POSTPROCESS_ALLOCADD | DIVIDESTRING_POSTPROCESS_REMOVEUNIQUE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	for (i=0; i < eaSize(&ppFileNamesToMatch); i++)
	{
		RegisterFileNameToMatch(ppFileNamesToMatch[i]);
	}

	eaDestroy(&ppActionNames);
	eaDestroy(&ppFileNamesToMatch);
}

static void GraphDestructor(Graph *pGraph)
{
	StructDestroy(parse_Graph, pGraph);
}

static void ClearAllGraphLists(void)
{
	eaDestroy(&sAllNonLongTermGraphList.ppGraphs);
	stashTableDestroyEx(sGraphsByGraphName, NULL, GraphDestructor);
	sGraphsByGraphName = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);

}

static void GetGraphStatusFileName(char *pString, int pString_size)
{
	sprintf_s(SAFESTR2(pString), "%s/LogParser_GraphStatus.txt", LogParserLocalDataDir());
}

static void GetLogParserTempStatusFileName(char *pString, int pString_size)
{
	sprintf_s(SAFESTR2(pString), "%s/LogParser_GraphStatus.txt.temp", LogParserLocalDataDir());
}

static void GetLogParserBackupStatusFileName(char *pString, int pString_size)
{
	sprintf_s(SAFESTR2(pString), "%s/LogParser_GraphStatus.txt.bak", LogParserLocalDataDir());
}


static void InitAllGraphsFromConfig(void)
{
	int i;

	for (i=0; i < eaSize(&gLogParserConfig.ppGraphs); i++)
	{
		Graph *pGraph;

	//skip creating graphs which already were loaded in
		if (gLogParserConfig.ppGraphs[i]->bThisGraphAlreadyExists)
		{
			continue;
		}

		if (gLogParserConfig.ppGraphs[i]->bDisableAtClusterLevel && LogParser_ClusterLevel_GotShardNames())
		{
			continue;
		}

		if (stashFindPointer(sGraphsByGraphName, gLogParserConfig.ppGraphs[i]->pGraphName, NULL))
		{
			Errorf("Two graphs with the same name %s", gLogParserConfig.ppGraphs[i]->pGraphName);
		}
		else
		{
			

			pGraph = StructCreate(parse_Graph);

//			estrPrintf(&pGraph->pImageLink, "<span><a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_%s.jpg\" target=\"_blank\">%s</a></span>\n", gLogParserConfig.ppGraphs[i]->pGraphName, gLogParserConfig.ppGraphs[i]->pGraphName);

			pGraph->pDefinition = StructCreate(parse_GraphDefinition);
			StructCopyAll(parse_GraphDefinition, gLogParserConfig.ppGraphs[i], pGraph->pDefinition);

			pGraph->pGraphName = strdup(gLogParserConfig.ppGraphs[i]->pGraphName);

			if (pGraph->pDefinition->pCategoryNames)
			{
				DivideString(pGraph->pDefinition->pCategoryNames, ",", &pGraph->ppCategoryNamesTableFromDef, DIVIDESTRING_POSTPROCESS_NONE);
			}

			FindOldDataForGraph(pGraph);
			AddGraphToLists(pGraph);

			if (!pGraph->pDefinition->bIAmALongTermGraph)
			{
				eaPush(&sAllNonLongTermGraphList.ppGraphs, pGraph);
			}
		}
	}



}


void Graph_FullyResetSystem(void)
{
	Graph_ResetSystem();
	ClearAllGraphLists();
	InitAllGraphsFromConfig();
}


//returns true if the definition was found, false otherwise
static bool FindAndMarkGraphDefinition(GraphDefinition *pDefinition)
{
	int i;

	for (i = eaSize(&gLogParserConfig.ppGraphs)-1 ; i >= 0 ; i--)
	{
		GraphDefinition *pSourceDef = gLogParserConfig.ppGraphs[i];

		if (StructCompare(parse_GraphDefinition, pDefinition, pSourceDef, 0, 0, TOK_LOGPARSER_MUTABLE) == 0)
		{
			StructCopy(parse_GraphDefinition, pSourceDef, pDefinition, 0, 0, 0);
			pSourceDef->bThisGraphAlreadyExists = true;
			return true;
		}
	}

	return false;
}




static char *GetFlotColorStringForFlotCategoryName(char *pName, int iCatNum)
{
	static char **sppCSSColorNames = NULL;
	int i;
	static char returnBuf[256];

	if (stricmp(pName, "white") == 0)
	{
		return " color: \"lightgrey\", ";
	}

	if (!sppCSSColorNames)
	{
		char **ppNames = NULL;
		char *pFullString = fileAlloc("server/mcp/static_home/css/csscolornames.txt", NULL);

		if (!pFullString)
		{
			return "";
		}

		DivideString(pFullString, "\n", &sppCSSColorNames,
			DIVIDESTRING_POSTPROCESS_ALLOCADD_CASESENSITIVE | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

		free(pFullString);
	}

	assert(sppCSSColorNames);

	for (i=0; i < eaSize(&sppCSSColorNames); i++)
	{
		if (stricmp(pName, sppCSSColorNames[i]) == 0)
		{
			sprintf(returnBuf, " c \"%s\", ", sppCSSColorNames[i]);
			return returnBuf;
		}
	}

	// If we use up all the colors, wrap around.
	iCatNum %= eaSize(&sppCSSColorNames);

	sprintf(returnBuf, " color: \"%s\", ", sppCSSColorNames[iCatNum]);
	return returnBuf;
}

static bool GetFlotDataStringForGraph(char **ppOutEstring, Graph *pGraph, char **ppErrorEString, UrlArgumentList *pArgList)
{
	int iCatNum, iPointNum;

	int iNumCategories;

	int iSkipVal = 0;

	U32 iStartTime = 0;
	U32 iEndTime = 0;
	bool bAddedOptions = false;

	const char *pTimeString = urlFindValue(pArgList, "svrGraphTime");
	if (pTimeString)
	{
		char *pComma = strchr(pTimeString, ',');
		if (pComma)
		{
			iStartTime = atoi(pTimeString);
			iEndTime = atoi(pComma+1);
		}
	}

	urlFindBoundedInt(pArgList, "svrGraphSkip", &iSkipVal, 0, 1000);

	


	Graph_SortDataPointNamesForOutput(pGraph);



	iNumCategories = Graph_GetNumOutputCategories(pGraph, false);


	estrPrintf(ppOutEstring, "{\n\"data\": {\n");

	for (iCatNum = 0; iCatNum < iNumCategories; iCatNum++)
	{
		GraphCategory *pCategory = Graph_GetNthOutputCategory(pGraph, iCatNum, false);
		char *pCategoryNameString = Graph_GetNthOutputCategoryName(pGraph, iCatNum, false);
		bool bFirstPoint = true;
		char *pTypeString;
		char *pColorString = GetFlotColorStringForFlotCategoryName(pCategoryNameString, iCatNum);

		if(!pCategory)
			continue;

		switch (pGraph->pDefinition->eDisplayType)
		{
			case GRAPHDISPLAYTYPE_BARGRAPH:
				pTypeString = "bars: { show: true }, ";
				break;
			case GRAPHDISPLAYTYPE_LINES:
				pTypeString = "";
				break;
			case GRAPHDISPLAYTYPE_POINTS:
				pTypeString = "points: { show: true }, ";
				break;
			case GRAPHDISPLAYTYPE_LINESWITHPOINTS:
				pTypeString = "lines: { show: true }, points: { show: true }, ";
				break;
			default:
				// Unsupported;
				devassert(0);
		}

		estrConcatf(ppOutEstring, "%s \"%s\": { label: \"%s\", %s%sdata: [", iCatNum == 0 ? "" : ", ", pCategoryNameString, pCategoryNameString, pTypeString, pColorString);
		

		for (iPointNum = 0; iPointNum < eaSize(&pGraph->ppDataPointNames); iPointNum++)
		{
			GraphDataPoint *pDataPoint;

			if (iSkipVal)
			{
				if (iPointNum % iSkipVal != 0)
				{
					continue;
				}
			}

			if (stashFindPointer(pCategory->dataPointsTable, pGraph->ppDataPointNames[iPointNum], &pDataPoint))
			{
				U32 iTime=0;
				float fValue;
		
				if (GraphUsesTime(pGraph))
				{
					iTime = atoi(pGraph->ppDataPointNames[iPointNum]);

					if (iStartTime)
					{
						if (iTime < iStartTime)
						{
							continue;
						}
					}

					if (iEndTime)
					{
						if (iTime > iEndTime)
						{
							continue;
						}
					}
				}

				// Find Y value.
				switch (pGraph->pDefinition->eDataType)
				{
				case GRAPHDATATYPE_SUM:
					fValue = pDataPoint->fSum;
					break;
				case GRAPHDATATYPE_COUNT:
					fValue = pDataPoint->iCount;
					break;
				case GRAPHDATATYPE_AVERAGE:
					fValue = pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f;
					break;
				case GRAPHDATATYPE_MIN_MAX_AVG:
					switch (iCatNum)
					{
					case 0:
						fValue = pDataPoint->fMin;
						break;
					case 1:
						fValue = pDataPoint->iCount ? pDataPoint->fSum / (float)pDataPoint->iCount : 0.f;
						break;
					case 2:
						fValue = pDataPoint->fMax;
						break;
					}
					break;
				case GRAPHDATATYPE_MEDIANS:
					switch (iCatNum)
					{
					case 0:
						fValue = MedianTracker_GetMedian(pDataPoint->pTracker, 0.1f);
						break;
					case 1:
						fValue = MedianTracker_GetMedian(pDataPoint->pTracker, 0.5f);
						break;
					case 2:
						fValue = MedianTracker_GetMedian(pDataPoint->pTracker, 0.9f);
						break;
					}
					break;
				default:
					devassert(0);
				}

				// Make sure value is in range for this type of graph.
				if (pGraph->pDefinition->bLogScaleY && fValue <= 0)
					continue;

				// Start printing data point.
				estrConcatf(ppOutEstring, "%s[", bFirstPoint ? "" : ", ");
				bFirstPoint = false;

				// Print X value.
				if (iTime)
				{
					estrConcatf(ppOutEstring, "%"FORM_LL"u, ", 1000 * (U64)(timeLocalOffsetFromUTC() + timeSecondsSince2000ToPatchFileTime(iTime)));
				}
				else
				{
					if (pGraph->pDefinition->eDisplayType == GRAPHDISPLAYTYPE_BARGRAPH)
					{
						estrConcatf(ppOutEstring, "%d, ", iPointNum * (iNumCategories+1) + iCatNum );
					}
					else
					{
						float fTempFloat;
						if (StringToFloat(pGraph->ppDataPointNames[iPointNum], &fTempFloat))
						{
							estrConcatf(ppOutEstring, "%s, ", pGraph->ppDataPointNames[iPointNum]);
						}
						else
						{
							estrPrintf(ppErrorEString, "non-bargraph had non-float point num");
							return false;
						}
					}
				}

				// Print Y value.
				estrConcatf(ppOutEstring, " %f]", fValue);
			}

			if (iPointNum % 16 == 0)
			{
				estrConcatf(ppOutEstring, "\n\t");
			}

		}

		estrConcatf(ppOutEstring, "] }\n");
	}

	estrConcatf(ppOutEstring, "},");

	estrConcatf(ppOutEstring, "\t\"time\":%s", GraphUsesTime(pGraph) ? "true":"false");

	estrAppend2(ppOutEstring, ",\n\t\"options\": { ");

	// X axis options
	if (pGraph->pDefinition->eDisplayType == GRAPHDISPLAYTYPE_BARGRAPH)
	{
		// Add ticks for each bar.
		Graph_SortDataPointNamesForOutput(pGraph);
		estrAppend2(ppOutEstring, "xaxis: {ticks: [");
		for (iPointNum = 0; iPointNum < eaSize(&pGraph->ppDataPointNames); iPointNum++)
		{
			estrConcatf(ppOutEstring, "%s[%f, \"%s\"]", iPointNum == 0  ? "" : ", ", ((float)iPointNum * (iNumCategories + 1)) + ((float)iNumCategories) / 2.0f, pGraph->ppDataPointNames[iPointNum]);			
		}
		estrAppend2(ppOutEstring, "] }");
		bAddedOptions = true;
	} else if (pGraph->pDefinition->bLogScaleX)
	{
		// Log scale for X axis
		estrAppend2(ppOutEstring,
			"xaxis: {"
				"transform: function (v) { return Math.log(v)/Math.log(10); },"
				"inverseTransform: function (v) { return Math.pow(10, v); }"
			"}");
		bAddedOptions = true;
	}

	// Y axis options
	if (pGraph->pDefinition->bLogScaleY)
	{
		// Log scale for Y axis
		if (bAddedOptions)
			estrAppend2(ppOutEstring, ", ");
		estrAppend2(ppOutEstring,
			"yaxis: {"
				"transform: function (v) { return Math.log(v)/Math.log(10); },"
				"inverseTransform: function (v) { return Math.pow(10, v); },"
				"autoscaleMargin: null"
			"}");
		bAddedOptions = true;
	}

	estrAppend2(ppOutEstring, " } }");

	return true;
}

//if pPreCalcedDataString is set, use it. Otherwise, recalculate it.
static bool GetFullyWrappedFlotStringForGraph(Graph *pGraph, char **ppOutString, char **ppErrorString, bool bStandaloneHTMLFileMode, char *pPreCalcedDataString, U32 iTime, UrlArgumentList *pArgList)
{
	bool bRetVal = true;
	char *pDataURLString = NULL;
	char *pDataString = NULL;
	char *pDescriptionString = NULL;
	char *pTitleString = NULL;

	if (!pGraph->pDefinition->pTemplateString)
	{
		char *pTemplateFileName;

		if (pGraph->pDefinition->eDisplayType == GRAPHDISPLAYTYPE_BARGRAPH)
		{
			pTemplateFileName = "server/MCP/templates/flotBasicTemplate.txt";
		}
		else
		{
			pTemplateFileName = "server/MCP/templates/flotSelectableLineTemplate.txt";
		}
	
		pGraph->pDefinition->pTemplateString = fileAlloc(pTemplateFileName, NULL);

		if (!pGraph->pDefinition->pTemplateString)
		{
			estrPrintf(ppErrorString, "Couldn't load flot template file %s", pTemplateFileName);
			bRetVal = false;
			goto done;
		}
	}


	if (bStandaloneHTMLFileMode)
	{
		if (pPreCalcedDataString)
		{
			estrConcatf(&pDataString, "%s", pPreCalcedDataString);
		}
		else
		{
			if (!GetFlotDataStringForGraph(&pDataString, pGraph, ppErrorString, NULL))
			{
				bRetVal = false;
				goto done;
			}
		}

		estrPrintf(&pDataURLString, "\"\"");
	}
	else
	{
		estrPrintf(&pDataString, "null");
		estrPrintf(&pDataURLString, "\"%s.graphData[%s].%u&update=1&nocache=1", LinkToThisServer(), pGraph->pGraphName, iTime);
		if (pArgList)
		{
			int i;

			for (i=0; i < eaSize(&pArgList->ppUrlArgList); i++)
			{
				if (strStartsWith(pArgList->ppUrlArgList[i]->arg, "svrGraph"))
				{
					estrConcatf(&pDataURLString, "&%s=%s", pArgList->ppUrlArgList[i]->arg, pArgList->ppUrlArgList[i]->value);
				}
			}
		}

		estrConcatf(&pDataURLString, "\"");
	}

	estrPrintf(&pDescriptionString, "(%s - ",
		timeGetLocalDateStringFromSecondsSince2000(pGraph->iGraphCreationTime));
	estrConcatf(&pDescriptionString, "%s)",
		timeGetLocalDateStringFromSecondsSince2000(pGraph->iMostRecentDataTime));

	estrConcatf(ppOutString, "%s", pGraph->pDefinition->pTemplateString);
	estrReplaceOccurrences(ppOutString, "$DATAURL$", pDataURLString);
	estrReplaceOccurrences(ppOutString, "$DATA$", pDataString);
	estrReplaceOccurrences(ppOutString, "$TITLE$", pGraph->pDefinition->pGraphTitle && pGraph->pDefinition->pGraphTitle[0] ? pGraph->pDefinition->pGraphTitle : pGraph->pGraphName);
	estrReplaceOccurrences(ppOutString, "$DESCRIPTION$", pDescriptionString);
	estrReplaceOccurrences(ppOutString, "$EXTRA$", pGraph->pDefinition->pGraphExtra && *pGraph->pDefinition->pGraphExtra
		? pGraph->pDefinition->pGraphExtra : "");

done:
	estrDestroy(&pDataString);
	estrDestroy(&pDataURLString);
	estrDestroy(&pDescriptionString);

	return bRetVal;
}




//if pDataString is set, then use it. Otherwise, recalculate it.
static void WriteOutFlotTextFileForGraph(char *pFileName, Graph *pGraph, char *pDataString)
{
	static char *pHeader = NULL;
	static char *pFooter = NULL;

	char *pOutString = NULL;
	char *pErrorString = NULL;


	if (!pHeader)
	{
		pHeader = fileAlloc("server/MCP/templates/MCPHtmlHeader_ForStandaloneFiles.txt", NULL);
	}

	if (!pFooter)
	{
		pFooter = fileAlloc("server/MCP/templates/MCPHtmlFooter_ForStandaloneFiles.txt", NULL);
	}

	if (!pHeader || !pFooter)
	{
		return;
	}

	estrAppend2(&pOutString, pHeader);
	if (GetFullyWrappedFlotStringForGraph(pGraph, &pOutString, &pErrorString, true, pDataString, 0, NULL))
	{
		FILE *pOutFile = fopen(pFileName, "wt");

		if (pOutFile)
		{
			fprintf(pOutFile, "%s", pOutString);
			fclose(pOutFile);
		}
	}

	estrDestroy(&pOutString);
	estrDestroy(&pErrorString);
}


//.graphdata[graphname].time. time=0 means live data
static bool ProcessGraphDataIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pDataString = NULL;
	char *pErrorString = NULL;

	char *pFirstCloseBracket;

	Graph *pGraph;
	U32 iTime = 0;

	if (pLocalXPath[0] != '[' || (pFirstCloseBracket = strchr(pLocalXPath, ']')) == NULL)
	{
		GetMessageForHttpXpath("Error - expected .graphdata[graphname]", pStructInfo, 1);
		return true;
	}

	if (pFirstCloseBracket[1] == '.')
	{
		iTime = atoi(pFirstCloseBracket+2);
	}

	*pFirstCloseBracket = 0;

	if (!stashFindPointer(sGraphsByGraphName, pLocalXPath+1, &pGraph))
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - unknown graph %s\n", pLocalXPath+1), pStructInfo, 1);
		return true;
	}


	if (iTime)
	{
		char dataFileName[CRYPTIC_MAX_PATH];
		char *pDataFileString;

		sprintf(dataFileName, "%s/%s/OldData/%uData.txt", GetGraphDirName(), pGraph->pGraphName, iTime);

		pDataFileString = fileAlloc(dataFileName, NULL);

		if (!pDataFileString)
		{
			GetMessageForHttpXpath(STACK_SPRINTF("Couldn't load file %s", dataFileName), pStructInfo, 1);
			return true;
		}

		estrConcatf(&pDataString, "%s", pDataFileString);

		free(pDataFileString);
	}
	else
	{

		if (!GetFlotDataStringForGraph(&pDataString, pGraph, &pErrorString, pArgList))
		{
			GetMessageForHttpXpath(pErrorString, pStructInfo, 1);
			estrDestroy(&pErrorString);
			estrDestroy(&pDataString);
			return true;
		}
	}

	GetRawHTMLForHttpXpath(pDataString, pStructInfo);

	estrDestroy(&pDataString);
	
	return true;
}

static bool ProcessCSVIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pErrorString = NULL;

	char *pFirstCloseBracket;
	bool bLive;
	U32 iTime = 0;
	Graph *pGraph;
	char *pDataString = NULL;

	if (pLocalXPath[0] != '[' || (pFirstCloseBracket = strchr(pLocalXPath, ']')) == NULL)
	{
		GetMessageForHttpXpath("Error - expected .csv[graphname].live/nnnnnnnnnn(time)", pStructInfo, 1);
		return true;
	}

	bLive = strStartsWith(pFirstCloseBracket + 1, ".live");
	if (pFirstCloseBracket[1] == '.')
	{
		iTime = atoi(pFirstCloseBracket + 2);
	}

	if (!(bLive || iTime))
	{
		GetMessageForHttpXpath("Error - expected .csv[graphname].live/nnnnnnnnnn(time)", pStructInfo, 1);
		return true;
	}

	*pFirstCloseBracket = 0;


	if (!stashFindPointer(sGraphsByGraphName, pLocalXPath+1, &pGraph))
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - unknown graph %s\n", pLocalXPath+1), pStructInfo, 1);
		return true;
	}

	if (bLive)
	{
		char *pWorkString = NULL;

		estrPrintf(&pWorkString, "<pre>\n");

		Graph_WriteCSV(pGraph, &pWorkString);

		estrConcatf(&pWorkString, "</pre>\n");

		GetRawHTMLForHttpXpath(pWorkString, pStructInfo);

		estrDestroy(&pWorkString);

		return true;
	}
	else
	{
		char fileName[CRYPTIC_MAX_PATH];
		char *pDateString = NULL;

		char *pFileString;
		char *pWorkString = NULL;

		estrCopy2(&pDateString, timeGetLocalDateStringFromSecondsSince2000(iTime));
		estrMakeAllAlphaNumAndUnderscores(&pDateString);
		sprintf(fileName, "%s/%s/OldCSVs/%s.txt", GetGraphDirName(), pGraph->pGraphName, pDateString);
		estrDestroy(&pDateString);

		pFileString = fileAlloc(fileName, NULL);

		if (!pFileString)
		{
			GetMessageForHttpXpath(STACK_SPRINTF("Couldn't open file %s", fileName), pStructInfo, 1);
			return true;
		}

		estrPrintf(&pWorkString, "<pre>\n%s\n</pre>\n", pFileString);

		GetRawHTMLForHttpXpath(pWorkString, pStructInfo);
		estrDestroy(&pWorkString);
		free(pFileString);
		return true;
	}
}


AUTO_STRUCT;
typedef struct ListOfLinks
{
	char *pWarning; AST(ESTRING, FORMATSTRING(HTML_CLASS="structheader"))
	char **ppLinks; AST(FORMATSTRING(HTML=1, HTML_NO_HEADER=1))


} ListOfLinks;

static void GetLinksForGraphWithTooManyPointsForFlot(Graph *pGraph, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pTemp = NULL;
	static ListOfLinks list = {0};

	StructReset(parse_ListOfLinks, &list);

	estrPrintf(&list.pWarning, "You wish to display graph %s, which currently has %d data points. Graphs of more than %d points may be slow and unresponsive when graphed with Flot, our interactive Javascript graph package. In fact, they may not show up at all. What would you like to do?",
		pGraph->pGraphName, pGraph->iUniqueDataPoints, MAX_FLOT_POINTS);

	estrStackCreate(&pTemp);
	estrPrintf(&pTemp, "<a href=\"%s.graph[%s].live&svrGraphConfirm=1\">Show the graph with Flot anyhow</a>", LinkToThisServer(), pGraph->pGraphName);
	eaPush(&list.ppLinks, strdup(pTemp));

	estrPrintf(&pTemp, "<a href=\"/viewimage?imageName=LOGPARSER_0_GRAPH_LIVE_%s.jpg\" target=\"_blank\">View the graph as a non-interactive .jpeg</a>", pGraph->pGraphName);
	eaPush(&list.ppLinks, strdup(pTemp));

	estrPrintf(&pTemp, "<a href=\"%s.graph[%s].live&svrGraphConfirm=1&svrGraphSkip=%d\">Show the graph in Flot displaying only one data point in %d</a>", LinkToThisServer(), pGraph->pGraphName, pGraph->iUniqueDataPoints / MAX_FLOT_POINTS + 1, pGraph->iUniqueDataPoints / MAX_FLOT_POINTS + 1);
	eaPush(&list.ppLinks, strdup(pTemp));

	if (GraphUsesTime(pGraph))
	{
		int i;
		int iNumPartitions = pGraph->iUniqueDataPoints / MAX_FLOT_POINTS + 1;

		for (i=0; i < iNumPartitions * 2 - 1; i++)
		{
			int j;
			float fStart = i * (1.0f / iNumPartitions) / 2;
			float fEnd = fStart + (1.0f / iNumPartitions); 

			U32 iStartTime = (pGraph->iMostRecentDataTime - pGraph->iGraphCreationTime) * fStart + pGraph->iGraphCreationTime;
			U32 iEndTime = (pGraph->iMostRecentDataTime - pGraph->iGraphCreationTime) * fEnd + pGraph->iGraphCreationTime;

			estrPrintf(&pTemp, "<a href=\"%s.graph[%s].live&svrGraphConfirm=1&svrGraphTime=%u,%u\">Display only this section of the graph, by time: ",
				LinkToThisServer(), pGraph->pGraphName, iStartTime, iEndTime);

			for (j = 0; j < 36; j++)
			{
				float t = (float)j / 36.0f;
				if (t >= fStart && t <= fEnd)
				{
					estrConcatf(&pTemp, "*");
				}
				else
				{
					estrConcatf(&pTemp, "-");
				}
			}
			estrConcatf(&pTemp, " (%s - ", timeGetLocalDateStringFromSecondsSince2000(iStartTime));
			estrConcatf(&pTemp, "%s)</a>", timeGetLocalDateStringFromSecondsSince2000(iEndTime));

			eaPush(&list.ppLinks, strdup(pTemp));
		}
	}


		



	ProcessStructIntoStructInfoForHttp("", NULL, &list, parse_ListOfLinks, 0, 0, pStructInfo, eFlags);

	estrDestroy(&pTemp);


}



//.graph[graphname].main/live
static bool ProcessGraphIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pWorkString = NULL;
	char *pErrorString = NULL;

	char *pFirstCloseBracket;
	bool bLive;
	U32 iTime = 0;
	Graph *pGraph;
	char *pDataString = NULL;

	if (pLocalXPath[0] != '[' || (pFirstCloseBracket = strchr(pLocalXPath, ']')) == NULL)
	{
		GetMessageForHttpXpath("Error - expected .graph[graphname].live/nnnnnnnnnn(time)", pStructInfo, 1);
		return true;
	}

	bLive = strStartsWith(pFirstCloseBracket + 1, ".live");
	if (pFirstCloseBracket[1] == '.')
	{
		iTime = atoi(pFirstCloseBracket + 2);
	}

	if (!(bLive || iTime))
	{
		GetMessageForHttpXpath("Error - expected .graph[graphname].live/nnnnnnnnnn(time)", pStructInfo, 1);
		return true;
	}

	*pFirstCloseBracket = 0;

	if (!stashFindPointer(sGraphsByGraphName, pLocalXPath+1, &pGraph))
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - unknown graph %s\n", pLocalXPath+1), pStructInfo, 1);
		return true;
	}

	if (iTime)
	{
		char *pFullString = NULL;
		
		if (!GetFullyWrappedFlotStringForGraph(pGraph, &pFullString, &pErrorString, false, NULL, iTime, NULL))
		{
			Errorf("%s", pErrorString);
		}
	
		GetRawHTMLForHttpXpath(pFullString, pStructInfo);
		pStructInfo->pExtraStylesheets = estrDup(sExtraStylesheet);
		pStructInfo->pExtraScripts = estrDup(sExtraScript);

		estrDestroy(&pFullString);

		return true;
	}

	//someone is asking for a live graph, so now we do our too-many-points-for-flot checking
	if (!urlFindValue(pArgList, "svrGraphConfirm") && pGraph->iUniqueDataPoints > MAX_FLOT_POINTS)
	{
		GetLinksForGraphWithTooManyPointsForFlot(pGraph, pStructInfo, eFlags);
		return true;
	}




	if (!GetFullyWrappedFlotStringForGraph(pGraph, &pWorkString, &pErrorString, false, NULL, iTime, pArgList))
	{
		GetMessageForHttpXpath(pErrorString, pStructInfo, 1);
		estrDestroy(&pWorkString);
		estrDestroy(&pErrorString);

		return true;
	}



	GetRawHTMLForHttpXpath(pWorkString, pStructInfo);
	pStructInfo->pExtraStylesheets = estrDup(sExtraStylesheet);
	pStructInfo->pExtraScripts = estrDup(sExtraScript);

	estrDestroy(&pWorkString);

	return true;
}


AUTO_STRUCT;
typedef struct ArchivedLinks
{
	char *pGraph; AST(ESTRING, FORMATSTRING(HTML=1))
	char *pCSV; AST(ESTRING, FORMATSTRING(HTML=1))
} ArchivedLinks;

AUTO_STRUCT;
typedef struct ArchivedLinksList
{
	ArchivedLinks **ppLinks;
} ArchivedLinksList;


//.archived[graphname]
static bool ProcessArchivesIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	char *pFirstCloseBracket;
	Graph *pGraph;
	ArchivedLinksList list = {0};
	int i;

	if (pLocalXPath[0] != '[' || (pFirstCloseBracket = strchr(pLocalXPath, ']')) == NULL)
	{
		GetMessageForHttpXpath("Error - expected .archived[graphname]", pStructInfo, 1);
		return true;
	}

	*pFirstCloseBracket = 0;

	if (!stashFindPointer(sGraphsByGraphName, pLocalXPath+1, &pGraph))
	{
		GetMessageForHttpXpath(STACK_SPRINTF("Error - unknown graph %s\n", pLocalXPath+1), pStructInfo, 1);
		return true;
	}

	for (i=0; i < ea32Size(&pGraph->ppTimesOfOldGraphs); i++)
	{
		ArchivedLinks *pLink = StructCreate(parse_ArchivedLinks);

		estrPrintf(&pLink->pGraph, "<a href=\"%s.graph[%s].%u\">%s Graph</a>", LinkToThisServer(), pGraph->pGraphName, pGraph->ppTimesOfOldGraphs[i], 
			GetDescriptiveTimeNameForGraph(pGraph, pGraph->ppTimesOfOldGraphs[i]));
		estrPrintf(&pLink->pCSV, "<a href=\"%s.csv[%s].%u\">CSV</a></span>", LinkToThisServer(), pGraph->pGraphName, pGraph->ppTimesOfOldGraphs[i]);

		eaPush(&list.ppLinks, pLink);
	}

	ProcessStructIntoStructInfoForHttp(pFirstCloseBracket + 1, pArgList, &list, parse_ArchivedLinksList, iAccessLevel, 0, pStructInfo, eFlags);

	StructDeInit(parse_ArchivedLinksList, &list);

	return true;
}





static S32 RevCompareInts(const U32 *i, const U32 *j) { return *j - *i; }


static void FindOldDataForGraph(Graph *pGraph)
{
	char dirName[CRYPTIC_MAX_PATH];
	char **ppFileList;

	int i;

	ea32Destroy(&pGraph->ppTimesOfOldGraphs);

	sprintf(dirName, "%s/%s/OldData", GetGraphDirName(), pGraph->pGraphName);

	ppFileList = fileScanDirFolders(dirName, FSF_FILES);

	for (i=0; i < eaSize(&ppFileList); i++)
	{
		if (strEndsWith(ppFileList[i], "Data.txt"))
		{
			char shortName[CRYPTIC_MAX_PATH];
			U32 iTime;

			getFileNameNoExtNoDirs(shortName, ppFileList[i]);

			iTime = atoi(shortName);
			if (iTime)
			{
				ea32Push(&pGraph->ppTimesOfOldGraphs, iTime);
			}
		}
	}

	ea32QSort(pGraph->ppTimesOfOldGraphs, RevCompareInts);

	fileScanDirFreeNames(ppFileList);

}




void Graph_InitSystem(void)
{
	int i;

	sGraphsByGraphName = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);

	resRegisterDictionaryForStashTable("Graphs", RESCATEGORY_OTHER, 0, sGraphsByGraphName, parse_Graph);

	JpegLibrary_RegisterCB("GRAPH", Graph_JpegCB);


	if (!gbStandAlone || gbLiveLikeStandAlone)
	{
		char fileName[CRYPTIC_MAX_PATH];
		gbLogParserGraphReadHasAlerted = false;
		GetGraphStatusFileName(SAFESTR(fileName));
		ParserReadTextFile(fileName, parse_GraphList, &sAllNonLongTermGraphList, 0);

		for (i=eaSize(&sAllNonLongTermGraphList.ppGraphs) - 1; i >= 0; i--)
		{
			if (FindAndMarkGraphDefinition(sAllNonLongTermGraphList.ppGraphs[i]->pDefinition))
			{
				FindOldDataForGraph(sAllNonLongTermGraphList.ppGraphs[i]);
				AddGraphToLists(sAllNonLongTermGraphList.ppGraphs[i]);
			}
			else
			{
				StructDestroy(parse_Graph, sAllNonLongTermGraphList.ppGraphs[i]);
				eaRemove(&sAllNonLongTermGraphList.ppGraphs, i);
			}
		}
	}

	RegisterCustomXPathDomain(".graphData", ProcessGraphDataIntoStructInfoForHttp, NULL);
	RegisterCustomXPathDomain(".graph", ProcessGraphIntoStructInfoForHttp, NULL);
	RegisterCustomXPathDomain(".csv", ProcessCSVIntoStructInfoForHttp, NULL);
	RegisterCustomXPathDomain(".archived", ProcessArchivesIntoStructInfoForHttp, NULL);

	InitAllGraphsFromConfig();
}

void Graph_CheckLifetimes(U32 iTime)
{
	StashTableIterator iterator;
	StashElement element;

	stashGetIterator(sGraphsByGraphName, &iterator);
	
	while (stashGetNextElement(&iterator, &element))
	{
		Graph *pGraph = stashElementGetPointer(element);

		Graph_CheckLifetime(pGraph, iTime);
	}

}



void Graph_CheckForLongTermGraphs(void)
{
	int i;

	if (gbStandAlone)
	{
		return;
	}

	for (i = eaSize(&gLogParserConfig.ppGraphs)-1 ; i >= 0 ; i--)
	{
		GraphDefinition *pSourceGraph = gLogParserConfig.ppGraphs[i];
		if (pSourceGraph->bLongTermGraph)
		{

			GraphDefinition *pNewDefinition = StructCreate(parse_GraphDefinition);
			char tempString[4096];
			UseThisLogCheckEquality *pEqualityCheck;

			pNewDefinition->eDisplayType = GRAPHDISPLAYTYPE_LINES;
			pNewDefinition->eDataType = GRAPHDATATYPE_SUM;

			sprintf(tempString, "%sLongTerm", pSourceGraph->pGraphName);

			pNewDefinition->pGraphName = strdup(tempString);

			sprintf(tempString, "%s - Long Term", pSourceGraph->pGraphTitle);

			pNewDefinition->pGraphTitle = strdup(tempString);

			pNewDefinition->pActionName = allocAddString("LongTermData");

			pEqualityCheck = StructCreate(parse_UseThisLogCheckEquality);


			pEqualityCheck->pXPath = strdup(".objInfo.LongTermData.GraphName");
			pEqualityCheck->pComparee = strdup(pSourceGraph->pGraphName);

			eaPush(&pNewDefinition->useThisLog.ppUseThisLog_EqualityChecks, pEqualityCheck);

			pNewDefinition->pCountField = strdup(".ObjInfo.LongTermData.Data");

			pNewDefinition->pBarField = strdup(".time");
			pNewDefinition->pCategoryField = strdup(".ObjInfo.LongTermData.DataType");

			pNewDefinition->bIAmALongTermGraph = true;

			eaPush(&gLogParserConfig.ppGraphs, pNewDefinition);
		}
	}
}



void Graph_DumpStatusFile(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	GetGraphStatusFileName(SAFESTR(fileName));
	ParserWriteTextFile(fileName, parse_GraphList, &sAllNonLongTermGraphList, 0, 0);
}

void Graph_CopyStatusFile(void)
{
	char fileName[CRYPTIC_MAX_PATH];
	char tempFileName[CRYPTIC_MAX_PATH];
	char backupFileName[CRYPTIC_MAX_PATH];

	GetGraphStatusFileName(SAFESTR(fileName));
	GetLogParserTempStatusFileName(SAFESTR(tempFileName));
	GetLogParserBackupStatusFileName(SAFESTR(backupFileName));
	if(!fileExists(tempFileName) || (fileForceRemove(tempFileName) == 0))
	{
		if(fileCopy(fileName, tempFileName) == 0)
		{
			if(!fileExists(backupFileName) || (fileForceRemove(backupFileName) == 0))
			{
				if(rename(tempFileName, backupFileName) != 0)
				{
					ErrorOrAlert("LOGPARSER_GRAPH_STATUS_WRITE", "Unable to rename %s to %s", tempFileName, backupFileName);
				}
			}
			else
			{
				ErrorOrAlert("LOGPARSER_GRAPH_STATUS_WRITE", "Unable to delete backup file %s.", backupFileName);
			}
		}
		else
		{
			ErrorOrAlert("LOGPARSER_GRAPH_STATUS_WRITE", "Unable to copy %s to %s.", fileName, tempFileName);
		}
	}
	else
	{
		ErrorOrAlert("LOGPARSER_GRAPH_STATUS_WRITE", "Unable to delete temporary file %s.", tempFileName);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void Graph_DumpLongTermCSVs(void)
{
	StashTableIterator iterator;
	StashElement element;

	stashGetIterator(sGraphsByGraphName, &iterator);
	
	while (stashGetNextElement(&iterator, &element))
	{
		Graph *pGraph = stashElementGetPointer(element);

		if (pGraph->pDefinition->bIAmALongTermGraph)
		{
			char outCsvName[CRYPTIC_MAX_PATH];
			FILE *pOutFile;

			sprintf(outCsvName, "%s/Graphs/%s/%s/longterm.csv", LogParserLocalDataDir(), GetShardNameFromShardInfoString(), 
				pGraph->pGraphName);
		
			mkdirtree(outCsvName);

			pOutFile = fopen(outCsvName, "wt");

			if (pOutFile)
			{
				char *pTempString = NULL;
				Graph_WriteCSV(pGraph, &pTempString);
				fprintf(pOutFile, "%s", pTempString);

				fclose (pOutFile);
				estrDestroy(&pTempString);
			}
		}
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void ActivateGraph(char *pGraphName)
{

	Graph *pGraph;

	if (!stashFindPointer(sGraphsByGraphName, pGraphName, &pGraph))
	{
		return;
	}

	pGraph->bActiveInStandaloneMode = true;

	SaveStandAloneOptions(NULL);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0);
void InactivateGraph(char *pGraphName)
{

	Graph *pGraph;

	if (!stashFindPointer(sGraphsByGraphName, pGraphName, &pGraph))
	{
		return;
	}
	pGraph->bActiveInStandaloneMode = false;

	SaveStandAloneOptions(NULL);

}



void Graph_ResetSystem(void)
{
	StashTableIterator iterator;
	StashElement element;

	stashGetIterator(sGraphsByGraphName, &iterator);
	
	while (stashGetNextElement(&iterator, &element))
	{
		Graph *pGraph = stashElementGetPointer(element);
		
		Graph_ClearData(pGraph);
	}
}


static char *GraphDownloadCB(char *pInName, char **ppErrorString)
{
	static char HTMLName[CRYPTIC_MAX_PATH];
	char *pDataString = NULL;
	Graph *pGraph;
	char graphName[256];




	if (!strEndsWith(pInName, ".html"))
	{
		estrPrintf(ppErrorString, "Only .html supported for graph downloading");
		return NULL;
	}

	getFileNameNoExt(graphName, pInName);

	if (!stashFindPointer(sGraphsByGraphName, graphName, &pGraph))
	{
		estrPrintf(ppErrorString, "Unknown graph %s", graphName);
		return NULL;
	}

	if (!GetFlotDataStringForGraph(&pDataString, pGraph, ppErrorString, NULL))
	{
		return NULL;
	}

	sprintf(HTMLName, "%s/%u_%s.html", fileTempDir(), timeSecondsSince2000(), graphName);
	mkdirtree_const(HTMLName);

	WriteOutFlotTextFileForGraph(HTMLName, pGraph, pDataString);



	return HTMLName;

}

AUTO_RUN;
void Graph_RegisterFileCB(void)
{
	GenericFileServing_RegisterSimpleDomain("graphDownLoad", GraphDownloadCB);
	POOLED_TOTAL_STRING = allocAddString("total");
}

#include "LogParserGraphs_h_ast.c"
#include "LogParserGraphs_c_ast.c"
