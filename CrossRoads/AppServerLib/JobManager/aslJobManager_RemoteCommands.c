#include "aslJobManagerPub.h"
#include "aslJobManager.h"
#include "textParser.h"
#include "aslJobManager_h_ast.h"
#include "aslJobManagerPub_h_ast.h"
#include "timing.h"
#include "estring.h"
#include "NameValuePair.h"

AUTO_COMMAND_REMOTE;
bool BeginNewJobGroup(JobManagerJobGroupDef *pGroup)
{
	JobSummary *pSummary;

	if (!ValidateGroupDefRequest(pGroup))
	{
		return false;
	}
	
	pSummary = StructCreate(parse_JobSummary);
	pSummary->pGroupDef = StructClone(parse_JobManagerJobGroupDef, pGroup);
	pSummary->pName = strdup(pGroup->pJobGroupName);
	pSummary->pOwnerName = strdup(pGroup->owner.pPlayerOwnerAccountName);

	AddSummaryToMainStashTable(pSummary);

	estrPrintf(&pSummary->pCancel, "CancelByName %s $CONFIRM(Really cancel this job?) $NORETURN", pSummary->pName);

	if (JobGroupQueueIsFull(pGroup->pGroupTypeName))
	{
		PutSummaryIntoGroupQueue(pSummary);
	}
	else
	{
		StartNewJobFromSummary(pSummary);
	}


	return true;
}

JobManagerJobStatus *GetJobStatusFromState(JobManagerJobState *pJobState)
{
	JobManagerJobStatus *pStatus = StructCreate(parse_JobManagerJobStatus);
	pStatus->pJobName = strdup(pJobState->pDef->pJobName);
	pStatus->eResult = pJobState->eResult;
	switch(pJobState->eResult)
	{
	xcase JMR_UNKNOWN:

	xcase JMR_ONGOING:
		pStatus->iPercentDone = pJobState->iPercentDone;
		pStatus->iTimeRunning = timeSecondsSince2000() - pJobState->iStartTime;
		pStatus->pCurStatusString = pJobState->pStateString ? strdup(pJobState->pStateString) : NULL;

	xcase JMR_SUCCEEDED:
		pStatus->iPercentDone = 100;
		pStatus->iTimeRunning = pJobState->iCompleteTime - pJobState->iStartTime;
		pStatus->pCurStatusString = pJobState->pStateString ? strdup(pJobState->pStateString) : NULL;

	xcase JMR_FAILED:
		pStatus->iPercentDone = 0;
		pStatus->iTimeRunning = pJobState->iCompleteTime - pJobState->iStartTime;
		pStatus->pCurStatusString = pJobState->pStateString ? strdup(pJobState->pStateString) : NULL;
	}

	return pStatus;
}



AUTO_COMMAND_REMOTE;
JobManagerGroupResult *RequestJobGroupStatus(char *pGroupName)
{
	JobSummary *pSummary = GetSummaryByName(pGroupName);
	JobManagerGroupResult *pResult = StructCreate(parse_JobManagerGroupResult);
	int i;

	pResult->pGroupName = strdup(pGroupName);

	if (!pSummary)
	{
		pSummary = GetCompletedSummaryByName(pGroupName);

		if (!pSummary)
		{
			pResult->eResult = JMR_UNKNOWN;
		}
		else
		{
			pResult->eResult = pSummary->bFailed ? JMR_FAILED : JMR_SUCCEEDED;
			pResult->iTimeStarted = pSummary->iStartTime;
			pResult->iTimeCompleted = pSummary->iCompletionTime;
		}

		return pResult;
	}

	

	StructCopy(parse_JobGroupOwner, &pSummary->pGroupDef->owner, &pResult->owner, 0, 0, 0);

	if (pSummary->pQueue)
	{
		pResult->eResult = JMR_QUEUED;
		pResult->iPlaceInQueue = pSummary->iPlaceInQueue;
		
		return pResult;
	}
	else
	{
		
		if (pSummary->pGroupState->bFailing)
		{
			pResult->eResult = JMR_FAILED;
			pResult->iTimeStarted = pSummary->iStartTime;
			pResult->iTimeCompleted = pSummary->iCompletionTime;
			return pResult;
		}

		pResult->eResult = JMR_ONGOING;
		pResult->iTimeStarted = pSummary->iStartTime;

		for (i=0; i < eaSize(&pSummary->pGroupState->ppJobStates); i++)
		{
			eaPush(&pResult->ppJobStatuses, GetJobStatusFromState(pSummary->pGroupState->ppJobStates[i]));
		}

		return pResult;
	}
}

AUTO_COMMAND_REMOTE;
bool ServerForStepCrashed(char *pFullName)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pFullName, &pGroup, &pJob))
	{
		FailJob(pGroup, pJob, false, "Server crashed");
	}

	return true;

}


AUTO_COMMAND_REMOTE;
bool ServerForStepClosed(char *pFullName)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pFullName, &pGroup, &pJob))
	{
		FailJob(pGroup, pJob, false, "Server closed");
	}

	return true;

}

AUTO_COMMAND_REMOTE ACMD_IFDEF(Job_MANAGER_SUPPORT);
void SetJobStatus(char *pJobName, int iPercentComplete, char *pComment)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		estrCopy2(&pJob->pStateString, pComment);
		pJob->iPercentDone = iPercentComplete;
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(Job_MANAGER_SUPPORT);
bool SetJobComplete(char *pJobName, bool bSucceeded, char *pComment)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		if (bSucceeded)
		{
			SucceedJob(pGroup, pJob, "%s", pComment);
		}
		else
		{
			FailJob(pGroup, pJob, false, "%s", pComment);
		}
	}
	return true;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(JOB_GROUP_VARIABLES);
void SetJobGroupVariable(char *pJobName, char *pVarName, char *pVarValue)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		UpdateOrSetValueInNameValuePairList(&pGroup->ppVariables, pVarName, pVarValue);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(JOB_GROUP_VARIABLES);
char *GetJobGroupVariable(char *pJobName, char *pVarName)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		char *pVal = GetValueFromNameValuePairs(&pGroup->ppVariables, pVarName);
		return pVal ? pVal : "";
	}

	return "";
}

AUTO_COMMAND_REMOTE;
void CancelJobGroup(char *pGroupName, const char *pHowCancelled)
{
	CancelJobGroupByName(pGroupName, pHowCancelled);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(Job_MANAGER_SUPPORT);
void JobLogRemotely(char *pJobName, char *pLogString)
{
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;
	if (FindGroupAndJobFromFullJobName(pJobName, &pGroup, &pJob))
	{
		JobLog(pGroup, "%s", pLogString);
	}
}

