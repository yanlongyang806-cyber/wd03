#pragma once

typedef struct TriviaData TriviaData;
typedef struct TriviaOverview TriviaOverview;
typedef struct ErrorEntry ErrorEntry;

void InitializeTriviaFilter(void);
void FilterIncomingTriviaData(TriviaData ***eaTriviaData);
void FilterForTriviaOverview(TriviaOverview *overview, CONST_EARRAY_OF(TriviaData) eaTrivia);

void TriviaDataInit(void);

//void LogAllTriviaOverviews();
void LogTriviaOverview(U32 uID, ErrorEntry *pEntry);
void GetOldTriviaDataFileName(U32 uID, char *destfilename, size_t dst_size);
void GetTriviaDataFileName(U32 uID, char *destfilename, size_t dst_size);
void GetTriviaDataZipFileName(U32 uID, char *destfilename, size_t destfilename_size);
void ParseTriviaLogLine(char *line, TriviaOverview *pOverview);
void LogTriviaData(U32 uID, CONST_EARRAY_OF(TriviaData) ppData);