#include "LogParsingFileBuckets.h"
#include "LogParsingFileBuckets_c_ast.h"
#include "file.h"
#include "timing.h"
#include "estring.h"
#include "StringCache.h"
#include "logging.h"
#include "wininclude.h"


AUTO_STRUCT;
typedef struct ProgressDataPoint
{
	U64 iBytesRead;
	float fTimePassed;
} ProgressDataPoint;

AUTO_STRUCT;
typedef struct FileNameBucket
{
	const char *pKey; AST(POOL_STRING)
	const char **ppFileNames; AST(POOL_STRING)
	const char **ppOutFileNames; AST(POOL_STRING)
	U32 *piOutFileSizes;
	LogFileParser *pParser; NO_AST

} FileNameBucket;





AUTO_STRUCT;
typedef struct FileNameBucketList
{
	char *pName;
	FileNameBucket **ppBuckets;
	LogParsingRestrictions *pRestrictions; NO_AST
	float fChunkingTime; NO_AST
	LogPostProcessingCB *pPostProcessingCB; NO_AST
	LogFileBucket_ChunkedLogProcessCB *pMainCB; NO_AST
	enumLogParsingFlags eFlags;

	bool bDoingChunkedProcessing;
	int iTotalLogCount;
	U64 iStartingBytes;
	ProgressDataPoint **ppDataPoints;

	//calculated from filenames, so will never be accurate to more than the nearest hour, could get 
	U32 iLikelyStartingTime; 
	U32 iLikelyEndingTime; 

} FileNameBucketList;



static int SortFilenamesByDate(const char **pName1, const char **pName2)
{
	U32 iTime1 = GetTimeFromLogFilename(*pName1);
	U32 iTime2 = GetTimeFromLogFilename(*pName2);

	if (iTime1 < iTime2)
	{
		return -1;
	}

	if (iTime1 > iTime2)
	{
		return 1;
	}

	return 0;
}



AUTO_FIXUPFUNC;
TextParserResult FileNameBucket_Fixup(FileNameBucket *pBucket, enumTextParserFixupType eFixupType, void *pExtraData)
{

	switch (eFixupType)
	{
	xcase FIXUPTYPE_DESTRUCTOR:
		if (pBucket->pParser)
		{
			LogFileParser_Destroy(pBucket->pParser);
		}
		break;
	}

	return PARSERESULT_SUCCESS;
}



void DivideFileListIntoBuckets(FileNameBucketList *pBucketList, char ***pppFileList)
{
	int i, j;
	int iNumFiles = eaSize(pppFileList);

	for (i=0; i < iNumFiles; i++)
	{
		const char *pKey = GetKeyFromLogFilename((*pppFileList)[i]);
		bool bFound = false;

		for (j=0 ; j < eaSize(&pBucketList->ppBuckets); j++)
		{
			if (pKey == pBucketList->ppBuckets[j]->pKey)
			{
				eaPush(&pBucketList->ppBuckets[j]->ppFileNames, allocAddString((*pppFileList)[i]));
				bFound = true;
				break;
			}
		}

		if (!bFound)
		{
			FileNameBucket *pBucket = StructCreate(parse_FileNameBucket);
			pBucket->pKey = pKey;
			eaPush(&pBucket->ppFileNames, allocAddString((*pppFileList)[i]));
			eaPush(&pBucketList->ppBuckets, pBucket);
		}
	}
}

void ApplyRestrictionsToBucketList(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions)
{
	int iBucketNum;
	int iCount;
	int j;
	U32 iFileSize;
	LogParsingRestrictions unRestricted = {0};
	U32 iStartingTime, iEndingTime;

	if (!pRestrictions)
	{
		pRestrictions = &unRestricted;
	}

	for (iBucketNum = 0; iBucketNum < eaSize(&pBucketList->ppBuckets); iBucketNum++)
	{
		iCount = eaSize(&pBucketList->ppBuckets[iBucketNum]->ppFileNames);


		eaQSort(pBucketList->ppBuckets[iBucketNum]->ppFileNames, SortFilenamesByDate);

		if (pRestrictions->iStartingTime)
		{
			U32 iNextTime = GetTimeFromLogFilename(pBucketList->ppBuckets[iBucketNum]->ppFileNames[0]);

			U32 iCurTime;
			for (j = 0; j < iCount - 1; j++)
			{
				iCurTime = iNextTime;

				if (iCurTime > pRestrictions->iEndingTime)
				{
					break;
				}

				iNextTime = GetTimeFromLogFilename(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j+1]);

				if (iNextTime >= pRestrictions->iStartingTime)
				{
	//				stashAddPointer(sFilesProcessedTable, pBucketList->ppBuckets[iBucketNum]->ppFileNames[j], NULL, false);
					eaPush(&pBucketList->ppBuckets[iBucketNum]->ppOutFileNames, pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);

					iFileSize = fileSize(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);
					if (strEndsWith(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j], ".gz"))
					{
						iFileSize *= ZIP_RATIO_APPROX;
					}

					ea32Push(&pBucketList->ppBuckets[iBucketNum]->piOutFileSizes, iFileSize);

				}
			}

			if ( j == iCount - 1 && iNextTime <= pRestrictions->iEndingTime)
			{
				eaPush(&pBucketList->ppBuckets[iBucketNum]->ppOutFileNames, pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);
				iFileSize = fileSize(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);
				if (strEndsWith(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j], ".gz"))
				{
					iFileSize *= ZIP_RATIO_APPROX;
				}

				ea32Push(&pBucketList->ppBuckets[iBucketNum]->piOutFileSizes, iFileSize);			
			}		
		}
		else
		{
			for (j=0; j < iCount; j++)
			{	
				eaPush(&pBucketList->ppBuckets[iBucketNum]->ppOutFileNames, pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);
				iFileSize = fileSize(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j]);
				if (strEndsWith(pBucketList->ppBuckets[iBucketNum]->ppFileNames[j], ".gz"))
				{
					iFileSize *= ZIP_RATIO_APPROX;
				}

				ea32Push(&pBucketList->ppBuckets[iBucketNum]->piOutFileSizes, iFileSize);		
			}
		}

		iStartingTime = GetTimeFromLogFilename(pBucketList->ppBuckets[iBucketNum]->ppFileNames[0]);
		iEndingTime = GetTimeFromLogFilename(eaTail(&pBucketList->ppBuckets[iBucketNum]->ppFileNames)) + 60 * 60;

		if (!pBucketList->iLikelyStartingTime)
		{
			pBucketList->iLikelyStartingTime = iStartingTime;
			pBucketList->iLikelyEndingTime = iEndingTime;
		}
		else
		{
			if (iStartingTime < pBucketList->iLikelyStartingTime)
			{
				pBucketList->iLikelyStartingTime = iStartingTime;
			}

			if (iEndingTime > pBucketList->iLikelyEndingTime)
			{
				pBucketList->iLikelyEndingTime = iEndingTime;
			}
		}
	}
}

bool UpdateNextLogInBucket(FileNameBucketList *pBucketList, FileNameBucket *pBucket, LogParsingRestrictions *pRestrictions)
{
	while (eaSize(&pBucket->ppOutFileNames))
	{
		const char *pNextName;
		if (!pBucket->pParser)
		{
			pBucket->pParser = LogFileParser_Create(pRestrictions, pBucketList->pPostProcessingCB, pBucketList->eFlags);
		}

		pNextName = pBucket->ppOutFileNames[0];
		eaRemove(&pBucket->ppOutFileNames, 0);
		ea32Remove(&pBucket->piOutFileSizes, 0);

		if (LogFileParser_OpenFile(pBucket->pParser, pNextName))
		{
			return true;
		}
	}

	return false;
}

U32 GetIDOfNextLogFromBucket(FileNameBucketList *pBucketList, FileNameBucket *pBucket, LogParsingRestrictions *pRestrictions)
{
	U32 iRetVal;

	if (pBucket->pParser)
	{
		iRetVal = LogFileParser_GetIDOfNextLog(pBucket->pParser);

		if (iRetVal)
		{
			return iRetVal;
		}
	}

	if(UpdateNextLogInBucket(pBucketList, pBucket, pRestrictions) && pBucket->pParser)
	{
		return LogFileParser_GetIDOfNextLog(pBucket->pParser);
	}

	return 0;
}

U32 GetTimeOfNextLogFromBucket(FileNameBucketList *pBucketList, FileNameBucket *pBucket, LogParsingRestrictions *pRestrictions)
{
	U32 iRetVal;

	if (pBucket->pParser)
	{
		iRetVal = LogFileParser_GetTimeOfNextLog(pBucket->pParser);

		if (iRetVal)
		{
			return iRetVal;
		}
	}

	if(UpdateNextLogInBucket(pBucketList, pBucket, pRestrictions) && pBucket->pParser)
	{
		return LogFileParser_GetTimeOfNextLog(pBucket->pParser);
	}

	return 0;
}

char *GetNextLogStringFromBucket(FileNameBucket *pBucket, char **ppFileName /*NOT AN ESTRING*/)
{
	char *return_val;

	PERFINFO_AUTO_START_FUNC();

	*ppFileName = LogFileParser_GetFileName(pBucket->pParser);
	return_val = LogFileParser_GetNextLogString(pBucket->pParser);

	PERFINFO_AUTO_STOP();
	return return_val;
}

U64 GetBytesRemainingInBucket(FileNameBucket *pBucket)
{
	U64 iRetVal = 0;
	int i;
	int iNumSizes;

	if (pBucket->pParser)
	{
		iRetVal = LogFileParser_GetBytesRemaining(pBucket->pParser);
	}
	
	iNumSizes = ea32Size(&pBucket->piOutFileSizes);

	for (i=0; i < iNumSizes; i++)
	{
		iRetVal += pBucket->piOutFileSizes[i];
	}

	return iRetVal;
}

U64 GetBucketListRemainingBytes(FileNameBucketList *pBucketList)
{
	int i;
	U64 iRetVal = 0;

	for (i=0; i < eaSize(&pBucketList->ppBuckets); i++)
	{
		iRetVal += GetBytesRemainingInBucket(pBucketList->ppBuckets[i]);
	}

	return iRetVal;
}



/*
void ProcessAllLogsFromBucketList(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions)
{
	int iNumBuckets = eaSize(&pBucketList->ppBuckets);
	int i;
	int iCurNextIndex;
	U32 iCurNextTime;
	ParsedLog *pLog;
	U64 iStartingBytes = 0;

	int iCount = 0;
	int timer = timerAlloc();

	ProgressDataPoint **ppDataPoints = NULL;

	iStartingBytes = GetBucketListRemainingBytes(pBucketList);
	timerStart(timer );

	while (1)
	{
		char *pFileName;


		iCurNextIndex = -1;

		for (i=0; i < iNumBuckets; i++)
		{
			FileNameBucket *pBucket = pBucketList->ppBuckets[i];
			
			U32 iTime = GetTimeOfNextLogFromBucket(pBucket, pRestrictions);

			if (iTime)
			{
				if (iCurNextIndex == -1 || iTime < iCurNextTime)
				{
					iCurNextIndex = i;
					iCurNextTime = iTime;
				}
			}
		}

		if (iCurNextIndex == -1)
		{
			break;
		}

		pLog = GetNextLogFromBucket(pBucketList->ppBuckets[iCurNextIndex], &pFileName);
		if (pLog)
		{
			ProcessSingleLog(pFileName, pLog, false);
//			LogParserBinning_AddLog(pLog);
			ProcessAllProceduralLogs();

			iCount++;

			if (iCount % 5000 == 0)
			{

				U64 iRemainingBytes = GetBucketListRemainingBytes(pBucketList);
				U64 iBytesRead = iStartingBytes - iRemainingBytes;
				
				float fTimePassed = timerElapsed(timer);

				ProgressDataPoint *pNewDataPoint = calloc(sizeof(ProgressDataPoint), 1);

				char *pTotalBytesString;
				char *pBytesReadString;

				estrStackCreate(&pTotalBytesString);
				estrStackCreate(&pBytesReadString);

				estrMakePrettyBytesString(&pTotalBytesString, iStartingBytes);
				estrMakePrettyBytesString(&pBytesReadString, iBytesRead);

				pNewDataPoint->iBytesRead = iBytesRead;
				pNewDataPoint->fTimePassed = fTimePassed;

				eaPush(&ppDataPoints, pNewDataPoint);

				if (eaSize(&ppDataPoints) >= 6)
				{
					ProgressDataPoint *pHalfwayPoint = ppDataPoints[eaSize(&ppDataPoints) / 2];
					double fBytesPerSecond = (((double)iBytesRead) - ((double)pHalfwayPoint->iBytesRead)) / (fTimePassed - pHalfwayPoint->fTimePassed);
					float fTimeRemaining = ((double)iRemainingBytes) / fBytesPerSecond;

					char *pTimeRemainingString;
					estrStackCreate(&pTimeRemainingString);
					timeSecondsDurationToPrettyEString((U32)fTimeRemaining, &pTimeRemainingString);


					printf("%s/%s. %s remaining\n", pBytesReadString, pTotalBytesString, pTimeRemainingString);

					estrDestroy(&pTimeRemainingString);


				}
				else
				{
					printf("%s/%s\n", pBytesReadString, pTotalBytesString);
				}

				estrDestroy(&pTotalBytesString);
				estrDestroy(&pBytesReadString);

			}
		}
	}

	eaDestroyEx(&ppDataPoints, NULL);
	timerFree(timer);
}
*/




void BeginChunkedLogProcessing(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions)
{
	char *pTotalBytesString = NULL;
	assert(!pBucketList->bDoingChunkedProcessing);
	pBucketList->bDoingChunkedProcessing = true;

	pBucketList->iTotalLogCount = 0;
	pBucketList->iStartingBytes = GetBucketListRemainingBytes(pBucketList);
	estrMakePrettyBytesString(&pTotalBytesString, pBucketList->iStartingBytes);
	printf("Total Bytes to scan: %s\n", pTotalBytesString);
	pBucketList->ppDataPoints = NULL;
	pBucketList->pRestrictions = pRestrictions;
	estrDestroy(&pTotalBytesString);
}



void AbortChunkedLogProcessing(FileNameBucketList *pBucketList)
{
	pBucketList->bDoingChunkedProcessing = false;
}

// Get the server and category associated with a log filename.
static void GetLogName(SA_PARAM_NN_VALID char **ppName, SA_PARAM_NN_VALID const char *pFileName)
{
	const char *ptr = strchr(pFileName, '.');
	if (ptr)
	{
		size_t len = ptr - pFileName;
		estrSetSize(ppName, (int)len);
		memcpy(*ppName, pFileName, len);
	}
	else
		estrCopy2(ppName, pFileName);
}

#define NUMLOGSTORESEND 20000

static int PassThroughLogFormatting(char **estrOut, enumLogCategory eCategory,char const *fmt, va_list ap)
{
	estrConcatfv(estrOut, fmt, ap);
	return 1;
}

bool ContinueChunkedLogProcessing(FileNameBucketList *pBucketList, float fTimeToRun, int *piPercentComplete, char **ppStatusString)
{

	int iNumBuckets = eaSize(&pBucketList->ppBuckets);
	int iCurNextIndex = 0;
	U32 iCurNextTime = 0;
	U32 iCurNextID = 0;
	ParsedLog *pLog;
	char *pLogString;
	int iNumLogsSent = 0;
	int iLogCount = 0;

	int i;
	bool bDone = false;

	int timer = timerAlloc();
	PERFINFO_AUTO_START_FUNC();
	timerStart(timer );


	while (1)
	{
		char *pFileName;
		char *name = NULL;
		PERFINFO_AUTO_START("GetNextIndex", 1);

		iCurNextIndex = -1;

		for (i=0; i < iNumBuckets; i++)
		{
			FileNameBucket *pBucket = pBucketList->ppBuckets[i];
			U32 iTime;
			U32 iLogID;
			iTime = GetTimeOfNextLogFromBucket(pBucketList, pBucket, pBucketList->pRestrictions);
			iLogID = GetIDOfNextLogFromBucket(pBucketList, pBucket, pBucketList->pRestrictions);

			if (iTime)
			{
				if (iCurNextIndex == -1 || iTime < iCurNextTime || (iTime == iCurNextTime && iLogID && iLogID < iCurNextID))
				{
					iCurNextIndex = i;
					iCurNextTime = iTime;
					iCurNextID = iLogID;
				}
			}
		}

		PERFINFO_AUTO_STOP();

		if (iCurNextIndex == -1)
		{
			bDone = true;
			break;
		}


		pLogString = GetNextLogStringFromBucket(pBucketList->ppBuckets[iCurNextIndex], &pFileName);
		estrStackCreate(&name);
		GetLogName(&name, pFileName);
		PERFINFO_AUTO_START("Stand alone log parsing", 1);

		PERFINFO_AUTO_START_LOGPARSE(name);
			

		estrDestroy(&name);
		if(pLogString)
		{
			iLogCount++;

	
			pLog = CreateLogFromString(pLogString, pBucketList->ppBuckets[iCurNextIndex]->pParser);
			if(pLog)
			{
				pBucketList->pMainCB(pFileName, pLog);
			}
			

			pBucketList->iTotalLogCount++;

			if ((pBucketList->iTotalLogCount % 128 == 0 && timerElapsed(timer) >= fTimeToRun))
			{
				U64 iRemainingBytes = GetBucketListRemainingBytes(pBucketList);
				U64 iBytesRead = pBucketList->iStartingBytes - iRemainingBytes;
				
				float fTimePassed = timerElapsed(timer) + pBucketList->fChunkingTime;

				ProgressDataPoint *pNewDataPoint = calloc(sizeof(ProgressDataPoint), 1);

				char *pTotalBytesString;
				char *pBytesReadString;

				PERFINFO_AUTO_START("Progress bar", 1);

				estrStackCreate(&pTotalBytesString);
				estrStackCreate(&pBytesReadString);

				estrMakePrettyBytesString(&pTotalBytesString, pBucketList->iStartingBytes);
				estrMakePrettyBytesString(&pBytesReadString,  iBytesRead);

				pNewDataPoint->iBytesRead = iBytesRead;
				pNewDataPoint->fTimePassed = fTimePassed;

				eaPush(&pBucketList->ppDataPoints, pNewDataPoint);

				if (eaSize(& pBucketList->ppDataPoints) >= 6)
				{
					ProgressDataPoint *pHalfwayPoint =  pBucketList->ppDataPoints[eaSize(&pBucketList->ppDataPoints) / 2];
					double fBytesPerSecond = (((double)iBytesRead) - ((double)pHalfwayPoint->iBytesRead)) / (fTimePassed - pHalfwayPoint->fTimePassed);
					float fTimeRemaining = ((double)iRemainingBytes) / fBytesPerSecond;

					char *pTimeRemainingString;
					estrStackCreate(&pTimeRemainingString);
					timeSecondsDurationToPrettyEString((U32)fTimeRemaining, &pTimeRemainingString);


					estrPrintf(ppStatusString, "%s/%s. %s remaining\n", pBytesReadString, pTotalBytesString, pTimeRemainingString);

					estrDestroy(&pTimeRemainingString);


				}
				else
				{
					estrPrintf(ppStatusString, "%s/%s\n", pBytesReadString, pTotalBytesString);
				}

				*piPercentComplete = ((float)iBytesRead / (float)pBucketList->iStartingBytes) * 100;

				estrDestroy(&pTotalBytesString);
				estrDestroy(&pBytesReadString);

				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				PERFINFO_AUTO_STOP();
				break;
			}
		}

		PERFINFO_AUTO_STOP();
		PERFINFO_AUTO_STOP();
	}

	printf("%d logs in %f seconds.\n", iLogCount, timerElapsed(timer));

	pBucketList->fChunkingTime += timerElapsed(timer);
	timerFree(timer);

	PERFINFO_AUTO_STOP();

	if (bDone)
	{
		pBucketList->bDoingChunkedProcessing = false;
		eaDestroyEx(&pBucketList->ppDataPoints, NULL);
		return true;
	}
	else
	{
		return false;
	}
}



FileNameBucketList *CreateFileNameBucketList(LogFileBucket_ChunkedLogProcessCB *pProcessingCB, 
	LogPostProcessingCB *pPostProcessingCB, enumLogParsingFlags eFlags)
{
	FileNameBucketList *pList = StructCreate(parse_FileNameBucketList);
	pList->pMainCB = pProcessingCB;
	pList->pPostProcessingCB = pPostProcessingCB;
	pList->eFlags = eFlags;

	return pList;
}

void DestroyFileNameBucketList(FileNameBucketList *pList)
{
	if (pList)
	{
		StructDestroy(parse_FileNameBucketList, pList);
	}


}

bool GetNextLogStringFromBucketList(FileNameBucketList *pBucketList, char **ppOutString /*NOT AN ESTRING*/, char **ppOutFileName,
	U32 *piOutTime, U32 *piOutIndex)
{
	int i;
	int iNumBuckets = eaSize(&pBucketList->ppBuckets);
	int iCurNextIndex = -1;
	U32 iCurNextTime = 0;
	U32 iCurNextID = 0;
	char *pString;
	char *pFileName;

	for (i=0; i < iNumBuckets; i++)
	{
		FileNameBucket *pBucket = pBucketList->ppBuckets[i];
		U32 iTime;
		U32 iLogID;
		iTime = GetTimeOfNextLogFromBucket(pBucketList, pBucket, pBucketList->pRestrictions);
		iLogID = GetIDOfNextLogFromBucket(pBucketList, pBucket, pBucketList->pRestrictions);

		if (iTime)
		{
			if (iCurNextIndex == -1 || iTime < iCurNextTime || (iTime == iCurNextTime && iLogID && iLogID < iCurNextID))
			{
				iCurNextIndex = i;
				iCurNextTime = iTime;
				iCurNextID = iLogID;
			}
		}
	}

	if (iCurNextIndex == -1)
	{
		return false;
	}

	pString = GetNextLogStringFromBucket(pBucketList->ppBuckets[iCurNextIndex], &pFileName);

	if (ppOutString)
	{
		*ppOutString = pString;
	}
	if (ppOutFileName)
	{
		*ppOutFileName = pFileName;
	}
	if (piOutTime)
	{
		*piOutTime = iCurNextTime;
	}
	if (piOutIndex)
	{
		*piOutIndex = iCurNextID;
	}

	return true;

}

void GetLikelyStartingAndEndingTimesFromBucketList(FileNameBucketList *pBucketList, U32 *piStartingTime, U32 *piEndingTime)
{
	*piStartingTime = pBucketList->iLikelyStartingTime;
	*piEndingTime = pBucketList->iLikelyEndingTime;
}



#include "LogParsingFileBuckets_c_ast.c"