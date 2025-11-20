#include "aslJobManager.h"
#include "aslJobManagerJobQueues.h"
#include "stashTable.h"
#include "aslJobManager_h_ast.h"
#include "NameValuePair.h"
#include "error.h"
#include "ResourceInfo.h"
#include "alerts.h"

StashTable hJobQueuesByName = NULL;

char *InterleavedJobList_GetNextJob(InterleavedJobList *pList)
{
	char *pRetVal;
	int iCounter = 0;

	if (!eaSize(&pList->ppLists))
	{
		return NULL;
	}

	if (!pList->iCount)
	{
		return NULL;
	}

	while (!eaSize(&pList->ppLists[pList->iNextListIndex]->ppJobs) && iCounter < eaSize(&pList->ppLists))
	{
		pList->iNextListIndex++;
		pList->iNextListIndex %= eaSize(&pList->ppLists);
		iCounter++;
	}

	if (iCounter == eaSize(&pList->ppLists))
	{
		AssertOrAlert("JOBGROUP_CORRUPTION", "Job group thinks it has jobs, but can't find any in any list... something out of sync?");
		return NULL;
	}

	pRetVal = eaRemove(&pList->ppLists[pList->iNextListIndex]->ppJobs, 0);
	pList->iNextListIndex++;
	pList->iNextListIndex %= eaSize(&pList->ppLists);

	if (pRetVal)
	{
		pList->iCount--;
	}

	return pRetVal;
}

void InterleavedJobList_AddJob(InterleavedJobList *pList, char *pJob, const char *pGroupTypeName)
{
	int i;

	JobListForSingleJobGroup *pSingleGroupList;

	for (i=0; i < eaSize(&pList->ppLists); i++)
	{
		if (pList->ppLists[i]->pJobGroupTypeName == pGroupTypeName)
		{
			eaPush(&pList->ppLists[i]->ppJobs, pJob);
			pList->iCount++;
			return;
		}
	}

	pSingleGroupList = StructCreate(parse_JobListForSingleJobGroup);
	pSingleGroupList->pJobGroupTypeName = pGroupTypeName;
	eaPush(&pList->ppLists, pSingleGroupList);
	eaPush(&pSingleGroupList->ppJobs, pJob);
	pList->iCount++;
}

int InterleavedJobList_GetCount(InterleavedJobList *pList)
{
	return pList->iCount;
}

void SetCurWaitingString(JobManagerJobQueue *pQueue)
{
	int i;
	estrPrintf(&pQueue->pCurWaiting, "%d", InterleavedJobList_GetCount(&pQueue->waitingJobs));

	if (InterleavedJobList_GetCount(&pQueue->waitingJobs))
	{
		estrConcatf(&pQueue->pCurWaiting, " (");
		for (i = 0; i < eaSize(&pQueue->waitingJobs.ppLists); i++)
		{
			estrConcatf(&pQueue->pCurWaiting, "%s%s %d", i == 0 ? "" : ", ", pQueue->waitingJobs.ppLists[i]->pJobGroupTypeName, eaSize(&pQueue->waitingJobs.ppLists[i]->ppJobs));
		}
		estrConcatf(&pQueue->pCurWaiting, ")");
	}
		
}

void InitJobQueues(void)
{
	int i;
	hJobQueuesByName = stashTableCreateWithStringKeys(16, StashDefault);
	resRegisterDictionaryForStashTable("JobQueues", RESCATEGORY_SYSTEM, 0, hJobQueuesByName, parse_JobManagerJobQueue);

	if (gpJobManagerConfig)
	{
		for (i=0; i < eaSize(&gpJobManagerConfig->ppMaxJobQueueSizes); i++)
		{
			JobManagerJobQueue *pQueue = StructCreate(parse_JobManagerJobQueue);

			pQueue->pQueueName = strdup(gpJobManagerConfig->ppMaxJobQueueSizes[i]->pName);
			pQueue->iMaxActive = atoi(gpJobManagerConfig->ppMaxJobQueueSizes[i]->pValue);

			if (pQueue->iMaxActive <= 0)
			{
				AssertOrAlert("BAD_JOB_QUEUE_CONFIG", "Job queue %s has invalid max active size", pQueue->pQueueName);
			}
			else
			{
				stashAddPointer(hJobQueuesByName, pQueue->pQueueName, pQueue, false);
			}
		}
	}
}


void UpdateJobQueue(JobManagerJobQueue *pQueue)
{
	int i;
	JobManagerJobGroupState *pGroup;
	JobManagerJobState *pJob;

	for (i = eaSize(&pQueue->ppActive) - 1; i >= 0; i--)
	{
		bool bNoLongerValid = true;

		if (FindGroupAndJobFromFullJobName(pQueue->ppActive[i], &pGroup, &pJob))
		{
			if (pJob->eResult == JMR_ONGOING)
			{
				bNoLongerValid = false;
			}
		}

		if (bNoLongerValid)
		{
			free(pQueue->ppActive[i]);
			eaRemoveFast(&pQueue->ppActive, i);
		}
	}

	if (!JobManagerIsPaused())
	{
		while (eaSize(&pQueue->ppActive) < pQueue->iMaxActive && InterleavedJobList_GetCount(&pQueue->waitingJobs))
		{
			bool bNoLongerValid = true;
			char *pPotential = InterleavedJobList_GetNextJob(&pQueue->waitingJobs);

			if (FindGroupAndJobFromFullJobName(pPotential, &pGroup, &pJob))
			{
				if (pJob->eResult == JMR_QUEUED)
				{
					bNoLongerValid = false;
				}
			}

			if (bNoLongerValid)
			{
				free(pPotential);
			}
			else
			{
				eaPush(&pQueue->ppActive, pPotential);
				ActuallyStartJob(pGroup, pJob);
			}
		}
	}

	pQueue->iCurActive = eaSize(&pQueue->ppActive);

	SetCurWaitingString(pQueue);
}

void JobQueues_AddOrStartJob(JobManagerJobGroupState *pGroupState, JobManagerJobState *pJobState)
{
	JobManagerJobQueue *pQueue;
	if (!stashFindPointer(hJobQueuesByName, pJobState->pDef->pJobName, &pQueue))
	{
		pQueue = StructCreate(parse_JobManagerJobQueue);
		pQueue->iMaxActive = 2;
		pQueue->pQueueName = strdup(pJobState->pDef->pJobName);

		stashAddPointer(hJobQueuesByName, pQueue->pQueueName, pQueue, false);

		WARNING_NETOPS_ALERT("UNKNOWN_JOB", "Someone is requesting an individual job step named %s. No maximum queue size is specified for this in jobManagerConfig.txt, defaulting to 2", pQueue->pQueueName);
	}

	UpdateJobQueue(pQueue);
	if (JobManagerIsPaused())
	{
		JobLog(pGroupState, "Ready to start job %s, but job manager is paused. Added to queue.",
			pQueue->pQueueName);
		pJobState->eResult = JMR_QUEUED;
		InterleavedJobList_AddJob(&pQueue->waitingJobs, strdup(GetFullJobName(pGroupState, pJobState)), pGroupState->pDef->pGroupTypeName);
	}
	else if (eaSize(&pQueue->ppActive) < pQueue->iMaxActive)
	{
		ActuallyStartJob(pGroupState, pJobState);
		eaPush(&pQueue->ppActive, strdup(GetFullJobName(pGroupState, pJobState)));
	}
	else
	{
		JobLog(pGroupState, "Ready to start job %s, but queue is full. Added to queue.",
			pQueue->pQueueName);
		pJobState->eResult = JMR_QUEUED;
		InterleavedJobList_AddJob(&pQueue->waitingJobs, strdup(GetFullJobName(pGroupState, pJobState)), pGroupState->pDef->pGroupTypeName);
	}
	

}

void UpdateJobQueues(void)
{
	FOR_EACH_IN_STASHTABLE(hJobQueuesByName, JobManagerJobQueue, pQueue)
	{
		UpdateJobQueue(pQueue);
	}
	FOR_EACH_END
}


AUTO_COMMAND;
char *SetJobQueueMaxActive(char *pQueueName, int iMaxActive)
{
	JobManagerJobQueue *pQueue;

	if (!stashFindPointer(hJobQueuesByName, pQueueName, &pQueue))
	{
		return "Couldn't find that queue";
	}

	pQueue->iMaxActive = iMaxActive;
	
	UpdateOrSetValueInNameValuePairList(&gpJobManagerConfig->ppMaxJobQueueSizes, pQueueName, STACK_SPRINTF("%d", iMaxActive));

	WriteOutJobManagerConfig();

	return "Udpated";
}
