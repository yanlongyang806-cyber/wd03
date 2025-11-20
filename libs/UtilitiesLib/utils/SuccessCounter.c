#include "SuccessCounter.h"
#include "textparser.h"
#include "SuccessCounter_c_ast.h"
#include "memtrack.h"

AUTO_STRUCT;
typedef struct SuccessCounter
{
	int iRingSize;
	int iNumInRing;
	int iNumSucceededInRing;
	int iIndexOfNextInRing;
	U8 *pRing; NO_AST

	int iTotal;
	int iTotalSuccess;
	int iTotalFailure;
} SuccessCounter;


SuccessCounter *SuccessCounter_Create(int iRingSize)
{
	SuccessCounter *pRetVal = StructCreate(parse_SuccessCounter);
	if (iRingSize)
	{
		pRetVal->iRingSize = iRingSize;
		pRetVal->pRing = calloc(iRingSize / 8, 1);
	}

	return pRetVal;
}


void SuccessCounter_Destroy(SuccessCounter **ppCounter)
{
	if (!ppCounter || !*ppCounter)
	{
		return;
	}

	SAFE_FREE(((*ppCounter)->pRing));
	StructDestroy(parse_SuccessCounter, *ppCounter);
	*ppCounter = NULL;
}

void SuccessCounter_ItHappened(SuccessCounter *pCounter, bool bSucceeded)
{
	pCounter->iTotal++;
	if (bSucceeded)
	{
		pCounter->iTotalSuccess++;
	}
	else
	{
		pCounter->iTotalFailure++;
	}

	if (pCounter->iRingSize)
	{
		int iByte = pCounter->iIndexOfNextInRing / 8;
		int iBit = pCounter->iIndexOfNextInRing % 8;

		if (pCounter->iNumInRing == pCounter->iRingSize)
		{
			bool bLast = (pCounter->pRing[iByte] >> iBit) & 1;

			if (bLast != bSucceeded)
			{
				if (bSucceeded)
				{
					pCounter->iNumSucceededInRing++;
					pCounter->pRing[iByte] |= (1 << iBit);
				}
				else
				{
					pCounter->iNumInRing--;
					pCounter->pRing[iByte] &= ~(1 << iBit);
				}
			}
		}
		else
		{
			if (bSucceeded)
			{
				pCounter->iNumSucceededInRing++;
				pCounter->pRing[iByte] |= (1 << iBit);
			}

			pCounter->iNumInRing++;

		}

		pCounter->iIndexOfNextInRing = (pCounter->iIndexOfNextInRing + 1) % pCounter->iNumInRing;
	}
}
int SuccessCounter_GetSuccessCount(SuccessCounter *pCounter)
{
	return pCounter->iTotalSuccess;
}
int SuccessCounter_GetFailureCount(SuccessCounter *pCounter)
{
	return pCounter->iTotalFailure;
}
int SuccessCounter_GetTotalCount(SuccessCounter *pCounter)
{
	return pCounter->iTotal;
}
int SuccessCounter_GetSuccessPercentLastN(SuccessCounter *pCounter)
{
	if (pCounter->iNumInRing)
	{
		return pCounter->iNumSucceededInRing * 100 / pCounter->iNumInRing;
	}
	return 0;
}
int SuccessCounter_GetSuccessPercentTotal(SuccessCounter *pCounter)
{
	if (pCounter->iTotal)
	{
		return pCounter->iTotalSuccess * 100 / pCounter->iTotal;
	}

	return 0;
}

#include "SuccessCounter_c_ast.c"
