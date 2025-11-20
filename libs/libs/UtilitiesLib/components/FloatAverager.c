
#include "FloatAverager.h"
#include "timing.h"
#include "blockearray.h"

typedef struct
{
	U32 iTime;
	int iVal;
} IntAveragerDataPoint;

struct IntAverager
{
	enumAverageType eMyType;
	S64 iCurSum;

	S64 iTotalDataPoints;

	//a ring buffer of IntAveragerDataPoints
	int iCurRingSize;

	//iCurRingStart is the index of the oldest entry in the ring
	int iCurRingStart;
	int iCurRingCount;

	int iMaxIndex;
	int iMinIndex;

	IntAveragerDataPoint *pRing;

	U32 iLastTimeSubmitted;
	IntAverager *pBiggerAverager; //if I am an AVERAGE_MINUTE, then this is an AVERAGE_HOUR which aggregates my averages, etc
};


IntAverager *IntAverager_CreateInternal(enumAverageType eRequestedType, int iStartingSize)
{
	IntAverager *pAverager = (IntAverager*)calloc(sizeof(IntAverager), 1);
	
	pAverager->eMyType = eRequestedType;
	pAverager->iCurRingSize = iStartingSize;
	pAverager->pRing = calloc(sizeof(IntAveragerDataPoint) * iStartingSize, 1);

	return pAverager;
}


IntAverager *IntAverager_Create(enumAverageType eRequestedType)
{
	switch (eRequestedType)
	{
	case AVERAGE_MINUTE:
		return IntAverager_CreateInternal(AVERAGE_MINUTE, 64);
	case AVERAGE_HOUR:
		{
			IntAverager *pMinuteAverager = IntAverager_CreateInternal(AVERAGE_MINUTE, 64);
			IntAverager *pHourAverager = IntAverager_CreateInternal(AVERAGE_HOUR, 64);
			pMinuteAverager->pBiggerAverager = pHourAverager;
			return pMinuteAverager;
		}

	case AVERAGE_DAY:
		{
			IntAverager *pMinuteAverager = IntAverager_CreateInternal(AVERAGE_MINUTE, 64);
			IntAverager *pHourAverager = IntAverager_CreateInternal(AVERAGE_HOUR, 64);
			IntAverager *pDayAverager = IntAverager_CreateInternal(AVERAGE_DAY, 32);
			pMinuteAverager->pBiggerAverager = pHourAverager;
			pHourAverager->pBiggerAverager = pDayAverager;
			return pMinuteAverager;
		}
	default:
		assertmsg(0, "Unsupported type passed to IntAverager_Create");
		return NULL;
	}
}

int IntAverager_Query(IntAverager *pAverager, enumAverageType eQueryType)
{
	if (pAverager->iCurRingCount == 0)
	{
		return 0;
	}
		
	if (eQueryType == AVERAGE_INSTANTANEOUS)
	{
		int iMostRecentIndex = (pAverager->iCurRingStart+pAverager->iCurRingCount - 1) % pAverager->iCurRingSize;
		return pAverager->pRing[iMostRecentIndex].iVal;
	}


	if (eQueryType == AVERAGE_MAX)
	{
		return pAverager->pRing[pAverager->iMaxIndex].iVal;
	}
	if (eQueryType == AVERAGE_MIN)
	{
		return pAverager->pRing[pAverager->iMinIndex].iVal;
	}

	if (eQueryType > pAverager->eMyType && pAverager->pBiggerAverager && pAverager->pBiggerAverager->iTotalDataPoints)
	{
		return IntAverager_Query(pAverager->pBiggerAverager, eQueryType);
	}

	return pAverager->iCurSum / pAverager->iCurRingCount;
}

// Determine weight of this average relative to the next bigger averager.
static int IntAverager_WeightInternal(enumAverageType eQueryType)
{
	int factor;
	switch (eQueryType)
	{
		case AVERAGE_MINUTE:
			factor = 60;
			break;
		case AVERAGE_HOUR:
			factor = 24;
			break;
		case AVERAGE_DAY:
		default:
			assertmsg(0, "Unsupported IntAverager query");
	}
	return factor;
}

// Count the equivalent samples of this averager and all bigger averagers.
static int IntAverager_CountInternal(IntAverager *pAverager)
{
	int count = pAverager->iCurRingCount;
	if (pAverager->pBiggerAverager)
		count += IntAverager_CountInternal(pAverager->pBiggerAverager) * IntAverager_WeightInternal(pAverager->eMyType);
	return count;
}

// Slower but more precise computation
// This is still not exact, but it eliminates artifacts at the very beginning of operation when smaller average data has not yet
// been incorporated into the larger average.
int IntAverager_QuerySlowPrecise(IntAverager *pAverager, enumAverageType eQueryType)
{
	int biggerAverage;
	int count;

	// More complicated computation only necessary for nested averagers.
	if (eQueryType <= pAverager->eMyType || !pAverager->pBiggerAverager)
		return IntAverager_Query(pAverager, eQueryType);

	// Query the bigger averager.
	biggerAverage = IntAverager_QuerySlowPrecise(pAverager->pBiggerAverager, eQueryType);
	if (pAverager->iCurRingCount == 0)
		return biggerAverage;

	// Calculate weighted average.
	count = IntAverager_CountInternal(pAverager);
	return (pAverager->iCurSum / pAverager->iCurRingCount + count * biggerAverage) / (count + 1);
}

S64 IntAverager_GetSum(IntAverager *pAverager, enumAverageType eQueryType)
{
	if (pAverager->iCurRingCount == 0)
	{
		return 0;
	}
		
	if (eQueryType == AVERAGE_INSTANTANEOUS)
	{
		assertmsg(0, "Getting an INSTANTANEOUS sum from an int averager has no meaning");
		return 0;
	}


	if (eQueryType > pAverager->eMyType && pAverager->pBiggerAverager && pAverager->pBiggerAverager->iTotalDataPoints)
	{
		return IntAverager_GetSum(pAverager->pBiggerAverager, eQueryType);
	}

	
		
	return pAverager->iCurSum;
	
}


void IntAverager_AddDatapoint(IntAverager *pAverager, int iVal)
{
	IntAverager_AddDatapoint_SpecifyTime(pAverager, iVal, timeSecondsSince2000());
}


void IntAverager_AddDatapoint_SpecifyTime(IntAverager *pAverager, int iVal, U32 iCurTime)
{
	U32 iOldestAge = 0;
	int iNewIndex;
	int iMax = pAverager->pRing[pAverager->iMaxIndex].iVal;
	int iMin = pAverager->pRing[pAverager->iMinIndex].iVal;

	switch (pAverager->eMyType)
	{
	case AVERAGE_MINUTE:
		iOldestAge = iCurTime - 59;
		break;
	case AVERAGE_HOUR:
		iOldestAge = iCurTime - (60 * 60 - 1);
		break;
	case AVERAGE_DAY:
		iOldestAge = iCurTime - (60 * 60 * 24 - 1);
		break;
	}

	pAverager->iTotalDataPoints++;

	//first, remove any old entries
	while (pAverager->iCurRingCount && pAverager->pRing[pAverager->iCurRingStart].iTime < iOldestAge)
	{
		pAverager->iCurRingCount--;

		if (pAverager->iCurRingStart == pAverager->iMaxIndex) pAverager->iMaxIndex = -1;
		if (pAverager->iCurRingStart == pAverager->iMinIndex) pAverager->iMinIndex = -1;

		//note that we don't yet recalculate the average as we will do so when we're done adding our new data point
		pAverager->iCurSum -= pAverager->pRing[pAverager->iCurRingStart].iVal;
		pAverager->iCurRingStart++;
		pAverager->iCurRingStart %= pAverager->iCurRingSize;
	}

	//now expand our ring if necessary (hopefully rare)
	if (pAverager->iCurRingCount == pAverager->iCurRingSize)
	{
		int iNewSize = pAverager->iCurRingSize * 2;
		int iNumToCopyPass1 = pAverager->iCurRingSize - pAverager->iCurRingStart;
		int iNumToCopyPass2 = pAverager->iCurRingSize - iNumToCopyPass1;
		IntAveragerDataPoint *pNewRing = calloc(sizeof(IntAveragerDataPoint) * iNewSize, 1);

		memcpy(pNewRing, pAverager->pRing + pAverager->iCurRingStart, sizeof(IntAveragerDataPoint) * iNumToCopyPass1);
		memcpy(pNewRing + iNumToCopyPass1, pAverager->pRing, sizeof(IntAveragerDataPoint) * iNumToCopyPass2);

		free(pAverager->pRing);
		pAverager->pRing = pNewRing;

		pAverager->iCurRingStart = 0;
		pAverager->iCurRingSize = iNewSize;
		pAverager->iMaxIndex = pAverager->iMinIndex = 0;
	}

	iNewIndex = pAverager->iCurRingStart + pAverager->iCurRingCount;
	iNewIndex %= pAverager->iCurRingSize;

	pAverager->pRing[iNewIndex].iTime = iCurTime;
	pAverager->pRing[iNewIndex].iVal = iVal;
	pAverager->iCurRingCount++;

	
	
	pAverager->iCurSum += iVal;
	

	//Update MIN/MAX
	
	
	
	if (pAverager->iMaxIndex < 0)
	{
		int i,j;
		int iCurrent;
		iMax = pAverager->pRing[pAverager->iCurRingStart].iVal;
		pAverager->iMaxIndex = pAverager->iCurRingStart;


		for (i = 1; i < pAverager->iCurRingCount; i++)
		{
			j = (pAverager->iCurRingStart + i) % pAverager->iCurRingSize;
			iCurrent = pAverager->pRing[j].iVal;
			if (iCurrent >= iMax)
			{
				pAverager->iMaxIndex = j;
				iMax = iCurrent;
			}
		}
	}
	else
	{
		if (iVal >= iMax) pAverager->iMaxIndex = iNewIndex;
	}

	if (pAverager->iMinIndex < 0)
	{
		int i,j;
		int iCurrent;
		iMin = pAverager->pRing[pAverager->iCurRingStart].iVal;
		pAverager->iMinIndex = pAverager->iCurRingStart;

		for (i = 1; i < pAverager->iCurRingCount; i++)
		{
			j = (pAverager->iCurRingStart + i) % pAverager->iCurRingSize;
			iCurrent = pAverager->pRing[j].iVal;
			if (iCurrent <= iMin)
			{
				pAverager->iMinIndex = j;
				iMin = iCurrent;
			}
		}
	}
	else
	{
		if (iVal <= iMin) pAverager->iMinIndex = iNewIndex;
	}


	if (pAverager->pBiggerAverager)
	{
		if (pAverager->iLastTimeSubmitted == 0)
		{
			pAverager->iLastTimeSubmitted = iCurTime - 1;
		}
		else
		{
			int iTimeSinceLastSubmission = iCurTime - pAverager->iLastTimeSubmitted;
	
			switch (pAverager->eMyType)
			{
			case AVERAGE_MINUTE:
				if (iTimeSinceLastSubmission >= 60)
				{
					IntAverager_AddDatapoint(pAverager->pBiggerAverager, pAverager->iCurSum / pAverager->iCurRingCount);
					pAverager->iLastTimeSubmitted = iCurTime;

				}
				break;
			case AVERAGE_HOUR:
				if (iTimeSinceLastSubmission >= 60 * 60)
				{
					IntAverager_AddDatapoint(pAverager->pBiggerAverager, pAverager->iCurSum / pAverager->iCurRingCount);
					pAverager->iLastTimeSubmitted = iCurTime;
				}
				break;
			}
		}
	}
}


int IntAverager_CountTotalDatapoints(IntAverager *pAverager)
{
	return pAverager->iTotalDataPoints;
}

void IntAverager_Destroy(IntAverager *pAverager)
{
	if (!pAverager)
	{
		return;
	}
	
	if (pAverager->pBiggerAverager)
	{
		IntAverager_Destroy(pAverager->pBiggerAverager);
	}

	free(pAverager->pRing);
	free(pAverager);
}

void IntAverager_Reset(IntAverager *pAverager)
{
	pAverager->iCurSum = 0;
	pAverager->iCurRingCount = 0;
	pAverager->iCurRingStart = 0;
	pAverager->iLastTimeSubmitted = 0;
	pAverager->iTotalDataPoints = 0;

	if (pAverager->pBiggerAverager)
	{
		IntAverager_Reset(pAverager->pBiggerAverager);
	}
}



struct CountAverager
{
	int iCurCount;
	S64 iTotalCount;
	U32 iCountedTime;
	IntAverager *pAverager;
};
	
//a "counter averager" counts how many times a certain thing happens each second,
//then averages that per minute, per hour, etc.
CountAverager *CountAverager_Create(enumAverageType eRequestedType)
{
	CountAverager *pCounter = calloc(sizeof(CountAverager), 1);
	pCounter->pAverager = IntAverager_Create(eRequestedType);

	return pCounter;
}

int CountAverager_Query(CountAverager *pCounter, enumAverageType eQueryType)
{
	U32 iCurTime = timeSecondsSince2000();

	if (pCounter->iCountedTime == 0)
	{
		return 0;
	}

	while (pCounter->iCountedTime < iCurTime)
	{
		IntAverager_AddDatapoint(pCounter->pAverager, pCounter->iCurCount);
		pCounter->iCurCount = 0;
		pCounter->iCountedTime++;
	}

	return IntAverager_Query(pCounter->pAverager, eQueryType);
}
/*
S64 CountAverager_GetCount(CountAverager *pCounter, enumAverageType eQueryType)
{
	U32 iCurTime = timeSecondsSince2000();

	if (pCounter->iCountedTime == 0)
	{
		return 0.0f;
	}

	while (pCounter->iCountedTime < iCurTime)
	{
		IntAverager_AddDatapoint(pCounter->pAverager, (float)pCounter->iCurCount);
		pCounter->iCurCount = 0;
		pCounter->iCountedTime++;
	}

	return pCounter->iCurCount + IntAverager_GetSum(pCounter->pAverager, eQueryType);
}
*/

void CountAverager_ItHappened_SpecifyTime(CountAverager *pCounter, U32 iCurTime)
{
	pCounter->iTotalCount++;

	if (iCurTime == pCounter->iCountedTime)
	{
		pCounter->iCurCount++;
		return;
	}

	if (pCounter->iCountedTime == 0)
	{
		pCounter->iCurCount = 1;
		pCounter->iCountedTime = iCurTime;
		return;
	}

	IntAverager_AddDatapoint_SpecifyTime(pCounter->pAverager, pCounter->iCurCount, iCurTime);

	pCounter->iCountedTime++;
	pCounter->iCurCount = 1;

	while (pCounter->iCountedTime < iCurTime)
	{
		IntAverager_AddDatapoint_SpecifyTime(pCounter->pAverager, 0.0f, iCurTime);
		pCounter->iCountedTime++;
	}
}

void CountAverager_ItHappened(CountAverager *pCounter)
{
	CountAverager_ItHappened_SpecifyTime(pCounter, timeSecondsSince2000());
}



	
S64 CountAverager_Total(CountAverager *pCounter)
{
	return pCounter->iTotalCount;
}

void CountAverager_Destroy(CountAverager *pCounter)
{
	if (pCounter)
	{
		IntAverager_Destroy(pCounter->pAverager);

		free(pCounter);
	}
}

void CountAverager_Reset(CountAverager *pCounter)
{
	IntAverager_Reset(pCounter->pAverager);
	pCounter->iCountedTime = 0;
	pCounter->iCurCount = 0;
	pCounter->iTotalCount = 0;
}

typedef struct FloatMaxTrackerDataPoint
{
	float fVal;
	U32 iTime;
} FloatMaxTrackerDataPoint;

typedef struct FloatMaxTracker
{
	FloatMaxTrackerDataPoint *pDataPoints; // uses blockEArray
} FloatMaxTracker;


FloatMaxTracker *FloatMaxTracker_Create(void)
{
	FloatMaxTracker *pTracker = calloc(sizeof(FloatMaxTracker), 1);
	beaSetCapacityEx(&pTracker->pDataPoints, sizeof(FloatMaxTrackerDataPoint), NULL, 32, 0, NULL, NULL MEM_DBG_PARMS_INIT);
	return pTracker;
}

void FloatMaxTracker_AddDataPoint(FloatMaxTracker *pTracker, float fVal, U32 iTime)
{
	int i;

	if (beaSize(&pTracker->pDataPoints) == 0)
	{
		FloatMaxTrackerDataPoint *pPoint = beaPushEmpty(&pTracker->pDataPoints);
		pPoint->fVal = fVal;
		pPoint->iTime = iTime;
		return;
	}

	i = beaSize(&pTracker->pDataPoints) - 1;
	
	//new value less than all values currently in tracker, just push it on the end
	if (fVal < pTracker->pDataPoints[i].fVal)
	{
		FloatMaxTrackerDataPoint *pPoint = beaPushEmpty(&pTracker->pDataPoints);
		pPoint->fVal = fVal;
		pPoint->iTime = iTime;
		return;	
	}

	//new value greater than or equal than all values currently in tracker, easier as special case
	if (fVal >= pTracker->pDataPoints[0].fVal)
	{
		beaSetSize(&pTracker->pDataPoints, 1);
		pTracker->pDataPoints[0].fVal = fVal;
		pTracker->pDataPoints[0].iTime = iTime;
		return;
	}

	//now we know that we need to do some modification, so find out where
	while (i)
	{
		if (fVal < pTracker->pDataPoints[i-1].fVal)
		{
			beaSetSize(&pTracker->pDataPoints, i + 1);
			pTracker->pDataPoints[i].fVal = fVal;
			pTracker->pDataPoints[i].iTime = iTime;
			return;
		}
		i--;
	}

	//if we get here, then the logic is flawed somewhere
	assert(0);
}



float FloatMaxTracker_GetMax(FloatMaxTracker *pTracker, U32 iStartingTime)
{
	int iSize = beaSize(&pTracker->pDataPoints);
	int i;

	for (i=0; i < iSize; i++)
	{
		if (iStartingTime <= pTracker->pDataPoints[i].iTime)
		{
			return pTracker->pDataPoints[i].fVal;
		}
	}

	return 0.0f;
}





void FloatMaxTracker_Destroy(FloatMaxTracker **ppTracker)
{
	if (!ppTracker || !(*ppTracker))
	{
		return;
	}

	beaDestroy(&(*ppTracker)->pDataPoints);
	free(*ppTracker);
	*ppTracker = NULL;
}
