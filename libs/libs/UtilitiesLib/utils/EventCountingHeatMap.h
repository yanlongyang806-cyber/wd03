#pragma once
GCC_SYSTEM

#define MAX_HEATMAP_CATEGORIES 4

AUTO_ENUM;
typedef enum
{
	HEATMAP_TIMEDIVISIONS, //generates last 15 minutes, last hour, etc.


	HEATMAP_DOUBLEBUFFERED, //fills in a buffer, then can swap it and fill in a different one, etc.
			//jpegs are of the inactive buffer

	HEATMAP_SINGLEBUFFERED, //just fills in a buffer and returns jpeg of it

	HEATMAP_CATEGORIES,
		//up to 4 categories of data, color coded. (No intensity color at all, just colors for data points)
} HeatMapType;

//when writing a jpeg with a heat map, you can specify the colors
AUTO_STRUCT;
typedef struct HeatMapColorLevel
{
	int iMinCount; AST(STRUCTPARAM)
	U8 r; AST(STRUCTPARAM)
	U8 g; AST(STRUCTPARAM)
	U8 b; AST(STRUCTPARAM)
} HeatMapColorLevel;

AUTO_STRUCT;
typedef struct HeatMapColorLevels
{
	HeatMapColorLevel **ppLevels;
} HeatMapColorLevels;


AUTO_ENUM;
typedef enum enumEventCountType
{
	EVENTCOUNT_CURRENTMINUTE,
	EVENTCOUNT_LASTFULLMINUTE,
	EVENTCOUNT_LASTFULL15MINUTES, //minute resolution
	EVENTCOUNT_LASTFULLHOUR, //minute resolution
	EVENTCOUNT_LASTFULL6HOURS, //15 minute resolution
	EVENTCOUNT_LASTFULLDAY, //hour resolution
	EVENTCOUNT_LASTFULLWEEK, //6 hour resolution

	EVENTCOUNT_COUNT,
} enumEventCountType;

typedef struct EventCounter EventCounter;

EventCounter *EventCounter_Create(U32 iTime);
void EventCounter_ItHappened(EventCounter *pEventCounter, U32 iTime);
int EventCounter_GetCount(EventCounter *pEventCounter, enumEventCountType eCountType, U32 iTime);

int EventCounter_GetUpdateFrequencyFromCountType(enumEventCountType eCountType);
U32 EventCounter_GetTotalTotal(EventCounter *pEventCounter);
void EventCounter_Destroy(EventCounter *pEventCounter);


typedef struct EventCountingHeatMap EventCountingHeatMap;


//a Double Buffered heatmap works slightly differently. Rather than tracking event counts over various recent time periods,
//it counts until it is told to reset, at which point it "swaps buffers" and starts over from zero. Getting a JPEG from it 
//always gets the last fully counted heatmap
//
//if you create a new heat map, you should make it double buffered before adding any data to it
void ECHeatMap_DoubleBuffered_Reset(EventCountingHeatMap *pHeatMap);


EventCountingHeatMap *ECHeatMap_Create(char *pName, HeatMapType eType, int iMinX, int iMaxX, int iMinZ, int iMaxZ, int iGridSize, U32 iTime);
void ECHeatMap_AddPoint(EventCountingHeatMap *pHeatMap, int iX, int iZ, int iRadius, U32 iTime, int iCategory);
void ECHeatMap_Destroy(EventCountingHeatMap *pHeatMap);

EventCountingHeatMap *ECHeatMap_CreateFromBitmap(char *pName, HeatMapType eType, char *pBitmapData, int iXSize, int iZSize, int iByteDepth, U32 iTime);
void ECHeatMap_SetCategoryName(EventCountingHeatMap *pHeatMap, int iCatNum, const char *pName);
void ECHeatMap_ResetCategoryNames(EventCountingHeatMap *pHeatMap);


void ECHeatMap_WriteJpegOrPng(EventCountingHeatMap *pHeatMap, char *pFileName, float fMaxValue, enumEventCountType eCountType, U32 iTime,
	HeatMapColorLevels *pColorLevels, int iLogScale);

U32 EventCounter_GetMostRecentEventTime(EventCountingHeatMap *pHeatMap);

