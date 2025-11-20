#pragma once
GCC_SYSTEM

// a "median tracker" takes in floats and then returns the min, max, and arbitrary percentiles. I'm implementing
//it to begin with as an earray, so inserting is fast, but before any number of queries it must do a sort. When it gets
//too big it does a sort and then merges together consecutive elements

AUTO_STRUCT;
typedef struct MedianTracker
{
	int iMaxSize;
	bool bSorted;
	float *pVals;
} MedianTracker;

MedianTracker *MedianTracker_Create(int iMaxValsToStore);
void MedianTracker_AddVal(MedianTracker *pTracker, float f);
float MedianTracker_GetMedian(MedianTracker *pTracker, float t); //0.0 means min, 1.0 means max, 0.5 means median, 0.9 means 90th percentile, etc.
void MedianTracker_Destroy(MedianTracker *pTracker);