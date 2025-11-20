#include "MedianTracker.h"
#include "MedianTracker_h_ast.h"



MedianTracker *MedianTracker_Create(int iMaxValsToStore)
{
	MedianTracker *pTracker = (MedianTracker*)calloc(sizeof(MedianTracker), 1);
	pTracker->iMaxSize = iMaxValsToStore;

	//maxVals must be even so that our compaction works
	if (pTracker->iMaxSize & 1)
	{
		pTracker->iMaxSize++;
	}
	assert(iMaxValsToStore > 0);

	return pTracker;

}

void MedianTracker_Destroy(MedianTracker *pTracker)
{
	if (!pTracker)
	{
		return;
	}

	eafDestroy(&pTracker->pVals);
	free(pTracker);
}

void MedianTracker_SortAndCompact(MedianTracker *pTracker)
{
	eafQSort(pTracker->pVals);
	pTracker->bSorted = true;
	if (eafSize(&pTracker->pVals) == pTracker->iMaxSize)
	{
		int iStartingSize = eafSize(&pTracker->pVals);
		int i;

		for (i=0; i < iStartingSize / 2; i++)
		{
			pTracker->pVals[i] = (pTracker->pVals[i*2] + pTracker->pVals[i*2+1]) / 2.0f;
		}

		eafSetSize(&pTracker->pVals, iStartingSize / 2);
	}

}
void MedianTracker_AddVal(MedianTracker *pTracker, float f)
{
	eafPush(&pTracker->pVals, f);
	pTracker->bSorted = false;
	if (eafSize(&pTracker->pVals) == pTracker->iMaxSize)
	{
		MedianTracker_SortAndCompact(pTracker);
	}
}

float MedianTracker_GetMedian(MedianTracker *pTracker, float t)
{
	int iCurSize;	
	int iLowerIndex;
	float fLowerIndex;
	float fRatio;
	float fTemp;

	if (!pTracker)
	{
		return 0.0f;
	}

	if (!pTracker->bSorted)
	{
		MedianTracker_SortAndCompact(pTracker);
	}

	iCurSize = eafSize(&pTracker->pVals);
	if (iCurSize == 0)
	{
		return 0.0f;
	}
	else if (iCurSize == 1)
	{
		return pTracker->pVals[0];
	}
	else if (t <= 0.0f)
	{
		return pTracker->pVals[0];
	}
	else if (t >= 1.0f)
	{
		return pTracker->pVals[iCurSize - 1];
	}

	fTemp = t * (iCurSize - 1);
	iLowerIndex = fTemp;
	fLowerIndex = iLowerIndex;
	fRatio = fTemp - fLowerIndex;

	return pTracker->pVals[iLowerIndex] * (1.0f - fRatio) + pTracker->pVals[iLowerIndex + 1] * fRatio;
}


/*
	
AUTO_RUN;
void MedianTrackerTest(void)
{
	MedianTracker *pTracker = MedianTracker_Create(1024);

	int i;

	for (i=0; i < 10000; i++)
	{
		MedianTracker_AddVal(pTracker, randomF32() * 100);
	}

	printf("0.0 %f 0.1 %f 0.5 %f 0.9 %f 1.0 %f\n",
		MedianTracker_GetMedian(pTracker, 0.0f),
		MedianTracker_GetMedian(pTracker, 0.1f),
		MedianTracker_GetMedian(pTracker, 0.5f),
		MedianTracker_GetMedian(pTracker, 0.9f),
		MedianTracker_GetMedian(pTracker, 1.0f));

}*/
#include "MedianTracker_h_ast.c"
