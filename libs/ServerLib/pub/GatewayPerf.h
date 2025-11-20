/***************************************************************************
 
 
 ***************************************************************************/
#ifndef GATEWAYPERF_H__
#define GATEWAYPERF_H__
#pragma once


AUTO_STRUCT;
typedef struct GatewayPerfCounter
{
	char *pchName;			AST(NAME(Name) STRUCTPARAM)
	U64 iMinute;			AST(NAME(Minute) STRUCTPARAM)
	U64 iTenMinute;			AST(NAME(TenMinute) STRUCTPARAM)
	U64 iHour;				AST(NAME(Hour) STRUCTPARAM)
	U64 iDay;				AST(NAME(Day) STRUCTPARAM)
	U64 iWeek;				AST(NAME(Week) STRUCTPARAM)
	U64 iYear;				AST(NAME(Year) STRUCTPARAM)
} GatewayPerfCounter;

AUTO_STRUCT;
typedef struct GatewayPerfHistogram
{
	char *pchName;		AST(NAME(Name) STRUCTPARAM)
	U64 iMinBin;		AST(NAME(MinBin) STRUCTPARAM)
	U64 iMaxBin;		AST(NAME(MaxBin) STRUCTPARAM)
	U64 iMaxCount;		AST(NAME(MaxCount) STRUCTPARAM)
	U32 *pBinStarts;	AST(NAME(BinStarts))
	char **ppBars;		AST(NAME(Bars))
	U32 *pCounts;		AST(NAME(Counts))
} GatewayPerfHistogram;

AUTO_STRUCT;
typedef struct GatewayPerfTimer
{
	char *pchName;    AST(NAME(Name) STRUCTPARAM)
	U64 iCount;       AST(NAME(Count) STRUCTPARAM)
	U64 iAverageMS;   AST(NAME(Average) STRUCTPARAM)
	U64 iTotalMS;     AST(NAME(Total) STRUCTPARAM)
} GatewayPerfTimer;

AUTO_STRUCT;
typedef struct GatewayPerf
{
	EARRAY_OF(GatewayPerfCounter) ppCounters;		AST(NAME(Counter))
	EARRAY_OF(GatewayPerfHistogram) ppHistograms;	AST(NAME(Histogram))
	EARRAY_OF(GatewayPerfTimer) ppTimers;			AST(NAME(Timer))
} GatewayPerf;


extern GatewayPerf *g_pperfCur;

extern void gateperf_SetCurFromString(char *pch);
extern GatewayPerfCounter *gateperf_GetCounter(char *pchName);
extern GatewayPerfTimer *gateperf_GetTimer(char *pchName);
extern U64 gateperf_GetCount(char *pchName, U64 iDefault);

#endif /* #ifndef GATEWAYPERF_H__ */

/* End of File */
