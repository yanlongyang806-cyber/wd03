#pragma once

#include "LogParsing.h"

typedef void LogFileBucket_ChunkedLogProcessCB(char *pFileName, ParsedLog *pLog);


typedef struct FileNameBucketList FileNameBucketList;

FileNameBucketList *CreateFileNameBucketList(LogFileBucket_ChunkedLogProcessCB *pProcessingCB, 
	LogPostProcessingCB *pPostProcessingCB, enumLogParsingFlags eFlags);

void DestroyFileNameBucketList(FileNameBucketList *pList);

void DivideFileListIntoBuckets(FileNameBucketList *pBucketList, char ***pppFileList);
void ApplyRestrictionsToBucketList(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions);

//processes all logs at once
void ProcessAllLogsFromBucketList(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions);

//processes logs in chunks
void BeginChunkedLogProcessing(FileNameBucketList *pBucketList, LogParsingRestrictions *pRestrictions);
//returns true if complete. Sets current status and percent completion
bool ContinueChunkedLogProcessing(FileNameBucketList *pBucketList, float fTimeToRun, int *piPercentComplete, char **ppStatusString);

void AbortChunkedLogProcessing(FileNameBucketList *pBucketList);

//returns true if one was found
bool GetNextLogStringFromBucketList(FileNameBucketList *pBucketList, char **ppOutString /*NOT AN ESTRING*/, char **ppOutFileName,
	U32 *piOutTime, U32 *piOutLogID);

U64 GetBucketListRemainingBytes(FileNameBucketList *pBucketList);

void GetLikelyStartingAndEndingTimesFromBucketList(FileNameBucketList *pBucketList, U32 *piStartingTime, U32 *piEndingTime);

