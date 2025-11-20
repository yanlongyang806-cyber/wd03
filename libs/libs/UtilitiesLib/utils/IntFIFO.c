#include "IntFIFO.h"
#include "floatAverager.h"
#include "timing.h"

typedef struct IntFIFO
{
	U32 *pInts;
	int iCurCapacity;
	int iCurCount;
	int iNextGetIndex;
} IntFIFO;

IntFIFO *IntFIFO_Create(int iInitialCapacity)
{
	IntFIFO *pRetVal = calloc(sizeof(IntFIFO), 1);
	pRetVal->iCurCapacity = MIN(iInitialCapacity, 4);
	pRetVal->pInts = malloc(sizeof(U32) * pRetVal->iCurCapacity);

	return pRetVal;
}
void IntFIFO_Push(IntFIFO *pBuf, U32 iInt)
{
	int iNextPushIndex;

	if (pBuf->iCurCount == pBuf->iCurCapacity)
	{
		int iNewCapacity = pBuf->iCurCapacity * 2;
		int iCopySize1 = 0;
		int iCopySize2 = 0;
		U32 *pNewBuf = malloc(sizeof(U32) * iNewCapacity);

		iCopySize1 = pBuf->iCurCapacity - pBuf->iNextGetIndex;
		if (iCopySize1 > pBuf->iCurCount)
		{
			iCopySize1 = pBuf->iCurCount;
		}
		else
		{
			iCopySize2 = pBuf->iCurCount - iCopySize1;
		}

		if (iCopySize1)
		{
			memcpy(pNewBuf, pBuf->pInts + pBuf->iNextGetIndex, iCopySize1 * sizeof(U32));

			if (iCopySize2)
			{
				memcpy(pNewBuf + iCopySize1, pBuf->pInts, iCopySize2 * sizeof(U32));
			}
		}

		free(pBuf->pInts);
		pBuf->pInts = pNewBuf;
		pBuf->iNextGetIndex = 0;
		pBuf->iCurCapacity = iNewCapacity;
	}

	iNextPushIndex = (pBuf->iNextGetIndex + pBuf->iCurCount) % pBuf->iCurCapacity;
	pBuf->pInts[iNextPushIndex] = iInt;
	pBuf->iCurCount++;
}

bool IntFIFO_Get(IntFIFO *pBuf, U32 *pOut)
{
	if (pBuf->iCurCount)
	{
		*pOut = pBuf->pInts[pBuf->iNextGetIndex];
		pBuf->iNextGetIndex = (pBuf->iNextGetIndex + 1) % pBuf->iCurCapacity;
		pBuf->iCurCount--;
		return true;
	}

	return false;
}

void IntFIFO_Clear(IntFIFO *pBuf)
{
	pBuf->iCurCount = 0;
}
void IntFIFO_Destroy(IntFIFO **ppBuf)
{
	if (!ppBuf || !*ppBuf)
	{
		return;
	}

	free((*ppBuf)->pInts);
	free(*ppBuf);
	*ppBuf = NULL;
}


void IntFIFO_FindAndRemove(IntFIFO *pBuf, U32 iInt)
{
	int iStartingCount = pBuf->iCurCount;
	int i;

	for (i=0; i < iStartingCount; i++)
	{
		U32 iCur;
		IntFIFO_Get(pBuf, &iCur);

		if (iCur != iInt)
		{
			IntFIFO_Push(pBuf, iCur);
		}
	}
}




typedef struct PointerFIFO
{
	void **ppPtrs;
	int iCurCapacity;
	int iCurCount;
	int iNextGetIndex;
	FloatMaxTracker *pMaxTracker;
} PointerFIFO;

PointerFIFO *PointerFIFO_Create(int iInitialCapacity)
{
	PointerFIFO *pRetVal = calloc(sizeof(PointerFIFO), 1);
	pRetVal->iCurCapacity = MIN(iInitialCapacity, 4);
	pRetVal->ppPtrs = malloc(sizeof(void*) * pRetVal->iCurCapacity);

	return pRetVal;
}
void PointerFIFO_Push(PointerFIFO *pBuf, void *pPtr)
{
	int iNextPushIndex;

	if (pBuf->iCurCount == pBuf->iCurCapacity)
	{
		int iNewCapacity = pBuf->iCurCapacity * 2;
		int iCopySize1 = 0;
		int iCopySize2 = 0;
		void **pNewBuf = malloc(sizeof(void*) * iNewCapacity);

		iCopySize1 = pBuf->iCurCapacity - pBuf->iNextGetIndex;
		if (iCopySize1 > pBuf->iCurCount)
		{
			iCopySize1 = pBuf->iCurCount;
		}
		else
		{
			iCopySize2 = pBuf->iCurCount - iCopySize1;
		}

		if (iCopySize1)
		{
			memcpy(pNewBuf, pBuf->ppPtrs + pBuf->iNextGetIndex, iCopySize1 * sizeof(void*));

			if (iCopySize2)
			{
				memcpy(pNewBuf + iCopySize1, pBuf->ppPtrs, iCopySize2 * sizeof(void*));
			}
		}

		free(pBuf->ppPtrs);
		pBuf->ppPtrs = pNewBuf;
		pBuf->iNextGetIndex = 0;
		pBuf->iCurCapacity = iNewCapacity;
	}

	iNextPushIndex = (pBuf->iNextGetIndex + pBuf->iCurCount) % pBuf->iCurCapacity;
	pBuf->ppPtrs[iNextPushIndex] = pPtr;
	pBuf->iCurCount++;

	if (pBuf->pMaxTracker)
	{
		FloatMaxTracker_AddDataPoint(pBuf->pMaxTracker, pBuf->iCurCount, timeSecondsSince2000());
	}
}

bool PointerFIFO_Get(PointerFIFO *pBuf, void **ppOut)
{
	if (!pBuf)
	{
		return false;
	}

	if (pBuf->pMaxTracker)
	{
		FloatMaxTracker_AddDataPoint(pBuf->pMaxTracker, pBuf->iCurCount, timeSecondsSince2000());
	}

	if (pBuf->iCurCount)
	{
		*ppOut = pBuf->ppPtrs[pBuf->iNextGetIndex];
		pBuf->iNextGetIndex = (pBuf->iNextGetIndex + 1) % pBuf->iCurCapacity;
		pBuf->iCurCount--;
		return true;
	}

	return false;
}

bool PointerFIFO_Peek(PointerFIFO *pBuf, void **ppOut)
{
	if (!pBuf)
	{
		return false;
	}


	if (pBuf->iCurCount)
	{
		*ppOut = pBuf->ppPtrs[pBuf->iNextGetIndex];
		return true;
	}

	return false;
}

void PointerFIFO_Clear(PointerFIFO *pBuf)
{
	pBuf->iCurCount = 0;
}
void PointerFIFO_Destroy(PointerFIFO **ppBuf)
{
	if (!ppBuf || !*ppBuf)
	{
		return;
	}

	FloatMaxTracker_Destroy(&((*ppBuf)->pMaxTracker));

	free((*ppBuf)->ppPtrs);
	free(*ppBuf);
	*ppBuf = NULL;
}

int PointerFIFO_Count(PointerFIFO *pBuf)
{
	if (!pBuf)
	{
		return 0;
	}

	return pBuf->iCurCount;
}


void PointerFIFO_EnableMaxTracker(PointerFIFO *pBuf)
{
	if (!pBuf)
	{
		return;
	}

	if (!pBuf->pMaxTracker)
	{
		pBuf->pMaxTracker = FloatMaxTracker_Create();
	}

}

float PointerFIFO_GetMaxCount(PointerFIFO *pBuf, U32 iStartingTime)
{
	if (pBuf && pBuf->pMaxTracker)
	{
		FloatMaxTracker_AddDataPoint(pBuf->pMaxTracker, pBuf->iCurCount, timeSecondsSince2000());
		return FloatMaxTracker_GetMax(pBuf->pMaxTracker, iStartingTime);
	}

	return 0.0f;
}