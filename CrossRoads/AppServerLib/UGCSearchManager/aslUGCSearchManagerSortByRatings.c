#include "aslUGCSearchManagerSortByRatings.h"

#include "GlobalTypes.h"
#include "error.h"
#include "stashTable.h"
#include "timing_profiler.h"
#include "utils.h"

#define SORT_BY_RATINGS_BUCKETS 1024

typedef struct SortyByRatingsNode SortyByRatingsNode;

typedef struct SortyByRatingsNode
{
	GlobalType nodeType;
	void* pVal;
	U32 iKey;
	int iBucketNum;
	SortyByRatingsNode *pPrev;
	SortyByRatingsNode *pNext;
} SortyByRatingsNode;

typedef struct SortByRatingsBucket
{
	SortyByRatingsNode *pFirst;
} SortByRatingsBucket;

static SortByRatingsBucket sBuckets[SORT_BY_RATINGS_BUCKETS] = {0};
static StashTable sProjectNodes = NULL;
static StashTable sSeriesNodes = NULL;

void SortyByRatings_Init(void)
{
	sProjectNodes = stashTableCreateInt(64);
	sSeriesNodes = stashTableCreateInt(64);
}

static int SortByRatings_RatingToBucketNum(float fRating)
{
	// Normally, fRating is 0-1.  To get Featured content to always
	// appear in front, force it to bucket 0.
	//
	// If we ever support more than one thing in front, this code will need to be refactored.
	if( fRating <= 1 ) {
		int iRating = (1.0f - fRating) * SORT_BY_RATINGS_BUCKETS;
		return CLAMP(iRating, 1, SORT_BY_RATINGS_BUCKETS - 1);
	} else {
		return 0;
	}
}

static void SortByRatings_AddNode(SortByRatingsBucket *pBucket, SortyByRatingsNode *pNode)
{
	pNode->pNext = pBucket->pFirst;
	pNode->pPrev = NULL;
	if(pNode->pNext)
		pNode->pNext->pPrev = pNode;
	pBucket->pFirst = pNode;
}

static void SortByRatings_RemoveNode(SortByRatingsBucket *pBucket, SortyByRatingsNode *pNode)
{
	if(pNode->pNext)
		pNode->pNext->pPrev = pNode->pPrev;

	if(pNode->pPrev)
		pNode->pPrev->pNext = pNode->pNext;

	if(pBucket->pFirst == pNode)
		pBucket->pFirst = pNode->pNext;
}

void SortByRatings_AddOrUpdate(GlobalType nodeType, U32 iKey, void *pVal, float fRating)
{
	int iBucketNum = SortByRatings_RatingToBucketNum(fRating);
	SortyByRatingsNode *pNode = NULL;
	StashTable *nodeStash = NULL;

	switch(nodeType)
	{
		case GLOBALTYPE_UGCPROJECT: nodeStash = &sProjectNodes; break;
		case GLOBALTYPE_UGCPROJECTSERIES: nodeStash = &sSeriesNodes; break;
		default: FatalErrorf("Unexpected GlobalType: %s", GlobalTypeToName(nodeType));
	}

	PERFINFO_AUTO_START_FUNC();

	if(stashIntFindPointer(*nodeStash, iKey, &pNode))
	{
		if(pNode->iBucketNum == iBucketNum)
		{
			pNode->pVal = pVal;
			PERFINFO_AUTO_STOP();
			return;
		}
		else
		{
			SortByRatings_RemoveNode(&sBuckets[pNode->iBucketNum], pNode);
			pNode->pVal = pVal;
			pNode->iBucketNum = iBucketNum;
			SortByRatings_AddNode(&sBuckets[pNode->iBucketNum], pNode);
			PERFINFO_AUTO_STOP();
			return;
		}
	}
	else
	{
		pNode = calloc(1, sizeof(SortyByRatingsNode));
		pNode->nodeType = nodeType;
		pNode->iKey = iKey;
		pNode->pVal = pVal;
		pNode->iBucketNum = iBucketNum;

		stashIntAddPointer(*nodeStash, iKey, pNode, false);

		SortByRatings_AddNode(&sBuckets[pNode->iBucketNum], pNode);
	}

	PERFINFO_AUTO_STOP();
}

void SortByRatings_Remove(GlobalType nodeType, U32 iKey)
{
	SortyByRatingsNode *pNode = NULL;
	StashTable *nodeStash = NULL;

	switch(nodeType)
	{
		case GLOBALTYPE_UGCPROJECT: nodeStash = &sProjectNodes; break;
		case GLOBALTYPE_UGCPROJECTSERIES: nodeStash = &sSeriesNodes; break;
		default: FatalErrorf("Unexpected GlobalType: %s", GlobalTypeToName(nodeType));
	}

	PERFINFO_AUTO_START_FUNC();

	if(stashIntRemovePointer(*nodeStash, iKey, &pNode))
	{
		SortByRatings_RemoveNode(&sBuckets[pNode->iBucketNum], pNode);
		free(pNode);
	}

	PERFINFO_AUTO_STOP();
}

void SortByRatings_Iterate(float fMaxRating, float fMinRating, SortByRatingsIterationFunc pFunc, void *pUserData)
{
	int iMinBucketNum = SortByRatings_RatingToBucketNum(fMaxRating);
	int iMaxBucketNum = SortByRatings_RatingToBucketNum(fMinRating);
	int i;

	PERFINFO_AUTO_START_FUNC();

	if(iMinBucketNum > iMaxBucketNum)
		SWAP32(iMinBucketNum, iMaxBucketNum);

	for(i = iMinBucketNum; i <= iMaxBucketNum; i++)
	{
		SortyByRatingsNode *pNode = sBuckets[i].pFirst;
		while(pNode)
		{
			if(pFunc(pNode->nodeType, pNode->pVal, pNode->iKey, pUserData))
			{
				PERFINFO_AUTO_STOP();
				return;
			}
			pNode = pNode->pNext;
		}
	}
	PERFINFO_AUTO_STOP();
}
