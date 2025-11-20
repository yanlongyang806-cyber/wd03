/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "aslJobManagerPub.h"

typedef struct NameValuePair NameValuePair;
typedef struct JobSummary JobSummary;
typedef struct EventCounter EventCounter;

//for each job, we want to separately track the jobs that come from different job group types. For instance, ugc publishes
//and ugc republishes. That way we can interleave them. Otherwise when 10000 republishes start and then someone tries
//to publish, the publish won't start for days

AUTO_STRUCT;
typedef struct JobListForSingleJobGroup
{
	const char *pJobGroupTypeName; AST(POOL_STRING)
	char **ppJobs;
} JobListForSingleJobGroup;

AUTO_STRUCT;
typedef struct InterleavedJobList
{
	int iNextListIndex;
	JobListForSingleJobGroup **ppLists;
	int iCount;
} InterleavedJobList;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "SetMaxActive, MaxActive, CurActive, CurWaiting");
typedef struct JobManagerJobQueue
{
	const char *pQueueName; //ie, "Server binning"
	int iMaxActive;

	int iCurActive;
	char **ppActive; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))//names of jobs that we believe are currently active

	char *pCurWaiting; AST(ESTRING)
	InterleavedJobList waitingJobs; AST(FORMATSTRING(HTML_SKIP_IN_TABLE = 1))

	AST_COMMAND("SetMaxActive", "SetJobQueueMaxActive \"$FIELD(QueueName)\" $INT(New Max Active) $CONFIRM(This will set the max active for the queue and will also write out the LOCAL copy of jobManagerConfig.txt, you may need to reflect the changes to the reference copy)") 
} JobManagerJobQueue;



//one queue for each GroupType
AUTO_STRUCT;
typedef struct JobManagerGroupQueue
{
	const char *pGroupTypeName; AST(POOL_STRING)
	int iNumActive;
	int iMaxActive;
	JobSummary **ppQueue;  AST(FORMATSTRING(collapsed = 1)) //push onto end, next to run is [0]
	AST_COMMAND("Set Max Active", "SetGroupQueueMaxActive $FIELD(GroupTypeName) $INT(New Max Active) $CONFIRM(This will set the max active for the queue and will also write out the LOCAL copy of jobManagerConfig.txt, you may need to reflect the changes to the reference copy)") 


	EventCounter *pSucceededCounter; NO_AST
	EventCounter *pFailedCounter; NO_AST

	U64 iTotalSucceededTime;
	U64 iTotalQueuedTime;
} JobManagerGroupQueue;

AUTO_STRUCT;
typedef struct JobManagerServerWCmdLineState
{
	ContainerID iServerID; //ID of the server we started, 0 = still unknown
	
	int iCloseRemoteCommandGroupID;
	int iCrashRemoteCommandGroupID; 
		//we requested that the controller notify us if the server crashed or closed. These are the IDs of that request so we
		//can cancel if necessary

} JobManagerServerWCmdLineState;

AUTO_STRUCT;
typedef struct JobManagerRemoteCmdState
{
	int iCloseRemoteCommandGroupID;
	int iCrashRemoteCommandGroupID; 
		//we requested that the controller notify us if the server crashed or closed. These are the IDs of that request so we
		//can cancel if necessary

	bool bCommandReturned;
} JobManagerRemoteCmdState;

AUTO_STRUCT;
typedef struct JobManagerJobState
{
	JobManagerJobDef *pDef;
	U32 iStartTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	U32 iCompleteTime; AST(FORMATSTRING(HTML_SECS_AGO = 1))
	int iPercentDone;
	char *pStateString; AST(ESTRING)
	JobManagerGroupResultEnum eResult;
	JobManagerServerWCmdLineState *pServerWCmdLineState;
	JobManagerRemoteCmdState *pRemoteCmdState;
} JobManagerJobState;

AUTO_STRUCT;
typedef struct JobManagerJobGroupState
{
	JobManagerJobGroupDef *pDef;
	JobManagerJobState **ppJobStates;

	U32 iCrashGroupID;
	U32 iCloseGroupID;

	int iNumStepsSucceeded;
	bool bFailing;

	NameValuePair **ppVariables;

	JobSummary *pSummary; NO_AST
} JobManagerJobGroupState;


AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW = "Name, OwnerName, Failed, StartTime, CompletionTime, LogString, Cancel, JumpQueue");
typedef struct JobSummary
{
	char *pName; 
	char *pOwnerName;
	char *pLogString; AST(ESTRING)
	bool bFailed;
	bool bComplete;
	U32 iStartTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 iCompletionTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))
	U32 iQueuedTime; AST(FORMATSTRING(HTML_SECS_AGO_SHORT = 1))

	int iPlaceInQueue; NO_AST
	JobManagerJobGroupState *pGroupState; NO_AST
	JobManagerGroupQueue *pQueue; NO_AST
	JobManagerJobGroupDef *pGroupDef; NO_AST

	char *pCancel; AST(ESTRING, FORMATSTRING(command=1))
	char *pJumpQueue; AST(ESTRING, FORMATSTRING(command=1))
} JobSummary;

AUTO_STRUCT;
typedef struct JobManagerConfig
{
	NameValuePair **ppMaxGroupQueueSizes; AST(NAME(FullJobCount))
	NameValuePair **ppMaxJobQueueSizes; AST(NAME(IndividualStepCount))
} JobManagerConfig;

extern JobManagerConfig *gpJobManagerConfig;


int JobManagerLibOncePerFrame(F32 fElapsed);

JobSummary *GetSummaryByName(char *pName);
JobSummary *GetCompletedSummaryByName(char *pName);

void AddSummaryToMainStashTable(JobSummary *pSummary);

bool ValidateGroupDefRequest(JobManagerJobGroupDef *pDef);
void StartNewJobFromSummary(JobSummary *pSummary);
JobManagerJobStatus *GetJobStatusFromState(JobManagerJobState *pJobState);

//returns "group name | Job name"
char *GetFullJobName(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob);

//given "group name | Job name", tries to find the group and Job
bool FindGroupAndJobFromFullJobName(char *pFullName, JobManagerJobGroupState **ppGroup, JobManagerJobState **ppJob);

//bFailedForExternalReasons is true if, for instance, two jobs are going on in parallel and one fails... jobmanager then fails
//the other one
void FailJob(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob, bool bFailedForExternalReasons, FORMAT_STR const char *pReason, ...);
#define FailJob(pGroup, pJob, bFailedForExternalReasons, pReason, ...) \
	FailJob(pGroup, pJob, bFailedForExternalReasons, FORMAT_STRING_CHECKED(pReason), __VA_ARGS__)
void SucceedJob(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob, FORMAT_STR const char *pReason, ...);
#define SucceedJob(pGroup, pJob, pReason, ...) SucceedJob(pGroup, pJob, FORMAT_STRING_CHECKED(pReason), __VA_ARGS__)

void FailJobGroup(JobManagerJobGroupState *pGroup, bool bWasCancelled, FORMAT_STR const char *pReason, ...);
#define FailJobGroup(pGroup, bWasCancelled, pReason, ...) FailJobGroup(pGroup, bWasCancelled, FORMAT_STRING_CHECKED(pReason), __VA_ARGS__)

void RegisterCompletedSummary(JobSummary *pSummary);

void SucceedJobGroup(JobManagerJobGroupState *pGroup);

void CleanupJobOnGroupSuccess(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob);

JobManagerGroupQueue *FindGroupQueue(const char *pGroupTypeName);
bool JobGroupQueueIsFull(const char *pGroupTypeName);
void PutSummaryIntoGroupQueue(JobSummary *pSummary);

//returns UNKNOWN if not in a queue
JobManagerGroupResultEnum FindStatusOfJobInQueue(char *pName, int *piOutPlaceInQueue);
void InformQueueOfActiveGroupCompletion(JobManagerGroupQueue *pQueue);

//called whenever a job in this queue completes, and also periodically in case someone has debug increased/decreased the 
//max active size
void MaybeStartGroupFromGroupQueue(JobManagerGroupQueue *pQueue);

void CancelJobGroupByName(char *pSummaryName, const char *pHowCancelled);

void ActuallyStartJob(JobManagerJobGroupState *pGroup, JobManagerJobState *pJob);

void WriteOutJobManagerConfig(void);

void JobLog(JobManagerJobGroupState *pGroupState, FORMAT_STR const char *pFmt, ...);

bool JobManagerIsPaused(void);
