#pragma once
GCC_SYSTEM

/*
Three structs defined in this header file.

A "FloatMaxTracker" quickly and efficiently gives you the max value over a given time frame. If you want a min, just pass
//in -values

A "Float Averager" is something which you give a sequence of floats to, and it does some internal tracking, and, when asked,
tells you the average value over the past minute, past hour or past day.

A "count Averager" is something where you tell it when something happens, and it does some internal tracking, and, when asked,
tells you the average number of times per second that thing has happened over the past minute, past hour, or past day.
*/


typedef struct FloatMaxTracker FloatMaxTracker;

FloatMaxTracker *FloatMaxTracker_Create(void);
void FloatMaxTracker_AddDataPoint(FloatMaxTracker *pTracker, float fVal, U32 iTime);
float FloatMaxTracker_GetMax(FloatMaxTracker *pTracker, U32 iStartingTime);
void FloatMaxTracker_Destroy(FloatMaxTracker **ppTracker);


//what type of averaging to do. Doing any type of averaging implies doing all "faster" kinds, so if you 
//request daily averages, you'll get minute and hour averages for free
typedef enum
{
	AVERAGE_INSTANTANEOUS = 1,	//the most recent value
	AVERAGE_MAX,
	AVERAGE_MIN,

	AVERAGE_MINUTE,				//average over the past minute
	AVERAGE_HOUR,				//average over the past hour
	AVERAGE_DAY					//average over the past day
} enumAverageType;

typedef struct IntAverager IntAverager;

IntAverager *IntAverager_Create(enumAverageType eRequestedType);
int IntAverager_Query(IntAverager *pAverager, enumAverageType eQueryType);				// May have some slight error
int IntAverager_QuerySlowPrecise(IntAverager *pAverager, enumAverageType eQueryType);		// Slower but more precise computation
void IntAverager_AddDatapoint(IntAverager *pAverager, int iVal);
void IntAverager_AddDatapoint_SpecifyTime(IntAverager *pAverager, int iVal, U32 iTime);
int IntAverager_CountTotalDatapoints(IntAverager *pAverager);
void IntAverager_Destroy(IntAverager *pAverager);
void IntAverager_Reset(IntAverager *pAverager);
//NOTE - IntAveragers seem to be very susceptible to creeping numerical imprecision... suggest using Int64 Averagers whenever possible instead




typedef struct CountAverager CountAverager;
//a "counter averager" counts how many times a certain thing happens each second,
//then averages that per minute, per hour, etc.
CountAverager *CountAverager_Create(enumAverageType eRequestedType);
int CountAverager_Query(CountAverager *pCounter, enumAverageType eQueryType);
void CountAverager_ItHappened(CountAverager *pCounter);
void CountAverager_ItHappened_SpecifyTime(CountAverager *pCounter, U32 iTime);
S64 CountAverager_Total(CountAverager *pCounter);
void CountAverager_Destroy(CountAverager *pCounter);
void CountAverager_Reset(CountAverager *pCounter);

