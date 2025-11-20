#ifndef ERRORTRACKER_WEBINTERFACE_H
#define ERRORTRACKER_WEBINTERFACE_H

typedef struct SearchData SearchData;
typedef struct NetLink NetLink;
typedef struct ErrorEntry ErrorEntry;

typedef enum SummaryTableFlags
{
	STF_DEFAULT           = 0,
	STF_NO_SEEN          = (1<<0), // Do not list First Seen / Last Seen
	STF_VERSION          = (1<<1),
	STF_NO_INFO          = (1<<2),
	STF_NO_WIDTH         = (1<<3), // Do not use a table width
	STF_SINGLELINE_JIRA  = (1<<4), // use " / " instead of "<br>" on Jira lines
	STF_SHORT_VERSION    = (1<<5), // Overrides STF_VERSION if used
	STF_SINGLELINE_STACK = (1<<6), // Only display the top valid stack line
} SummaryTableFlags;

void initSearchReports();

void appendSummaryHeader(char **estr, SummaryTableFlags flags);
void appendReportHeader(char **estr, SummaryTableFlags flags, char *reportID, SearchData *sd);
void wiAppendSummaryTableEntry(ErrorEntry *p, char **estr, const char * const * ppExecutableNames, bool bAssignmentMode, int assignmentID, int iMaxCallstackCount, int count, SummaryTableFlags flags);

void wiAppendSearchForm(NetLink *link, 
						char **estr, 
						SearchData *sd, 
						bool bAssignmentMode,
						int iSearchCountLimit,
						int iMaxCallstackCount,
						char *pDateStart,
						char *pDateEnd,
						const char *extraBlockText);

typedef struct TriviaOverviewItem TriviaOverviewItem;
void appendTriviaOverview (char **estr, CONST_EARRAY_OF(TriviaOverviewItem) ppTriviaItems);

void errorTrackerWebInterfaceInit(void);

#endif
