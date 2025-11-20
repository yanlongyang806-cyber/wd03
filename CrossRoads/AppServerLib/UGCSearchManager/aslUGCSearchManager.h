/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef enum SSSTreeSearchType SSSTreeSearchType;
typedef enum UGCProjectSearchFilterComparison UGCProjectSearchFilterComparison;
typedef struct UGCContentInfo UGCContentInfo;
typedef struct FeaturedContentList FeaturedContentList;
typedef struct GameProgressionNodeRef GameProgressionNodeRef;
typedef struct QueueDefRef QueueDefRef;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectSearchFilter UGCProjectSearchFilter;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;
typedef struct UGCProjectSeries UGCProjectSeries;

typedef struct TextSearchContext
{
	int iMaxToReturn;
	UGCProject ***pppFoundProjects;
	UGCProjectSeries ***pppFoundSeries;
	GameProgressionNodeRef ***pppFoundProgressionNodesIndexed;
	QueueDefRef ***pppFoundQueuesIndexed;
	UGCProjectSearchInfo *pSearchInfo;
} TextSearchContext;

S32 UGCSearch_GetTotalSearchResultCount(const UGCContentInfo *const *const eaResults,
								const UGCProject *const *const eaUGCProjects,
								const UGCProjectSeries *const *const eaUGCSeries,
								const GameProgressionNodeRef *const *const eaProgressionNodes,
								const QueueDefRef *const *const eaQueues);

bool UGCSearch_ShouldEarlyOut(const UGCContentInfo *const *const eaResults,
								const UGCProject *const *const eaUGCProjects,
								const UGCProjectSeries *const *const eaUGCSeries,
								const GameProgressionNodeRef *const *const eaProgressionNodes,
								const QueueDefRef *const *const eaQueues);

SSSTreeSearchType SSSSearchTypeFromUGCComparison(UGCProjectSearchFilterComparison eComparison);
bool RunStringFilter(UGCProjectSearchFilter *pFilter, const char *pString);
bool RunNumberFilter(UGCProjectSearchFilter *pFilter, float fValue);
