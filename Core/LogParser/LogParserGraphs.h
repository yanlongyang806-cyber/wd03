#pragma once

typedef struct MedianTracker MedianTracker;

AUTO_STRUCT;
typedef struct GraphDataPoint
{
	const char *pDataPointName; AST(POOL_STRING)
	float fSum;
	int iCount;
	float fMin;
	float fMax;
	MedianTracker *pTracker; 
} GraphDataPoint;

// A field value rounded according to a magic field name.
// Presently, only iTime rounding is supported.
AUTO_STRUCT;
typedef struct RoundedFieldValue
{
	U32 iTime; AST(DEFAULT(ULONG_MAX))
} RoundedFieldValue;

AUTO_STRUCT;
typedef struct GraphCategory
{
	const char *pCategoryName; AST(POOL_STRING)
	char *pColorName; NO_AST

	StashTable dataPointsTable; NO_AST
	GraphDataPoint **ppDataPoints;
	RoundedFieldValue minDataPoint;			// Minimum data point seen so far; only set when using magic bar fields.

} GraphCategory;

AUTO_STRUCT AST_IGNORE(ImageLink) AST_IGNORE(LiveImageLink);
typedef struct Graph
{
	char *pLiveLink; AST(ESTRING, FORMATSTRING(HTML=1)) //link to the stored image, if there is one, otherwise the "live" image
	char *pOldDataLink; AST(ESTRING, FORMATSTRING(HTML=1)) //only used if there is a stored image
	char *pGraphName;
	GraphDefinition *pDefinition; 

//a graph may or may not have categories. If it has categories, then categoriesTable and ppCategories
//will be non-NULL. Otherwise, pOnly Category will be non-NULL

	StashTable categoriesTable; NO_AST
	GraphCategory **ppCategories; AST(FORMATSTRING(HTML_SKIP = 1))
		//everything in the list is in the stash table and vice versa


	GraphCategory *pOnlyCategory; AST(FORMATSTRING(HTML_SKIP = 1))

	

	char **ppCategoryNames; AST(POOL_STRING, FORMATSTRING(HTML_SKIP = 1))  //all unique names of categories, allocadded
	char **ppDataPointNames; AST(POOL_STRING, FORMATSTRING(HTML_SKIP = 1)) //all unique names of data points, allocadded

	char **ppCategoryNamesTableFromDef; AST(FORMATSTRING(HTML_SKIP = 1)) //the contents of pDefinition->pCategoryNames, if present.

	U32 iGraphCreationTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 iGraphClearTime; AST(FORMATSTRING(HTML_SECS = 1))
	U32 iMostRecentDataTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	void *pLastFullGraphStoredJpeg; NO_AST
	int iLastFullGraphDataSize; NO_AST

	char *pLastFullGraphStoredCSV; AST(ESTRING)

	bool bSavingStoredVersionRightNow; NO_AST

	//the difference between these two is that if a bunch of data points are averaged together, each will be in iTotalDataCount
	//but only one will go into iUniqueDataPoints
	int iTotalDataCount;
	int iUniqueDataPoints; 


	float fDataMin;
	float fDataMax;
	float fDataSum;

	bool bNamesSorted; //set to false whenever a new data point is added

	//in standalone mode, a graph will only have have data loaded if it is "active", and only files
	//needed for loading active graphs will be loaded
	bool bActiveInStandaloneMode; NO_AST


	U32 *ppTimesOfOldGraphs; //secsSince2000 of times for which old data exists. (Should typically be every day or week from the present backwards 
		//until whenever purging has happened

	char* filename; AST(CURRENTFILE)
} Graph;


AUTO_STRUCT;
typedef struct GraphList
{
	Graph **ppGraphs;
} GraphList;



void Graph_InitSystem(void);
void Graph_CheckLifetimes(U32 iTime);
void Graph_CheckForLongTermGraphs(void);
void Graph_DumpStatusFile(void);
void Graph_CopyStatusFile(void);
void Graph_WriteCSV(Graph *pGraph, char **ppOutEString);

//difference between these two is that FullyResetSystem also reprocesses gLogParserConfig
void Graph_ResetSystem(void);
void Graph_FullyResetSystem(void);

extern StashTable sGraphsByGraphName;
char *GetDescriptiveTimeNameForGraph(Graph *pGraph, U32 iTime);

#define MAX_FLOT_POINTS 200000 //a graph with more than this many points gets slow and unresponsive when graphed with Flot
