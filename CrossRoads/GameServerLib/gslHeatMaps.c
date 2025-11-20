#include "gslHeatMaps.h"
#include "WorldLib.h"
#include "WorldGrid.h"
#include "estring.h"
#include "stashTable.h"
#include "jpeg.h"
#include "PixelCircle.h"
#include "entity.h"
#include "EntityIterator.h"
#include "httpJpegLibrary.h"
#include "HttpLib.h"
#include "error.h"

static StashTable sTableTypesByName = NULL;


typedef struct gslHeatMapCBHandle
{
	Vec3 min;
	Vec3 max;
	int iGameUnitsPerPixel;
	int iXPixels;
	int iZPixels;
	int iPenRadius;
	int *pGrid;
} gslHeatMapCBHandle;

typedef struct JpegPixel
{
	U8 r;
	U8 g;
	U8 b;
	U8 a;
} JpegPixel;

#define MIN_GREEN 80
#define MAX_GREEN 200

#define MIN_YELLOW 80
#define MAX_YELLOW 200

#define RED 180

void gslHeatMapPutColorIntoPixel(JpegPixel *pPixel, int iValue, int iYellowCutoff, int iRedCutoff)
{
	if (iValue == 0)
	{
		return;
	}
	else if (iValue < iYellowCutoff)
	{
		pPixel->g = interpU8((float)iValue / (float)iYellowCutoff, MIN_GREEN, MAX_GREEN);
	}
	else if (iValue < iRedCutoff)
	{
		pPixel->r = pPixel->g = interpU8((float)(iValue - iYellowCutoff) / (float)(iRedCutoff - iYellowCutoff), MIN_YELLOW, MAX_YELLOW);
	}
	else
	{
		pPixel->r = RED;
	}
}

bool gslHeatMapDoActualWriting(const char *pOutFileName, int *pData, int iXDataSize, int iZDataSize,
	int iYellowCutoff, int iRedCutoff, int iMinOutputPixelSize, char **ppErrorString)
{
	int iZoom = 1;
	JpegPixel *pOutPixels;
	int x, z;
	char *pDupFileName; //need to dup the filename so it can get forwardslashed

	if (iMinOutputPixelSize)
	{
		int iXZoom = iMinOutputPixelSize / iXDataSize;
		int iZZoom = iMinOutputPixelSize / iZDataSize;

		iZoom = MAX(iXZoom, iZZoom);
		if (iZoom < 1)
		{
			iZoom = 1;
		}

	}

	pOutPixels = calloc(sizeof(JpegPixel) * iXDataSize * iZDataSize * iZoom * iZoom, 1);

	for (x = 0; x < iXDataSize; x++)
	{
		for (z = 0; z < iZDataSize; z++)
		{
			int iData = pData[iXDataSize * z + x];

			if (iData)
			{
				gslHeatMapPutColorIntoPixel(&pOutPixels[(iXDataSize * iZoom) * z * iZoom + x * iZoom],
					pData[iXDataSize * z + x], iYellowCutoff, iRedCutoff);

				if (iZoom)
				{
					int iZoomX, iZoomZ;

					for (iZoomX = 0; iZoomX < iZoom; iZoomX++)
					{
						for (iZoomZ = 0; iZoomZ < iZoom; iZoomZ++)
						{
							if (iZoomX || iZoomZ)
							{
								memcpy(&pOutPixels[(iXDataSize * iZoom) * (z * iZoom + iZoomZ) + (x * iZoom + iZoomX)], &pOutPixels[(iXDataSize * iZoom) * z * iZoom + x * iZoom], sizeof(JpegPixel));
							}
						}
					}
				}
			}
		}
	}

	pDupFileName = strdup(pOutFileName);
	jpgSave(pDupFileName, (U8*)pOutPixels, 4, iXDataSize * iZoom, iZDataSize * iZoom, 95);

	free(pDupFileName);
	free(pOutPixels);

	return true;
}

bool gslWriteJpegHeatMap(const char *pOutFileName, const char *pTypeName, const char *pRegionName, 
						 int iGameUnitsPerPixel, int iPenRadius, int iYellowCutoff, int iRedCutoff, int iMinOutputPixelSize, 
						 char **ppErrorString)
{
	Vec3 min, max;
	gslHeatmapGetRegionBounds(min, max, pRegionName);

	return gslWriteJpegHeatMapEx(pOutFileName, pTypeName, min, max, iGameUnitsPerPixel, iPenRadius, 
									iYellowCutoff, iRedCutoff, iMinOutputPixelSize, ppErrorString);
}


bool gslWriteJpegHeatMapEx(const char *pOutFileName, const char *pTypeName, const Vec3 boundingMin, const Vec3 boundingMax, 
						 int iGameUnitsPerPixel, int iPenRadius, int iYellowCutoff, int iRedCutoff, int iMinOutputPixelSize, 
						 char **ppErrorString)
{
	gslHeatMapGatherDataCB *pCB = NULL;
	gslHeatMapCBHandle hHandle = {0};
	bool bRetVal;

	if (!sTableTypesByName || !stashFindPointer(sTableTypesByName, pTypeName, (void*)(&pCB)))
	{
		estrPrintf(ppErrorString, "Unrecognized gsl heatmap type %s", pTypeName);
		return false;
	}
	
	copyVec3(boundingMin, hHandle.min);
	copyVec3(boundingMax, hHandle.max);
		

	//special case for empty maps
	if (hHandle.min[0] >= hHandle.max[0])
	{
		hHandle.min[0] = -1000.0f;
		hHandle.min[1] = -1000.0f;
		hHandle.min[2] = -1000.0f;

		hHandle.max[0] = 1000.0f;
		hHandle.max[1] = 1000.0f;
		hHandle.max[2] = 1000.0f;
	}

	hHandle.iGameUnitsPerPixel = iGameUnitsPerPixel;
	hHandle.iXPixels = (hHandle.max[0] - hHandle.min[0]) / iGameUnitsPerPixel + 1;
	hHandle.iZPixels = (hHandle.max[2] - hHandle.min[2]) / iGameUnitsPerPixel + 1;

	hHandle.pGrid = calloc(sizeof(int) * hHandle.iXPixels * hHandle.iZPixels, 1);
	hHandle.iPenRadius = iPenRadius;

	bRetVal = pCB(&hHandle, ppErrorString);
	if (!bRetVal)
	{
		free(hHandle.pGrid);
		return false;
	}

	bRetVal = gslHeatMapDoActualWriting(pOutFileName, hHandle.pGrid, hHandle.iXPixels, hHandle.iZPixels, iYellowCutoff, iRedCutoff, iMinOutputPixelSize, ppErrorString);

	free(hHandle.pGrid);
	return bRetVal;
}


bool gslHeatMapIsPointIn(const gslHeatMapCBHandle *pHandle, const Vec3 vec)
{
	return pointBoxCollision(vec, pHandle->min, pHandle->max);
}

void gslHeatMapAddPoint(gslHeatMapCBHandle *pHandle, const Vec3 vec, int iAmount)
{
	int x, z;

	x = (vec[0] - pHandle->min[0]) / pHandle->iGameUnitsPerPixel;
	z = (vec[2] - pHandle->min[2]) / pHandle->iGameUnitsPerPixel;

	// Should we really clamp here? we possibly just want to throw out this point...
	if (x < 0)
	{
		x = 0;
	}
	else if (x >= pHandle->iXPixels)
	{
		x = pHandle->iXPixels - 1;
	}

	if (z < 0)
	{
		z = 0;
	}
	else if (z >= pHandle->iZPixels)
	{
		z = pHandle->iZPixels - 1;
	}

	// flip the Z coordinate since the bitmap is actually upside down in memory
	z = (pHandle->iZPixels-1) - z;

	if (pHandle->iPenRadius)
	{
		int i;
		PixelCircle *pCircle = pFindCircleCoordsForRadius(pHandle->iPenRadius);

		for (i=0 ; i < eaSize(&pCircle->ppCoords); i++)
		{
			int iInnerX = x + pCircle->ppCoords[i]->x;
			int iInnerZ = z + pCircle->ppCoords[i]->y;

			if (iInnerX >= 0 && iInnerX < pHandle->iXPixels && iInnerZ >= 0 && iInnerZ < pHandle->iZPixels)
			{
				pHandle->pGrid[pHandle->iXPixels * iInnerZ + iInnerX] += iAmount;
			}
		}
	}
	else
	{
		pHandle->pGrid[pHandle->iXPixels * z + x] += iAmount;
	}
}

void gslHeatMapAddLine(gslHeatMapCBHandle *pHandle, const Vec3 start, const Vec3 end, int iAmount)
{
	Vec3 dir;
	F32 len;

	subVec3(end, start, dir);
	len = normalVec3(dir);
		
	gslHeatMapAddLineEx(pHandle, start, dir, len, iAmount);
}

// note: this is not optimal as it is just calling gslHeatMapAddPoint over and over with world-coordinates
void gslHeatMapAddLineEx(gslHeatMapCBHandle *pHandle, const Vec3 start, const Vec3 dir, F32 len, int iAmount)
{
	Vec3 pos;
	F32 dist = 0.0f;

	// TODO: clip the line to the min/max bounds
	while(dist < len)
	{
		scaleAddVec3(dir, dist, start, pos);

		gslHeatMapAddPoint(pHandle, pos, iAmount);

		dist += 1.0f;
	}

	// 
	if (fmod(len, 1.0f) >= 0.5f)
	{
		scaleAddVec3(dir, len, start, pos);
		gslHeatMapAddPoint(pHandle, pos, iAmount);
	}
}


void gslHeatMapRegisterType(char *pTypeName, gslHeatMapGatherDataCB *pCB)
{
	if (!sTableTypesByName)
	{
		sTableTypesByName = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	}

	stashAddPointer(sTableTypesByName, pTypeName, pCB, true);
}

bool gslEntityDensityGatherData(gslHeatMapCBHandle *pHandle, char **ppErrorString)
{

	EntityIterator *pIter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IGNORE);
	Entity *pEnt;

	while(pEnt = EntityIteratorGetNext(pIter))
	{
		Vec3 pos;
		entGetPos(pEnt, pos);

		if (gslHeatMapIsPointIn(pHandle, pos))
		{
			gslHeatMapAddPoint(pHandle, pos, 1);
		}
	}

	EntityIteratorRelease(pIter);

	return true;
}


void gslHeatMapsJpegCB(char *pName, UrlArgumentList *pArgList, JpegLibrary_ReturnJpegCB *pCB, void *pUserData)
{
	char *pOutFileName = NULL; //estring
	char *pTypeName;
	char *pRegionName = NULL;
	int iGameUnitsPerPixel = 16;
	int iPenRadius = 3;
	int iYellowCutoff = 5;
	int iRedCutoff = 10;
	int iMinOutputPixelSize = 300;

	char *pFirstUnderscore;

	char *pErrorString = NULL;

	if (!strEndsWith(pName, ".jpg"))
	{
		pCB(NULL, 0, 0, "Bad jpeg syntax", pUserData);
		return;
	}

	//cut off extension
	pName[strlen(pName) - 4] = 0;
	pTypeName = pName;

	pFirstUnderscore = strchr(pName, '_');
	if (pFirstUnderscore)
	{
		*pFirstUnderscore = 0;
		pRegionName = pFirstUnderscore + 1;
	}

	if (urlFindBoundedInt(pArgList, "jpgGameUnitsPerPixel", &iGameUnitsPerPixel, 1, 1000000) == -1)
	{
		pCB(NULL, 0, 0, "Bad jpgGameUnitsPerPixel syntax", pUserData);
		return;
	}

	if (urlFindBoundedInt(pArgList, "jpgPenRadius", &iPenRadius, 0, 20) == -1)
	{
		pCB(NULL, 0, 0, "Bad jpgPenRadius syntax", pUserData);
		return;
	}

	if (urlFindBoundedInt(pArgList, "jpgYellowCutoff", &iYellowCutoff, 1, 1000000) == -1)
	{
		pCB(NULL, 0, 0, "Bad jpgYellowCutoff syntax", pUserData);
		return;
	}

	if (urlFindBoundedInt(pArgList, "jpgRedCutoff", &iRedCutoff, 1, 1000000) == -1)
	{
		pCB(NULL, 0, 0, "Bad jpgRedCutoff syntax", pUserData);
		return;
	}

	if (urlFindBoundedInt(pArgList, "jpgMinOutputSize", &iMinOutputPixelSize, 1, 2000) == -1)
	{
		pCB(NULL, 0, 0, "Bad jpgMinOutputSize syntax", pUserData);
		return;
	}

	loadstart_printf("Beginning heatmap jpeg...");


	estrPrintf(&pOutFileName, "%s/gslHeatmap_%s_%u.jpg", fileTempDir(), pTypeName, GetAppGlobalID());
	mkdirtree(pOutFileName);

	if (!gslWriteJpegHeatMap(pOutFileName, pTypeName, pRegionName, iGameUnitsPerPixel,
		 iPenRadius, iYellowCutoff, iRedCutoff, iMinOutputPixelSize, &pErrorString))
	{
		pCB(NULL, 0, 0, pErrorString, pUserData);
		estrDestroy(&pErrorString);
	}
	else
	{
		int iBufferSize;
		char *pBuffer = fileAlloc(pOutFileName, &iBufferSize);
		if (pBuffer)
		{
			pCB(pBuffer, iBufferSize, 1, NULL, pUserData);
			free(pBuffer);
		}
		else
		{
			pCB(NULL, 0, 0, "FileAlloc failed", pUserData);
		}
	}

	estrDestroy(&pOutFileName);

	loadend_printf("Done");

}

bool gslHeatmapGetRegionBounds(Vec3 min, Vec3 max, const char *pRegionName)
{
	WorldRegion *pWorldRegion = zmapGetWorldRegionByName(NULL, pRegionName);
	if (!pWorldRegion)
	{
		zeroVec3(min);
		zeroVec3(max);
		return false;
	}

	return worldRegionGetBounds(pWorldRegion, min, max);
}

AUTO_RUN;
void gslHeatMaps_Init(void)
{
	gslHeatMapRegisterType("EntityDensity", &gslEntityDensityGatherData);

	JpegLibrary_RegisterCB("HeatMap", &gslHeatMapsJpegCB);

}

AUTO_COMMAND;
void testHeatmap(void)
{
	char *pErrorString = NULL;

	gslWriteJpegHeatMap("c:\\temp\\heatmap.jpg", "EntityDensity", NULL, 32, 3, 4, 6, 300, &pErrorString);

	estrDestroy(&pErrorString);
}



