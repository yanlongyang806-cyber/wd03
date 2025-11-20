#include "MemLeakTracking.h"
#include "TimedCallback.h"
#include "alerts.h"
#include "error.h"
#include "timing.h"
#include "sysUtil.h"
#include "GlobalTypes.h"
#include "file.h"
#include "memreport.h"
#include "estring.h"
#include "earray.h"
#include "stringUtil.h"
#include "stashTable.h"
#include "MemLeakTracking_c_ast.h"
#include "MemLeakTracking_h_ast.h"
#include "RegistryReader.h"
#include "memoryBudget.h"
#include "httpXpathSupport.h"


bool gbTrackMemLeaksInDevMode = false;
AUTO_CMD_INT(gbTrackMemLeaksInDevMode, TrackMemLeaksInDevMode);


static size_t siSavedSize = 0;
static size_t siSavedEffectiveSize = 0;
static char *pFirstSizePretty = NULL;
static char *pOldSizePretty = NULL;
static char *pOldFullReport = NULL;
static U32 siSavedTime = 0;
static int siPauseBetweenChecks = 0;
static size_t siIncreaseAmount = 0;
static size_t siFirstIncreaseAllowance = 0;
static char *spAlertKeyPrefix = NULL;
static char *spMMDSFileName = NULL;
static int siAlertCount = 0;
static U32 siTimeBegan = 0;
static char *spMostRecentSizePretty = NULL;
static U32 siMostRecentTime = 0;
static char *spMostRecentAnalysis = NULL;
static bool sbForceAnalysisNow = false;
static size_t siSizeOfAllocationsToIgnore = 0;
static char *spAllocationsToIgnoreString = NULL;

//when alerting on potentialy mem leaks, list the top N
static int siMaxNumLeaksToSendInAlert = 10;
AUTO_CMD_INT(siMaxNumLeaksToSendInAlert, MaxNumLeaksToSendInAlert);

//when categorizing leaks and listing leaks within each category, only list leaks of this  amount or greater
static U32 siMinRelevantAmountForMemLeakCategory = 8 * 1024;
AUTO_CMD_INT(siMinRelevantAmountForMemLeakCategory, MinRelevantAmountForMemLeakCategory);

//if true, then try to deduce the largest leaking culprit, append its name to the alert key. Stephen wants this
//off for now
static bool sbMakeUniqueMemLeakAlertKeys = false;
AUTO_CMD_INT(sbMakeUniqueMemLeakAlertKeys, MakeUniqueMemLeakAlertKeys);

AUTO_STRUCT;
typedef struct MemDumpLineNums
{
	//line number of the --------------- that begins the block
	int iStartLine;
	int iStopLine;
} MemDumpLineNums;

AUTO_STRUCT;
typedef struct MemDumpNode
{
	char key[64];
	S64 iStartingRAM;
	S64 iEndingRAM;
} MemDumpNode;

void DEFAULT_LATELINK_GetMemSizeForMemLeakTracking(size_t *pOutSize, size_t *pOutEffectiveSize, char **ppOutPrettySizeString)
{
	*pOutSize = getProcessPageFileUsage();
	*pOutEffectiveSize = *pOutSize;
	estrMakePrettyBytesString(ppOutPrettySizeString, *pOutSize);
}

int SortMemDumpNodesBySize(const MemDumpNode **pInfo1, const MemDumpNode **pInfo2)
{
	return ((*pInfo2)->iEndingRAM - (*pInfo2)->iStartingRAM) - ((*pInfo1)->iEndingRAM - (*pInfo1)->iStartingRAM);
}

int SortMemDumpCategoriesBySize(const MemLeakCategory **pCategory1, const MemLeakCategory **pCategory2)
{
	return (*pCategory2)->iAmountLeaked - (*pCategory1)->iAmountLeaked;
}

typedef enum
{
	RES_IGNORE,
	RES_GOOD,
	RES_BAD,
} enumLineProcessingResult;

enumLineProcessingResult FindNameAndAmountFromLine(char *pLine, char *pOutName, int iOutNameSize, S64 *pOutAmount)
{
	char **ppTokens = NULL;
	static char *pName = NULL;
	
	int i = 0;

	int iNumTokens;

	float fVal;
	int iLastWithColon;

	/*975 files tracked
Video Memory:
System Memory:*/
	if (strstri(pLine, "files tracked") || strstri(pLine, "Video Memory:") || strstri(pLine, "System Memory:"))
	{
		return RES_IGNORE;
	}

	estrClear(&pName);

	//the first N tokens up to one including a colon are the key,
	//next is float, next is "bytes", "kb", "mb" or "tb"
	DivideString(pLine, " ", &ppTokens, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS | DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE);

	iNumTokens = eaSize(&ppTokens);

	if (iNumTokens < 3)
	{
		eaDestroyEx(&ppTokens, NULL);
		return RES_BAD;
	}

	//find last token with a colon in it, that should be the last token of the filename
	iLastWithColon = iNumTokens - 1;
	while (iLastWithColon >= 0 && !strchr(ppTokens[iLastWithColon], ':'))
	{
		iLastWithColon--;
	}

	if (iLastWithColon < 0)
	{
		eaDestroyEx(&ppTokens, NULL);
		return RES_BAD;
	}

	while (i <= iLastWithColon)
	{
		estrConcatf(&pName, "%s%s", i == 0 ? "" : " ", ppTokens[i]);
		i++;
	}

	if ((int)estrLength(&pName) >= iOutNameSize)
	{
		eaDestroyEx(&ppTokens, NULL);
		return RES_BAD;
	}

	strcpy_s(pOutName, iOutNameSize, pName);

	if (i > iNumTokens - 3)
	{
		eaDestroyEx(&ppTokens, NULL);
		return RES_BAD;
	}

	if (!StringToFloat(ppTokens[i], &fVal))
	{
		eaDestroyEx(&ppTokens, NULL);
		return RES_BAD;
	}

	i++;

	if (strStartsWith(ppTokens[i], "bytes"))
	{
		*pOutAmount = fVal;
		eaDestroyEx(&ppTokens, NULL);
		return RES_GOOD;
	}


	if (strStartsWith(ppTokens[i], "KB"))
	{
		*pOutAmount = fVal * 1024.0f;
		eaDestroyEx(&ppTokens, NULL);
		return RES_GOOD;
	}


	if (strStartsWith(ppTokens[i], "MB"))
	{
		*pOutAmount = fVal * 1024.0f * 1024.0f;
		eaDestroyEx(&ppTokens, NULL);
		return RES_GOOD;
	}


	if (strStartsWith(ppTokens[i], "GB"))
	{
		*pOutAmount = fVal * 1024.0f * 1024.0f * 1024.0f;
		eaDestroyEx(&ppTokens, NULL);
		return RES_GOOD;
	}
	eaDestroyEx(&ppTokens, NULL);

	return RES_BAD;
}

char *FindCategoryMemberNameFromMemDumpNode(char *pNodeName)
{
	static char *pRetVal = NULL;
	estrCopy2(&pRetVal, pNodeName);

		//Sm_Species (...esLib\utils\ResourceManager.c):1 -> SM_Species
	if (strchr(pRetVal, '(') && strchr(pRetVal, ')'))
	{
		estrTruncateAtFirstOccurrence(&pRetVal, '(');
		estrTrimLeadingAndTrailingWhitespace(&pRetVal);
		return pRetVal;
	}

	//c:\src\libs\utilitieslib\utils\dirmonitor.c:296 -> dirmonitor.c
	if (strchr(pRetVal, '\\'))
	{		
		estrTruncateAtLastOccurrence(&pRetVal, ':');
		estrRemoveUpToLastOccurrence(&pRetVal, '\\');
		return pRetVal;
	}

	// SM_Materials:1  -> SM_Materials
	estrTruncateAtLastOccurrence(&pRetVal, ':');
	return pRetVal;
}

MemLeakCategory *FindCategoryFromListForMemberName(char *pName, MemLeakCategory ***pppCategories)
{
	int i;
	int iDefaultIndex = -1;
	for (i=0; i < eaSize(pppCategories); i++)
	{
		MemLeakCategory *pCategory = (*pppCategories)[i];

		if (stricmp(pCategory->pCategoryName__, "Uncategorized") == 0)
		{
			iDefaultIndex = i;
		}
		else
		{
			if (eaFindString(&pCategory->ppMemberNames__, pName) != -1)
			{
				return pCategory;
			}
		}
	}

	if (iDefaultIndex != -1)
	{
		return (*pppCategories)[iDefaultIndex];
	}

	return NULL;
}

MemLeakCategory *FindCategory(MemLeakCategory ***pppCategories, char *pName)
{
	int i;

	for (i=0; i < eaSize(pppCategories); i++)
	{
		MemLeakCategory *pCategory = (*pppCategories)[i];

		if (stricmp(pCategory->pCategoryName__, pName) == 0)
		{
			return pCategory;
		}
	}

	return NULL;
}

void MakeMemLeakCategoryReport(char **ppOutString, MemLeakCategory ***pppCategories, MemDumpNode ***pppNodes)
{
	int i;
	static char *pBytes = NULL;

	if (!FindCategory(pppCategories, "Uncategorized"))
	{
		MemLeakCategory *pCategory = StructCreate(parse_MemLeakCategory);
		pCategory->pCategoryName__ = strdup("Uncategorized");
		eaPush(pppCategories, pCategory);
	}

	for (i=0; i < eaSize(pppCategories); i++)
	{
		MemLeakCategory *pCategory = (*pppCategories)[i];

		pCategory->iAmountLeaked = 0;
		estrDestroy(&pCategory->pReportString__);
	}

	for (i=0; i < eaSize(pppNodes); i++)
	{
		MemDumpNode *pNode = (*pppNodes)[i];
		char *pMemberName = FindCategoryMemberNameFromMemDumpNode(pNode->key);
		MemLeakCategory *pCategory = FindCategoryFromListForMemberName(pMemberName, pppCategories);
	
		pCategory->iAmountLeaked += pNode->iEndingRAM - pNode->iStartingRAM;

		if (pNode->iEndingRAM > pNode->iStartingRAM + siMinRelevantAmountForMemLeakCategory)
		{
			estrMakePrettyBytesString(&pBytes, pNode->iEndingRAM - pNode->iStartingRAM);
			estrConcatf(&pCategory->pReportString__, "    %s: LEAKED %s\r\n", pNode->key, pBytes);
		}
		else if (pNode->iEndingRAM < pNode->iStartingRAM - siMinRelevantAmountForMemLeakCategory) 
		{
			estrMakePrettyBytesString(&pBytes, pNode->iStartingRAM - pNode->iEndingRAM);
			estrConcatf(&pCategory->pReportString__, "    %s: GAINED %s\r\n", pNode->key, pBytes);
		}
	}

	eaQSort((*pppCategories), SortMemDumpCategoriesBySize);

	for (i=0; i < eaSize(pppCategories); i++)
	{
		MemLeakCategory *pCategory = (*pppCategories)[i];

		if (pCategory->iAmountLeaked >= 0)
		{
			estrMakePrettyBytesString(&pBytes, pCategory->iAmountLeaked);

			estrConcatf(ppOutString, "Category %s LEAKED %s\n%s\n\n", pCategory->pCategoryName__, pBytes, pCategory->pReportString__ ? pCategory->pReportString__ : "");
		}
		else
		{
			estrMakePrettyBytesString(&pBytes, -pCategory->iAmountLeaked);

			estrConcatf(ppOutString, "Category %s GAINED %s\n%s\n\n", pCategory->pCategoryName__, pBytes, pCategory->pReportString__ ? pCategory->pReportString__ : "");
		}
	}
}


void FindMemLeaksFromStringWithMultipleMMDS(char *pInString, char **ppOutString, char **ppOutBiggestKey, MemLeakCategory ***pppCategories, int iNumToIncludePerCategory)
{
	static char **ppLines = NULL;
	int i;
	static MemDumpLineNums **ppMemDumps = NULL;
	int iMostRecentBars = -1;

	static StashTable sMemDumpNodeTable = NULL;
	static MemDumpNode **sppMemDumpNodeList = NULL;

	if (iNumToIncludePerCategory == 0)
	{
		iNumToIncludePerCategory = 10000000;
	}

	if (sMemDumpNodeTable)
	{
		stashTableDestroy(sMemDumpNodeTable);
		sMemDumpNodeTable = NULL;
	}

	eaDestroyEx(&ppLines, NULL);
	eaDestroyStruct(&ppMemDumps, parse_MemDumpLineNums);
	eaDestroyStruct(&sppMemDumpNodeList, parse_MemDumpNode);



	

	DivideString(pInString, "\r\n", &ppLines, DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS);

	if (!ppLines)
	{
		estrPrintf(ppOutString, "No valid input");
		return;
	}

	for (i=0 ; i < eaSize(&ppLines); i++)
	{
		if (stricmp(ppLines[i], "--------------------------------------------------") == 0)
		{
			if (i + 3 < eaSize(&ppLines) && strStartsWith(ppLines[i+3], "OS reports") && iMostRecentBars != -1)
			{
				MemDumpLineNums *pMemDump = StructCreate(parse_MemDumpLineNums);
				pMemDump->iStartLine = iMostRecentBars;
				pMemDump->iStopLine = i;
				eaPush(&ppMemDumps, pMemDump);
				iMostRecentBars = -1;
			}
			else
			{
				iMostRecentBars = i;
			}
		}
	}

	if (eaSize(&ppMemDumps) < 2)
	{
		estrPrintf(ppOutString, "Only found %d mem dumps in the input string... no comparing to do", eaSize(&ppMemDumps));
			
		eaDestroyEx(&ppLines, NULL);
		return;
	}

	for (i = ppMemDumps[0]->iStartLine + 1; i < ppMemDumps[0]->iStopLine; i++)
	{
		char name[64];
		S64 iAmount;
		MemDumpNode *pNode;

		switch (FindNameAndAmountFromLine(ppLines[i], SAFESTR(name), &iAmount))
		{
		case RES_IGNORE:
			continue;
		
		case RES_BAD:
			estrPrintf(ppOutString, "Badly formatted line: <<%s>>.", ppLines[i]);
			eaDestroyEx(&ppLines, NULL);
			return;
		}

		pNode = StructCreate(parse_MemDumpNode);
		pNode->iStartingRAM = iAmount;
		strcpy(pNode->key, name);

		if (!sMemDumpNodeTable)
		{
			sMemDumpNodeTable = stashTableCreateWithStringKeys(10, StashDefault);
		}

		stashAddPointer(sMemDumpNodeTable, pNode->key, pNode, true);
		eaPush(&sppMemDumpNodeList, pNode);
	}

	for (i = ppMemDumps[eaSize(&ppMemDumps)-1]->iStartLine + 1; i < ppMemDumps[eaSize(&ppMemDumps)-1]->iStopLine; i++)
	{
		char name[64];
		S64 iAmount;
		MemDumpNode *pNode;


		switch (FindNameAndAmountFromLine(ppLines[i], SAFESTR(name), &iAmount))
		{
		case RES_IGNORE:
			continue;
		
		case RES_BAD:
			estrPrintf(ppOutString, "Badly formatted line: <<%s>>.", ppLines[i]);
			eaDestroyEx(&ppLines, NULL);
			return;
		}

		if (stashFindPointer(sMemDumpNodeTable, name, &pNode))
		{
			pNode->iEndingRAM = iAmount;
		}
		else
		{
			pNode = StructCreate(parse_MemDumpNode);
			pNode->iEndingRAM = iAmount;
			strcpy(pNode->key, name);

			if (!sMemDumpNodeTable)
			{
				sMemDumpNodeTable = stashTableCreateWithStringKeys(10, StashDefault);
			}

			stashAddPointer(sMemDumpNodeTable, pNode->key, pNode, true);
			eaPush(&sppMemDumpNodeList, pNode);
		}
	}

	eaDestroyEx(&ppLines, NULL);

	eaQSort(sppMemDumpNodeList, SortMemDumpNodesBySize);

	if (pppCategories)
	{
		MakeMemLeakCategoryReport(ppOutString, pppCategories, &sppMemDumpNodeList);
	}

	if (eaSize(&sppMemDumpNodeList) && ppOutBiggestKey)
	{
		estrCopy2(ppOutBiggestKey, sppMemDumpNodeList[0]->key);
	}

	for (i=0 ; i < eaSize(&sppMemDumpNodeList) && i < iNumToIncludePerCategory; i++)
	{
		if (sppMemDumpNodeList[i]->iEndingRAM > sppMemDumpNodeList[i]->iStartingRAM)
		{
			static char *pBytes = NULL;
			estrMakePrettyBytesString(&pBytes, sppMemDumpNodeList[i]->iEndingRAM - sppMemDumpNodeList[i]->iStartingRAM);
			estrConcatf(ppOutString, "%s: leaked %s\r\n", sppMemDumpNodeList[i]->key, pBytes);
		}
	}
}

void DEFAULT_LATELINK_MemLeakTracking_ExtraReport(char *pFilePrefix)
{

}


void DoExtraReports(U32 iCurTime)
{
	char filePrefix[CRYPTIC_MAX_PATH];
	sprintf(filePrefix, "c:\\mmds_%s\\%s_%u_%u_", GetShortProductName(), GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), iCurTime);

	MemLeakTracking_ExtraReport(filePrefix);
}




static void WriteVMMapFile(U32 iCurTime)
{
	char *pSystemString = NULL;
	RegReader rr = createRegReader();

	char filename[CRYPTIC_MAX_PATH];

	sprintf(filename, "c:\\mmds_%s\\%s_%u_%u_vmmap.mmp", GetShortProductName(), GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), iCurTime);
	mkdirtree_const(filename);

	if (initRegReader(rr, "HKEY_CURRENT_USER\\Software\\SysInternals\\VMMap"))
	{
		rrWriteInt(rr, "EulaAccepted", 1);
		destroyRegReader(rr);
	}
	else
	{
		CRITICAL_NETOPS_ALERT("CANT_WRITE_REGISTRY", "Tried to write HKEY_CURRENT_USER\\Software\\SysInternals\\VMMap\\EulaAccepted into the registry so that a popup wouldn't show up for vmmap, failed to do so. There may be a popup there");
	}


	estrStackCreate(&pSystemString);
	estrPrintf(&pSystemString, "vmmap -p %u %s",
		getpid(), filename);

	system_detach(pSystemString, true, true);

	estrDestroy(&pSystemString);


}

AUTO_COMMAND;
void WriteVMapNow(void)
{
	WriteVMMapFile(timeSecondsSince2000());
}


static void fileHandler(char *pAppendMe, FILE *pFile)
{
	fprintf(pFile, "%s", pAppendMe);
}


//takes in something that looks like .../file/foo.c:2030 and returns FOO_C_2030
char *GetAlertKeyFromMemLeakKey(char *pMemLeakKey)
{
	static char *spRetVal = NULL;
	estrCopy2(&spRetVal, pMemLeakKey ? pMemLeakKey : "");

	estrRemoveUpToLastOccurrence(&spRetVal, '/');
	estrRemoveUpToLastOccurrence(&spRetVal, '\\');

	estrMakeAllAlphaNumAndUnderscores(&spRetVal);

	while (spRetVal[0] == '_')
	{
		estrRemove(&spRetVal, 0, 1);
	}

	while (spRetVal[estrLength(&spRetVal) - 1] == '_')
	{
		estrRemove(&spRetVal, estrLength(&spRetVal) - 1, 1);
	}

	string_toupper(spRetVal);

	return spRetVal;
}






static void MemLeakTrackingRepeatedCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	size_t iCurSize = 0, iCurEffectiveSize = 0;
	U32 iCurTime = siMostRecentTime = timeSecondsSince2000();

	GetMemSizeForMemLeakTracking(&iCurSize, &iCurEffectiveSize, &spMostRecentSizePretty);

	if (sbForceAnalysisNow || ((iCurEffectiveSize - siSizeOfAllocationsToIgnore) > siSavedEffectiveSize + siIncreaseAmount + (siAlertCount ? 0 : siFirstIncreaseAllowance)))
	{
		char *pTimeDiffPretty = NULL;
		FILE *pFile;
		char *pNewFileName = NULL;
		char *pNewFullReport = NULL;
		char *pFullString = NULL;
		char *pBiggestKey = NULL;
		char *pAlertKeyToUse = NULL;

		timeSecondsDurationToPrettyEString(iCurTime - siSavedTime, &pTimeDiffPretty);

		estrPrintf(&pNewFileName, "c:\\mmds_%s\\%s_%u_%u_mmds.txt", GetShortProductName(), GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), iCurTime);
		mkdirtree_const(pNewFileName);

		// Write out VMMap and entity reports
		WriteVMMapFile(iCurTime);
		DoExtraReports(iCurTime);

		// Analyze potential memory leaks
		memMonitorPerLineStatsInternal(estrConcatHandler, &pNewFullReport, NULL, 100000, 0);

		estrPrintf(&pFullString, "%s\n\n%s", pOldFullReport, pNewFullReport);

		estrDestroy(&spMostRecentAnalysis);

		FindMemLeaksFromStringWithMultipleMMDS(pFullString, &spMostRecentAnalysis, &pBiggestKey, NULL, siMaxNumLeaksToSendInAlert);

		estrPrintf(&pAlertKeyToUse, "%s%s_%s", NULL_TO_EMPTY(spAlertKeyPrefix), GlobalTypeToName(GetAppGlobalType()),(siAlertCount == 1 ? "POSSIBLE_MEMLEAK" : "LIKELY_MEMLEAK"));

		if (sbMakeUniqueMemLeakAlertKeys)
		{
			estrConcatf(&pAlertKeyToUse, "_%s",  GetAlertKeyFromMemLeakKey(pBiggestKey));
		}


		// Issue the alert
		++siAlertCount;
		WARNING_NETOPS_ALERT( pAlertKeyToUse, 
			"%sMem usage has increased from %s to %s in the last %s.  This is a %s memory leak (alert #%d).\nDumps are in %s and %s (%s).\nMem leak analysis:\n%s",
			sbForceAnalysisNow ? "(Triggered due to ForceAnalysisNow command) " : "",
			pOldSizePretty, spMostRecentSizePretty, pTimeDiffPretty, 
			(siAlertCount == 1 ? "possible" : "likely"), siAlertCount, 
			spMMDSFileName, pNewFileName, getHostName(), spMostRecentAnalysis);
		

		// Write the new report to disk
		pFile = fopen(pNewFileName, "wt");
		if (pFile)
		{
			fprintf(pFile, "%s", pNewFullReport);
			fclose(pFile);
		}

		// Write old report on first alert
		if (siAlertCount == 1)
		{
			estrPrintf(&pNewFileName, "c:\\mmds_%s\\%s_%u_%u_mmds.txt", GetShortProductName(), GlobalTypeToName(GetAppGlobalType()), GetAppGlobalID(), siSavedTime);
			mkdirtree_const(pNewFileName);
			pFile = fopen(pNewFileName, "wt");
			if (pFile)
			{
				fprintf(pFile, "%s", pOldFullReport);
				fclose(pFile);
			}
		}

		// Save values for next alert and clean up
		siSavedSize = iCurSize;
		siSavedEffectiveSize = iCurEffectiveSize;
		siSavedTime = iCurTime;

		//once we've established our new "baseline" size, wipe out the to-ignore size as it's now fully in the past
		siSizeOfAllocationsToIgnore = 0;

		estrCopy(&pOldSizePretty, &spMostRecentSizePretty);

		estrDestroy(&pFullString);

		estrDestroy(&pOldFullReport);
		pOldFullReport = pNewFullReport;
		
		estrDestroy(&spMMDSFileName);
		spMMDSFileName = pNewFileName;

		estrDestroy(&pTimeDiffPretty);
		estrDestroy(&pBiggestKey);
		estrDestroy(&pAlertKeyToUse);

		sbForceAnalysisNow = false;
	}

}

static void BeginMemLeakTrackingCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	GetMemSizeForMemLeakTracking(&siSavedSize, &siSavedEffectiveSize, &pOldSizePretty);
	siSavedTime = timeSecondsSince2000();
	memMonitorPerLineStatsInternal(estrConcatHandler, &pOldFullReport, NULL, 100000, 0);

	TimedCallback_Add(MemLeakTrackingRepeatedCB, NULL, siPauseBetweenChecks);

	estrPrintf(&spMMDSFileName,  "(file not saved)");
	siTimeBegan = siMostRecentTime = timeSecondsSince2000();
	estrCopy(&pFirstSizePretty, &pOldSizePretty);
	estrCopy(&spMostRecentSizePretty, &pOldSizePretty);


}

bool MemLeakTracking_Running(void)
{
	return !!siTimeBegan;
}

void BeginMemLeakTracking(int iPauseBeforeBeginning, int iPauseBetweenChecks, size_t iIncreaseAmountThatIsALeak, size_t iFirstIncreaseAllowance, const char *pAlertKeyPrefix)
{
	if (siIncreaseAmount)
	{
		AssertOrAlert("BEGIN_MEM_LEAK_TRACK_TWICE", "Can't begin mem leak tracking twice");
		return;
	}

	siIncreaseAmount = iIncreaseAmountThatIsALeak;
	siFirstIncreaseAllowance = iFirstIncreaseAllowance;
	siPauseBetweenChecks = iPauseBetweenChecks;

	siTimeBegan = timeSecondsSince2000() + iPauseBeforeBeginning;

	if(!nullStr(pAlertKeyPrefix))
		spAlertKeyPrefix = strdup(pAlertKeyPrefix);

	TimedCallback_Run(BeginMemLeakTrackingCB, NULL, iPauseBeforeBeginning);
}

AUTO_COMMAND ACMD_NAME(BeginMemLeakTracking);
void BeginMemLeakTrackingCmd(int iPauseBeforeBeginning, int iPauseBetweenCHecks, int iIncreaseAmountThatIsALeakMegs, int iFirstIncreaseAllowanceMegs, const char *pAlertKeyPrefix)
{
	if (siIncreaseAmount)
	{
		return;
	}

	BeginMemLeakTracking(iPauseBeforeBeginning, iPauseBetweenCHecks,
		((size_t)iIncreaseAmountThatIsALeakMegs) * 1024 * 1024,
		((size_t)iFirstIncreaseAllowanceMegs) * 1024 * 1024,
		pAlertKeyPrefix);
}

AUTO_STRUCT;
typedef struct MemLeakTrackingOverview
{
	char *pSummary; AST(ESTRING, FORMATSTRING(HTML_PREFORMATTED=1))
	int iNumAlertsGenerated;
	int iCheckFrequency; AST(FORMATSTRING(HTML_SECS_DURATION=1))
	S64 iIncreaseAmountThatIsALeak; AST(FORMATSTRING(HTML_BYTES=1))
	S64 iFirstIncreaseAllowance; AST(FORMATSTRING(HTML_BYTES=1))
	char *pAlertKeyPrefix;

	//effectively unowned
	char *pMostRecentAnalysis; AST(POOL_STRING, FORMATSTRING(HTML_PREFORMATTED=1))
	char *pAllocationsToIgnore; AST(POOL_STRING, FORMATSTRING(HTML_PREFORMATTED=1))

	char *pForceAnalysisNow; AST(ESTRING, FORMATSTRING(command=1))
	char *pBeginMemLeakTracking; AST(ESTRING, FORMATSTRING(command=1))

} MemLeakTrackingOverview;

MemLeakTrackingOverview *GetMemLeakTrackingOverview(void)
{
	static MemLeakTrackingOverview sOverview = {0};
	U32 iCurTime = timeSecondsSince2000();
	char *pDurationString = NULL;

	StructReset(parse_MemLeakTrackingOverview, &sOverview);

	if (!siTimeBegan)
	{
		estrPrintf(&sOverview.pSummary, "Mem Leak Tracking IS NOT ACTIVE");
		estrPrintf(&sOverview.pBeginMemLeakTracking, "BeginMemLeakTracking $INT(Start in this many seconds) $INT(Interval between checks) $INT(Megs increase that is a leak) $INT(First leak megs allowance) $STRING(Alert key prefix)");
	}
	else if (!estrLength(&pFirstSizePretty))
	{
		int iTimeBeforeBeginning = siTimeBegan - timeSecondsSince2000();

		if (iTimeBeforeBeginning <= 0)
		{
			estrPrintf(&sOverview.pSummary, "Mem Leak Tracking not yet begun, should do so at any moment (something screwy may be going on with the server being paused or something like that)");
		}
		else
		{
			timeSecondsDurationToPrettyEString(iTimeBeforeBeginning, &pDurationString);
			estrPrintf(&sOverview.pSummary, "Mem Leak Tracking expected to begin in %s", pDurationString);
		}

		sOverview.iCheckFrequency = siPauseBetweenChecks;
		sOverview.iIncreaseAmountThatIsALeak = siIncreaseAmount;
		sOverview.iFirstIncreaseAllowance = siFirstIncreaseAllowance;
		sOverview.pAlertKeyPrefix = strdup(spAlertKeyPrefix);
	}
	else
	{
		sOverview.iNumAlertsGenerated = siAlertCount;
		sOverview.iCheckFrequency = siPauseBetweenChecks;

		sOverview.iIncreaseAmountThatIsALeak = siIncreaseAmount;
		sOverview.iFirstIncreaseAllowance = siFirstIncreaseAllowance;
		sOverview.pAlertKeyPrefix = strdup(spAlertKeyPrefix);

		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - siTimeBegan, &pDurationString);
		estrPrintf(&sOverview.pSummary, "Mem Leak Tracking began %s ago. First size was %s.", pDurationString, pFirstSizePretty);
	
		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - siSavedTime, &pDurationString);
		estrConcatf(&sOverview.pSummary, "\nCurrent baseline value acquired %s ago, size was %s.", pDurationString, pOldSizePretty);		

		timeSecondsDurationToPrettyEString(timeSecondsSince2000() - siMostRecentTime, &pDurationString);
		estrConcatf(&sOverview.pSummary, "\nMost recently checked %s ago, size was %s.", pDurationString, spMostRecentSizePretty);		

		sOverview.pMostRecentAnalysis = spMostRecentAnalysis;

		sOverview.pAllocationsToIgnore = spAllocationsToIgnoreString;

		estrPrintf(&sOverview.pForceAnalysisNow, "MemLeakTracking_ForceAnalysisNow $NORETURN");
	}

	estrDestroy(&pDurationString);
	return &sOverview;

}


bool ProcessMemLeakTrackingOverviewIntoStructInfoForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	bool bRetVal;
	MemLeakTrackingOverview *pInfo = GetMemLeakTrackingOverview();

	bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
		pInfo, parse_MemLeakTrackingOverview, iAccessLevel, 0, pStructInfo, eFlags);

	return bRetVal;
}

AUTO_RUN;
void MemLeakTracking_Init(void)
{
	RegisterCustomXPathDomain(".memLeakTracking", ProcessMemLeakTrackingOverviewIntoStructInfoForHttp, NULL);


}

AUTO_COMMAND;
void MemLeakTracking_ForceAnalysisNow(void)
{
	sbForceAnalysisNow = true;
	MemLeakTrackingRepeatedCB(NULL, 0, NULL);
}

void MemLeakTracking_IgnoreLargeAllocation(char *pComment, size_t iAllocationSize)
{
	char *pSizeString = NULL;
	if (siTimeBegan)
	{
		estrMakePrettyBytesString(&pSizeString, iAllocationSize);

		estrConcatf(&spAllocationsToIgnoreString, "Ignoring %s from %s\n", pSizeString, pComment);

		estrDestroy(&pSizeString);

		siSizeOfAllocationsToIgnore += iAllocationSize;
	}
}
	


#include "MemLeakTracking_c_ast.c"
#include "MemLeakTracking_h_ast.c"


