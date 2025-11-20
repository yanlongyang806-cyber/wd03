#include "EventCountingHeatMap.h"
#include "jpeg.h"
#include "rand.h"
#include "Error.h"
#include "EventCountingHeatMap_h_ast.h"
#include "PixelCircle.h"
#include "writePng.h"
#include "winUtil.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_EngineMisc););


#define MIN_JPEG_WIDTH 300

typedef struct JpegPixel
{
	U8 r;
	U8 g;
	U8 b;
	U8 a; 
} JpegPixel;


typedef struct EventCounterInternal
{
	int iCurTotal;
	int iNumSubTotals;
	int iSecsPerSubTotal;
	U32 iNextTimeToSubmitToBiggerGuys;
	U32 iMostRecentSubTotalTime;
	int iNextSubTotalIndex;
	int *pSubTotals;
	struct EventCounterInternal **ppBiggerGuys;
} EventCounterInternal;

EventCounterInternal *ECI_Create(int iSecsPerSubTotal, int iNumSubTotals, U32 iCurTime)
{
	EventCounterInternal *pECI = calloc(sizeof(EventCounterInternal), 1);
	pECI->iNumSubTotals = iNumSubTotals;
	pECI->iSecsPerSubTotal = iSecsPerSubTotal;
	pECI->iNextTimeToSubmitToBiggerGuys = iCurTime + pECI->iNumSubTotals * pECI->iSecsPerSubTotal;
	pECI->iMostRecentSubTotalTime = iCurTime;
	pECI->pSubTotals = calloc(sizeof(int) * pECI->iNumSubTotals, 1);

	return pECI;
}

void ECI_Destroy(EventCounterInternal *pECI)
{
	eaDestroy(&pECI->ppBiggerGuys);
	free(pECI->pSubTotals);
	free(pECI);
}

void ECI_AddBiggerGuy(EventCounterInternal *pECI, EventCounterInternal *pBiggerGuy)
{
	eaPush(&pECI->ppBiggerGuys, pBiggerGuy);
}

void ECI_AddSubTotal(EventCounterInternal *pECI, int iAmount, U32 iCurTime)
{
	assert(iCurTime == pECI->iMostRecentSubTotalTime + pECI->iSecsPerSubTotal);
	pECI->iCurTotal -= pECI->pSubTotals[pECI->iNextSubTotalIndex];
	pECI->pSubTotals[pECI->iNextSubTotalIndex] = iAmount;
	pECI->iCurTotal += iAmount;
	pECI->iMostRecentSubTotalTime = iCurTime;

	if (pECI->iMostRecentSubTotalTime == pECI->iNextTimeToSubmitToBiggerGuys)
	{
		int i;

		for (i=0; i < eaSize(&pECI->ppBiggerGuys); i++)
		{
			ECI_AddSubTotal(pECI->ppBiggerGuys[i], pECI->iCurTotal, iCurTime);
		}

		pECI->iNextTimeToSubmitToBiggerGuys += pECI->iNumSubTotals * pECI->iSecsPerSubTotal;
	}

	pECI->iNextSubTotalIndex++;
	pECI->iNextSubTotalIndex %= pECI->iNumSubTotals;
}


struct EventCounter
{
	U32 iCurMinuteStartTime;
	int iCurMinuteTotal;
	int iLastMinuteTotal;
	U32 iTotalTotal;

	EventCounterInternal *p15Minutes;
	EventCounterInternal *pHour;
	EventCounterInternal *p6Hours;
	EventCounterInternal *pDay;
	EventCounterInternal *pWeek;
};


EventCounter *EventCounter_Create(U32 iTime)
{
	EventCounter *pEventCounter = calloc(sizeof(EventCounter), 1);

	pEventCounter->iCurMinuteStartTime = iTime;
	pEventCounter->p15Minutes = ECI_Create(60, 15, iTime);
	pEventCounter->pHour = ECI_Create(60, 60, iTime);
	pEventCounter->p6Hours = ECI_Create(15 * 60, 24, iTime);
	pEventCounter->pDay = ECI_Create(60 * 60, 24, iTime);
	pEventCounter->pWeek = ECI_Create(6 * 60 * 60, 28, iTime);

	ECI_AddBiggerGuy(pEventCounter->p15Minutes, pEventCounter->p6Hours);
	ECI_AddBiggerGuy(pEventCounter->pHour, pEventCounter->pDay);
	ECI_AddBiggerGuy(pEventCounter->p6Hours, pEventCounter->pWeek);

	return pEventCounter;
}

int EventCounter_GetUpdateFrequencyFromCountType(enumEventCountType eCountType)
{
	switch (eCountType)
	{
	case EVENTCOUNT_CURRENTMINUTE:
	case EVENTCOUNT_LASTFULLMINUTE:
		return 1;
	case EVENTCOUNT_LASTFULL15MINUTES:
	case EVENTCOUNT_LASTFULLHOUR:
		return 60;
	case EVENTCOUNT_LASTFULL6HOURS:
		return 15 * 60;
	case EVENTCOUNT_LASTFULLDAY:
		return 60 * 60;
	case EVENTCOUNT_LASTFULLWEEK:
		return 6 * 60 * 60;
	default:
		assert(0);
	}
}




void EventCounter_Destroy(EventCounter *pEventCounter)
{
	ECI_Destroy(pEventCounter->p15Minutes);
	ECI_Destroy(pEventCounter->pHour);
	ECI_Destroy(pEventCounter->p6Hours);
	ECI_Destroy(pEventCounter->pDay);
	ECI_Destroy(pEventCounter->pWeek);
	free(pEventCounter);
}

void EventCounter_BringUpToDate(EventCounter *pEventCounter, U32 iTime)
{
	while (iTime >= pEventCounter->iCurMinuteStartTime + 60)
	{
		pEventCounter->iCurMinuteStartTime += 60;
		ECI_AddSubTotal(pEventCounter->p15Minutes, pEventCounter->iCurMinuteTotal, pEventCounter->iCurMinuteStartTime);
		ECI_AddSubTotal(pEventCounter->pHour, pEventCounter->iCurMinuteTotal, pEventCounter->iCurMinuteStartTime);
		pEventCounter->iLastMinuteTotal = pEventCounter->iCurMinuteTotal;
		pEventCounter->iCurMinuteTotal = 0;
	}
}


void EventCounter_ItHappened(EventCounter *pEventCounter, U32 iTime)
{
	if (iTime < pEventCounter->iCurMinuteStartTime)
	{
		//Errorf("Out of order events received by event counter");
		return;
	}
	EventCounter_BringUpToDate(pEventCounter, iTime);
	pEventCounter->iCurMinuteTotal++;
	pEventCounter->iTotalTotal++;
}

U32 EventCounter_GetTotalTotal(EventCounter *pEventCounter)
{
	return pEventCounter->iTotalTotal;
}

int EventCounter_GetCount(EventCounter *pEventCounter, enumEventCountType eCountType, U32 iTime)
{
	EventCounter_BringUpToDate(pEventCounter, iTime);

	switch (eCountType)
	{
	case EVENTCOUNT_CURRENTMINUTE:
		return pEventCounter->iCurMinuteTotal;
	case EVENTCOUNT_LASTFULLMINUTE:
		return pEventCounter->iLastMinuteTotal;
	case EVENTCOUNT_LASTFULL15MINUTES:
		return pEventCounter->p15Minutes->iCurTotal;
	case EVENTCOUNT_LASTFULLHOUR:
		return pEventCounter->pHour->iCurTotal;
	case EVENTCOUNT_LASTFULL6HOURS:
		return pEventCounter->p6Hours->iCurTotal;
	case EVENTCOUNT_LASTFULLDAY:
		return pEventCounter->pDay->iCurTotal;
	case EVENTCOUNT_LASTFULLWEEK:
		return pEventCounter->pWeek->iCurTotal;
	}


		assert(0);
		return 0;
}


/*
AUTO_RUN_LATE;
void EventCounterTest(void)
{
	U32 iTime;
	int j;
	EventCounter *pCounter = EventCounter_Create(20);

	for (iTime = 21; iTime < 100000; iTime++)
	{
		for (j = randInt(5); j >= 0; j--)
		{
			EventCounter_ItHappened(pCounter, iTime);
		}
	}

	{
		int iBrk = 0;
	}
}
*/


typedef union HeatMapSquare
{
	EventCounter *pEventCounter;
	U16 doubleBufferCounts[2];
	U32 singleBufferCount;
	U8 categoryCounts[MAX_HEATMAP_CATEGORIES];
} HeatMapSquare;

struct EventCountingHeatMap 
{
	char *pName;
	HeatMapType eType;

	int iMinX;
	int iMinZ;
	int iGridSize; //each pixel in the EventCountingHeatMap is an iGridSize by iGridSize square
	int iNumXPixels;
	int iNumZPixels;



	U32 iMostRecentEventTime;

	HeatMapSquare *pHeatMapSquares; //NOT an earray, just an malloced of countaveragers


	U8 *pBitmapData;
	int iBitmapByteDepth;

	
	U32 iCurActiveBuffer : 1;

	char categoryNames[MAX_HEATMAP_CATEGORIES][64];
};
	
EventCountingHeatMap *ECHeatMap_Create(char *pName, HeatMapType eType, int iMinX, int iMaxX, int iMinZ, int iMaxZ, int iGridSize, U32 iTime)
{
	EventCountingHeatMap *pHeatMap = calloc(sizeof(EventCountingHeatMap), 1);
	pHeatMap->pName = strdup(pName);
	pHeatMap->iMinX = iMinX;
	pHeatMap->iMinZ = iMinZ;
	pHeatMap->iGridSize = iGridSize;
	pHeatMap->iNumXPixels = (iMaxX - iMinX) / iGridSize;
	pHeatMap->iNumZPixels = (iMaxZ - iMinZ) / iGridSize;

	if (pHeatMap->iNumXPixels < 1)
	{
		pHeatMap->iNumXPixels = 1;
	}

	if (pHeatMap->iNumZPixels < 1)
	{
		pHeatMap->iNumZPixels = 1;
	}


	pHeatMap->pHeatMapSquares = calloc(pHeatMap->iNumXPixels * pHeatMap->iNumZPixels * sizeof(HeatMapSquare), 1);

	pHeatMap->eType = eType;


	return pHeatMap;
}

void ECHeatMap_ResetCategoryNames(EventCountingHeatMap *pHeatMap)
{
	memset(pHeatMap->categoryNames, 0, sizeof(pHeatMap->categoryNames));
}

void ECHeatMap_SetCategoryName(EventCountingHeatMap *pHeatMap, int iCatNum, const char *pName)
{
	if (iCatNum >= 0 && iCatNum < MAX_HEATMAP_CATEGORIES)
	{
		sprintf(pHeatMap->categoryNames[iCatNum], "%s", pName);
	}
}


EventCountingHeatMap *ECHeatMap_CreateFromBitmap(char *pName, HeatMapType eType, char *pBitmapData, int iXSize, int iZSize, int iByteDepth, U32 iTime)
{
	EventCountingHeatMap *pHeatMap = ECHeatMap_Create(pName, eType, 0, iXSize, 0, iZSize, 1, iTime);
	pHeatMap->pBitmapData = pBitmapData;
	pHeatMap->iBitmapByteDepth = iByteDepth;
	

	return pHeatMap;

}





void ECHeatMap_Destroy(EventCountingHeatMap *pHeatMap)
{
	int i;

	if (pHeatMap->eType == HEATMAP_TIMEDIVISIONS)
	{
		for (i=0; i < pHeatMap->iNumXPixels * pHeatMap->iNumZPixels; i++)
		{
			if (pHeatMap->pHeatMapSquares[i].pEventCounter)
			{
				EventCounter_Destroy(pHeatMap->pHeatMapSquares[i].pEventCounter);
			}
		}
	}
	free(pHeatMap->pHeatMapSquares);
	free(pHeatMap->pName);
	free(pHeatMap);
}

U32 EventCounter_GetMostRecentEventTime(EventCountingHeatMap *pHeatMap)
{
	return pHeatMap->iMostRecentEventTime;
}


void ECHeatMap_AddPointInternal(EventCountingHeatMap *pHeatMap, int x, int z, int iTime, int iCategory)
{
	switch (pHeatMap->eType)
	{
	xcase HEATMAP_TIMEDIVISIONS:
		if (!pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].pEventCounter)
		{
			pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].pEventCounter = EventCounter_Create(iTime);
		}

		EventCounter_ItHappened(pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].pEventCounter, iTime);
	xcase HEATMAP_DOUBLEBUFFERED:
		{
			U16 *pCurVal = &pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].doubleBufferCounts[pHeatMap->iCurActiveBuffer];

			if (*pCurVal < 0xffff)
			{
				(*pCurVal)++;
			}
		}
	xcase HEATMAP_SINGLEBUFFERED:
		{
			U32 *pCurVal = &pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].singleBufferCount;

			if (*pCurVal < 0xffffffff)
			{
				(*pCurVal)++;
			}
		}
	xcase HEATMAP_CATEGORIES:
		{
			U8 *pCurVal;

			if (iCategory < 0 || iCategory >= MAX_HEATMAP_CATEGORIES)
			{
				return;
			}

			pCurVal = &pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].categoryCounts[iCategory];

			if (*pCurVal < 0xff)
			{
				(*pCurVal)++;
			}
		}


	}
}

void ECHeatMap_AddPoint(EventCountingHeatMap *pHeatMap, int iX, int iZ, int iRadius, U32 iTime, int iCategory)
{
	int iGridX, iGridZ;

	if (iCategory < 0 || iCategory >= MAX_HEATMAP_CATEGORIES)
	{
		return;
	}

	if (iTime < pHeatMap->iMostRecentEventTime)
	{
		//Errorf("Heat map received events out of order");
		return;
	}

	pHeatMap->iMostRecentEventTime = iTime;

	iGridX = (iX - pHeatMap->iMinX) / pHeatMap->iGridSize;
	if (iGridX < 0)
	{
		iGridX = 0;
	}
	if (iGridX >= pHeatMap->iNumXPixels)
	{
		iGridX = pHeatMap->iNumXPixels - 1;
	}

	iGridZ = (iZ - pHeatMap->iMinZ) / pHeatMap->iGridSize;
	if (iGridZ < 0)
	{
		iGridZ = 0;
	}
	if (iGridZ >= pHeatMap->iNumZPixels)
	{
		iGridZ = pHeatMap->iNumZPixels - 1;
	}



	if (iRadius)
	{
		int i;
		int iNewX;
		int iNewZ;
		PixelCircle *pCircle = pFindCircleCoordsForRadius(iRadius);

		for (i=0; i < eaSize(&pCircle->ppCoords); i++)
		{
			iNewX = iGridX + pCircle->ppCoords[i]->x;
			iNewZ = iGridZ + pCircle->ppCoords[i]->y;

			if (iNewX >= 0 && iNewX < pHeatMap->iNumXPixels && iNewZ >= 0 && iNewZ < pHeatMap->iNumZPixels)
			{
				ECHeatMap_AddPointInternal(pHeatMap, iNewX, iNewZ, iTime, iCategory);			
			}
		}
	}
	else
	{
		ECHeatMap_AddPointInternal(pHeatMap, iGridX, iGridZ, iTime, iCategory);
	}
}

typedef struct ValueColorPair
{
	float fValue;
	JpegPixel color;
} ValueColorPair;

static ValueColorPair sValueColorPairs[] = 
{
	{
		0.0f,
		{ 0,50,0,255 },
	},
	{
		0.0f,
		{ 0,128,0,255 },
	},
	{
		0.25f,
		{ 0, 240, 0, 255 },
	},
	{
		0.5f,
		{ 0, 0, 240, 255 },
	},
	{
		0.75f,
		{ 240, 0, 0, 255 },
	},
	{
		1.0f,
		{ 255, 255, 255, 255 },
	},
};

void InterpColor(JpegPixel *pOut, JpegPixel *pIn1, JpegPixel *pIn2, float f)
{
	pOut->a = interpU8(f, pIn1->a, pIn2->a);
	pOut->r = interpU8(f, pIn1->r, pIn2->r);
	pOut->g = interpU8(f, pIn1->g, pIn2->g);
	pOut->b = interpU8(f, pIn1->b, pIn2->b);
}

int ECHeatMap_GetValue(EventCountingHeatMap *pHeatMap, int x, int z, enumEventCountType eCountType, U32 iTime)
{
	switch (pHeatMap->eType)
	{
	xcase HEATMAP_TIMEDIVISIONS:
		return pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].pEventCounter ? EventCounter_GetCount(pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].pEventCounter, eCountType, iTime) : 0;
	xcase HEATMAP_DOUBLEBUFFERED:
		return pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].doubleBufferCounts[(((int)pHeatMap->iCurActiveBuffer) ^ 1) & 1];
	xcase HEATMAP_SINGLEBUFFERED:
		return pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].singleBufferCount;
	}

	assert(0);
	return 0;

}

//returns -1 if there is no data, the category number (0-3) of the category with 
//the most data if it has at least twice as much as anything else, otherwise 4
int ECHeatMap_GetCategory(EventCountingHeatMap *pHeatMap, int x, int z)
{
	U8 *pValues = pHeatMap->pHeatMapSquares[z * pHeatMap->iNumXPixels + x].categoryCounts;

	int iMaxVal = -1;
	int iSecondMax = -1;
	int iMaxCatNum = 0;
	int iSecondCatNum = 0;
	int i;

	for (i=0; i < MAX_HEATMAP_CATEGORIES; i++)
	{
		if (pValues[i] > iMaxVal)
		{
			iSecondMax = iMaxVal;
			iSecondCatNum = iMaxCatNum;

			iMaxVal = pValues[i];
			iMaxCatNum = i;
		}
		else if (pValues[i] > iSecondMax)
		{
			iSecondMax = pValues[i];
			iSecondCatNum = i;
		}
	}

	if (iMaxVal == 0)
	{
		return -1;
	}

	if (iSecondMax * 2 < iMaxVal)
	{
		return iMaxCatNum;
	}

	return MAX_HEATMAP_CATEGORIES;
}

static JpegPixel sBlackPixel = { 0, 0, 0, 255};

static JpegPixel sCategoryColorPixels[5] = 
{
	{ 64, 64, 255, 255 },
	{ 64, 255, 64, 255 },
	{ 220, 220, 64, 255 },
	{ 255, 64, 255, 255 },
	{ 255, 255, 255, 255 },
};

void ECHeatMap_FillInJpegPixel(EventCountingHeatMap *pHeatMap, int x, int z, float fMax, enumEventCountType eCountType, U32 iTime, JpegPixel *pPixel,
	HeatMapColorLevels *pColorLevels, int iLogScale)
{
	int iValue;
	float fValue;
	int i;
	int iArraySize;

	if (pHeatMap->eType == HEATMAP_CATEGORIES)
	{
		int iCategoryNum = ECHeatMap_GetCategory(pHeatMap, x, z);


		//MAX_HEATMAP_CATEGORIES is valid here.. it means "multiple categories" 
		if (iCategoryNum >= 0 && iCategoryNum <= MAX_HEATMAP_CATEGORIES)
		{

			memcpy(pPixel, &sCategoryColorPixels[iCategoryNum], sizeof(JpegPixel));
			return;
		}
		
		//if we end up here, we have no category data for that pixel, so we set our iValue to 0 and fall out into the normal
		//non-category code
		iValue = 0;

	}
	else
	{
		iValue = ECHeatMap_GetValue(pHeatMap, x, z, eCountType, iTime);
	}

	fValue = (iLogScale ? log((float)iValue) : (float)iValue) / fMax;
	iArraySize = ARRAY_SIZE(sValueColorPairs);

	

	if (iValue == 0 && pHeatMap->pBitmapData)
	{
		U8 *pSourceData = pHeatMap->pBitmapData + pHeatMap->iBitmapByteDepth * ((pHeatMap->iNumZPixels - 1 - z) * pHeatMap->iNumXPixels + x);
		pPixel->r = pSourceData[2];
		pPixel->g = pSourceData[1];
		pPixel->b = pSourceData[0];
		pPixel->a = 255;
		return;
	}

	if (pColorLevels)
	{
		for (i=0; i < eaSize(&pColorLevels->ppLevels); i++)
		{
			if ((i == eaSize(&pColorLevels->ppLevels) - 1) 
				|| (iValue < pColorLevels->ppLevels[i+1]->iMinCount))
			{
				pPixel->r = pColorLevels->ppLevels[i]->r;
				pPixel->g = pColorLevels->ppLevels[i]->g;
				pPixel->b = pColorLevels->ppLevels[i]->b;
				pPixel->a = 255;

				return;
			}
		}

	}	

	if (fValue <= sValueColorPairs[0].fValue)
	{
		memcpy(pPixel, &sValueColorPairs[0].color, sizeof(JpegPixel));
		return;
	}

	//note that we skip 0 because nothing is between sValueColorPairs[0] and sValueColorPairs[1]
	for (i=1; i < iArraySize - 1; i++)
	{
		if (fValue >= sValueColorPairs[i].fValue && fValue <= sValueColorPairs[i+1].fValue)
		{
			InterpColor(pPixel, &sValueColorPairs[i].color, &sValueColorPairs[i+1].color, (fValue - sValueColorPairs[i].fValue) / (sValueColorPairs[i+1].fValue - sValueColorPairs[i].fValue));
			return;
		}
	}

	memcpy(pPixel, &sValueColorPairs[iArraySize-1].color, sizeof(JpegPixel));
}

static void WriteKeyPixel(JpegPixel *pPixel, int iCatNum, U8 iIntensity)
{
	InterpColor(pPixel, &sBlackPixel, &sCategoryColorPixels[iCatNum], ((float)iIntensity) / 255.0f);

}

static void PutKeyIntoBuffer(EventCountingHeatMap *pHeatMap, JpegPixel *pPixels, int iTargetXSize, int iTargetYSize)
{
#if !PLATFORM_CONSOLE

	int iKeyWidthSoFar = 0;

	U8 *pBuf;
	int iBufXSize;
	int iBufYSize;
	int iCatNum;

	if (pHeatMap->eType != HEATMAP_CATEGORIES)
	{
		return;
	}

	for (iCatNum=0; iCatNum < MAX_HEATMAP_CATEGORIES; iCatNum++)
	{
		if (pHeatMap->categoryNames[iCatNum][0])
		{
			int x, y;

			pBuf = stringToBuffer(pHeatMap->categoryNames[iCatNum], &iBufXSize, &iBufYSize);

			for (x=0; x < iBufXSize; x++)
			{
				for (y = 0; y < iBufYSize; y++)
				{
					if (x + iKeyWidthSoFar < iTargetXSize)
					{
						WriteKeyPixel(&pPixels[y * iTargetXSize + x + iKeyWidthSoFar], iCatNum, pBuf[y * iBufXSize + x]);
					}
				}
			}

			iKeyWidthSoFar += iBufXSize;
			free(pBuf);
		}
	}

#endif
}





void ECHeatMap_WriteJpegOrPng(EventCountingHeatMap *pHeatMap, char *pFileName, float fMaxValue, enumEventCountType eCountType, U32 iTime, HeatMapColorLevels *pColorLevels, int iLogScale)
{
	int x, z;
	JpegPixel *pPixels;
	char *pFileNameCopy = strdup(pFileName);
	int iCount = 0;

	pPixels = calloc(sizeof(JpegPixel) * pHeatMap->iNumXPixels * pHeatMap->iNumZPixels, 1);

	if (iTime == 0)
	{
		iTime = EventCounter_GetMostRecentEventTime(pHeatMap);
	}

	if (pHeatMap->eType != HEATMAP_CATEGORIES)
	{
		if (fMaxValue == 0)
		{
			for (x=0; x < pHeatMap->iNumXPixels; x++)
			{
				for (z=0; z < pHeatMap->iNumZPixels; z++)
				{
					int iCurValue = ECHeatMap_GetValue(pHeatMap, x, z, eCountType, iTime);
					if(iCurValue > 0)
					{
						float fCurValue = iLogScale ? log((float)iCurValue) : (float)iCurValue;
						++iCount;
						if (fCurValue > fMaxValue)
						{
							fMaxValue = fCurValue;
						}
					}
				}
			}
		}

		if (fMaxValue == 0)
		{
			fMaxValue = 100;
		}
	}

	for (x=0; x < pHeatMap->iNumXPixels; x++)
	{
		for (z=0; z < pHeatMap->iNumZPixels; z++)
		{
			ECHeatMap_FillInJpegPixel(pHeatMap, x, z, 
				fMaxValue, eCountType, iTime,
				&pPixels[(pHeatMap->iNumZPixels - z - 1) * pHeatMap->iNumXPixels + x], pColorLevels, iLogScale);
		}
	}

	//make this more sophisticated later?
	if (pHeatMap->iNumXPixels < MIN_JPEG_WIDTH)
	{
		int iZoomFactor = MIN_JPEG_WIDTH / pHeatMap->iNumXPixels + 1;
		JpegPixel *pNewPixels = calloc(sizeof(JpegPixel) * pHeatMap->iNumXPixels * pHeatMap->iNumZPixels * iZoomFactor * iZoomFactor, 1);

		for (x = 0; x < pHeatMap->iNumXPixels * iZoomFactor; x++)
		{
			for (z = 0; z < pHeatMap->iNumZPixels * iZoomFactor; z++)
			{
				memcpy(&pNewPixels[z * pHeatMap->iNumXPixels * iZoomFactor + x], &pPixels[(z / iZoomFactor) * pHeatMap->iNumXPixels + (x / iZoomFactor)], sizeof(JpegPixel));
			}
		}

		PutKeyIntoBuffer(pHeatMap, pNewPixels, pHeatMap->iNumXPixels * iZoomFactor, pHeatMap->iNumZPixels * iZoomFactor);

		if (strEndsWith(pFileNameCopy, ".jpg"))
		{
			jpgSave(pFileNameCopy, (U8*)pNewPixels, 4, pHeatMap->iNumXPixels * iZoomFactor, pHeatMap->iNumZPixels * iZoomFactor, 95);
		}
		else
		{
			WritePNG_File(pNewPixels, pHeatMap->iNumXPixels * iZoomFactor, pHeatMap->iNumZPixels * iZoomFactor, pHeatMap->iNumXPixels * iZoomFactor, 4, pFileNameCopy);

		}
		free(pNewPixels);
	}
	else
	{

		PutKeyIntoBuffer(pHeatMap, pPixels, pHeatMap->iNumXPixels, pHeatMap->iNumZPixels);

		if (strEndsWith(pFileNameCopy, ".jpg"))
		{
			jpgSave(pFileNameCopy, (U8*)pPixels, 4, pHeatMap->iNumXPixels, pHeatMap->iNumZPixels, 95);
		}
		else
		{
			WritePNG_File(pPixels, pHeatMap->iNumXPixels, pHeatMap->iNumZPixels, pHeatMap->iNumXPixels, 4, pFileNameCopy);
		}
	}

	free(pPixels);
	free(pFileNameCopy);
}

/*
AUTO_RUN_LATE;
void heatMapTest(void)
{
	int x, z;

	EventCountingHeatMap *pHeatMap = ECHeatMap_Create("test", 0, 200, 0, 100, 1, 10);


	for (x = 0; x < 200; x++)
	{
		for (z = 0; z < 100; z++)
		{
			int iNumEvents = (int)(((float)x / 10.0f + (float)z /10.0f) * (randomMersennePositiveF32() + randomMersennePositiveF32() + randomMersennePositiveF32()));
			int i;

			for (i=0; i < iNumEvents; i++)
			{
				ECHeatMap_AddPoint(pHeatMap, x, z, 11);
			}
		
		}
	}

	ECHeatMap_WriteJpeg(pHeatMap, "c:\\temp\\test.jpg", 40, EVENTCOUNT_CURRENTMINUTE, 12);
	ECHeatMap_Destroy(pHeatMap);
}
*/

void ECHeatMap_DoubleBuffered_Reset(EventCountingHeatMap *pHeatMap)
{
	int i;
	int iNumPixels = pHeatMap->iNumXPixels * pHeatMap->iNumZPixels;

	assertmsg(pHeatMap->eType == HEATMAP_DOUBLEBUFFERED, "Only double buffered heat maps can be reset");
	pHeatMap->iCurActiveBuffer ^= 1;

	for (i=0; i < iNumPixels; i++)
	{
		pHeatMap->pHeatMapSquares[i].doubleBufferCounts[pHeatMap->iCurActiveBuffer & 1] = 0;
	}
}

#include "EventCountingHeatMap_h_ast.c"
