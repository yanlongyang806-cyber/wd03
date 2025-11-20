#include "PatchClientLibStatusMonitoring.h"
#include "PatchClientLibStatusMonitoring_h_ast.h"
#include "net.h"
#include "structNet.h"
#include "earray.h"
#include "EString.h"
#include "Alerts.h"
#include "GlobalComm.h"
#include "StashTable.h"
#include "ResourceInfo.h"
#include "StringUtil.h"
#include "winutil.h"
#include "StringCache.h"

static bool sbStatusMonitoringActive = false;
static PCLStatusMonitoringCB spCB = NULL;
static U32 *spTimeoutIntervals = NULL;
static int siPersistTimeAfterFail = 0;
static int siPersistTimeAfterSucceed = 0;

static StashTable sUpdatesByIDString = NULL;
static StashTable sFailedOrSucceededUpdatesByString = NULL;
static StashTable sAllUpdatesForMonitoring = NULL;

static U32 siThreadID = 0;

#define THREAD_ALERT() if (GetCurrentThreadId() != siThreadID) { AssertOrAlert("PCLSM_THREADMISMATCH", "PatchClientLibStatusMonitoring must all be called from a single thread... " __FUNCTION__ " was not");};

static CRITICAL_SECTION sPCLStatusMonCritSec;

static void LazyCreateStashTable(void)
{
	if (!sUpdatesByIDString)
	{
		sUpdatesByIDString = stashTableCreateWithStringKeys(16, StashDefault);
		sFailedOrSucceededUpdatesByString = stashTableCreateWithStringKeys(16, StashDefault);
		sAllUpdatesForMonitoring = stashTableCreateWithStringKeys(16, StashDefault);
	
		resRegisterDictionaryForStashTable("PatchingStatuses", RESCATEGORY_SYSTEM, 0, sAllUpdatesForMonitoring, parse_PCLStatusMonitoringUpdate);

	}
}

static void RemoveSucceededOrFailedStatus(char *pName)
{
	PCLStatusMonitoringUpdate *pUpdate = NULL;

	if (stashRemovePointer(sFailedOrSucceededUpdatesByString, pName, &pUpdate))
	{
		stashRemovePointer(sAllUpdatesForMonitoring, pName, NULL);
		StructDestroy(parse_PCLStatusMonitoringUpdate, pUpdate);
	}
}

void PCLStatusMonitoring_DismissSucceededOrFailedByName(char *pName)
{
	THREAD_ALERT();
	RemoveSucceededOrFailedStatus(pName);
}

static void ManageSucceededOrFailedStatus(PCLStatusMonitoringUpdate *pUpdate)
{
	if (siPersistTimeAfterSucceed && pUpdate->internalStatus.eState == PCLSMS_SUCCEEDED || siPersistTimeAfterFail)
	{
		pUpdate->iSucceededOrFailedTime = timeSecondsSince2000();
		stashAddPointer(sFailedOrSucceededUpdatesByString, pUpdate->internalStatus.pMyIDString, pUpdate, true);
		stashAddPointer(sAllUpdatesForMonitoring, pUpdate->internalStatus.pMyIDString, pUpdate, true);
	}
	else
	{
		StructDestroy(parse_PCLStatusMonitoringUpdate, pUpdate);
	}
}

static void HandleUpdate(PCLStatusMonitoringUpdate_Internal *pUpdate)
{
	PCLStatusMonitoringUpdate *pPreExisting;
	bool bSucceededOrFailed = false;

	LazyCreateStashTable();

	//any time we get a new update on something with a given name, any previous 
	//failed/succeeded status is automatically obsolete
	RemoveSucceededOrFailedStatus(pUpdate->pMyIDString);


	if (stashFindPointer(sUpdatesByIDString, pUpdate->pMyIDString, &pPreExisting))
	{

		pPreExisting->internalStatus.eState = pUpdate->eState;
		pPreExisting->iMostRecentUpdateTime = timeSecondsSince2000();
		pPreExisting->iTimeoutLength = 0;

		switch (pUpdate->eState)
		{
		case PCLSMS_UPDATE:
			pPreExisting->iNumUpdatesReceived++;
			StructCopy(parse_PCLStatusMonitoringUpdate_Internal, pUpdate, &pPreExisting->internalStatus, 0, 0, 0);
			break;

		case PCLSMS_FAILED:
		case PCLSMS_SUCCEEDED:
			bSucceededOrFailed = true;
			StructCopy(parse_PCLStatusMonitoringUpdate_Internal, pUpdate, &pPreExisting->internalStatus, 0, 0, 0);
			break;
		}

		if (spCB)
		{
			spCB(pPreExisting);
		}

		if (bSucceededOrFailed)
		{
			stashRemovePointer(sUpdatesByIDString, pPreExisting->internalStatus.pMyIDString, NULL);
			stashRemovePointer(sAllUpdatesForMonitoring, pPreExisting->internalStatus.pMyIDString, NULL);
			ManageSucceededOrFailedStatus(pPreExisting);
		}
	}
	else
	{
		PCLStatusMonitoringUpdate *pNewStatus = StructCreate(parse_PCLStatusMonitoringUpdate);

		pNewStatus->iTimeBegan = pNewStatus->iMostRecentUpdateTime = timeSecondsSince2000();
		StructCopy(parse_PCLStatusMonitoringUpdate_Internal, pUpdate, &pNewStatus->internalStatus, 0, 0, 0);

		switch (pUpdate->eState)
		{
		case PCLSMS_UPDATE:
			pNewStatus->iNumUpdatesReceived = 1;
			stashAddPointer(sUpdatesByIDString, pNewStatus->internalStatus.pMyIDString, pNewStatus, false);
			stashAddPointer(sAllUpdatesForMonitoring, pNewStatus->internalStatus.pMyIDString, pNewStatus, false);
			
			break;
		default:
			bSucceededOrFailed = true;
			break;

		}	

		if (spCB)
		{
			spCB(pNewStatus);
		}

		if (bSucceededOrFailed)
		{
			ManageSucceededOrFailedStatus(pNewStatus);
		}
	}
}

//TO_PATCHCLIENT_FOR_STATUSREPORTING_ABORT

static void PCLStatusMonitoringHandleCB(Packet *pak,int cmd, NetLink *link,void *pUserData)
{
	switch (cmd)
	{
	xcase FROM_PATCHCLIENT_FOR_STATUSREPORTING_UPDATE:
		{
			PCLStatusMonitoringUpdate_Internal *pUpdate = StructCreate(parse_PCLStatusMonitoringUpdate_Internal);
			ParserRecvStructSafe(parse_PCLStatusMonitoringUpdate_Internal, pak, pUpdate);
			linkSetUserData(link, (char*)allocAddString(pUpdate->pMyIDString));
			HandleUpdate(pUpdate);
		}
	}
}

static NetListen *spPCLStatusMonitoringListen = NULL;

bool PCLStatusMonitoring_Begin(NetComm *pComm, PCLStatusMonitoringCB pCB, U32 iListeningPortNum, 
	U32 *pTimeOutIntervals, int iPersistTimeAfterFail, int iPersistTimeAfterSucceed )
{
	int i;
	bool bIntervalsAreBad = false;

	if (sbStatusMonitoringActive)
	{
		AssertOrAlert("PCLSTATUSMONITORING_RESTART", "Can't start PCL Status monitoring if it's already going");
		return false;
	}

	
	InitializeCriticalSection(&sPCLStatusMonCritSec);
	siThreadID = GetCurrentThreadId();

	for (i = 0; i < ea32Size(&pTimeOutIntervals); i++)
	{
		if (pTimeOutIntervals[i] == 0 || (i > 0 && pTimeOutIntervals[i] <= pTimeOutIntervals[i-1]))
		{
			AssertOrAlert("PCLSTATUSMONITORING_BADTIMEOUTS", "Timeout intervals for PCL Status Monitoring must be strictly increasing positive integers");
			bIntervalsAreBad = true;
			break;
		}
	}

	if (!bIntervalsAreBad)
	{
		ea32Copy(&spTimeoutIntervals, &pTimeOutIntervals);
	}

	sbStatusMonitoringActive = true;
	spCB = pCB;
	siPersistTimeAfterFail = iPersistTimeAfterFail;
	siPersistTimeAfterSucceed = iPersistTimeAfterSucceed;

	spPCLStatusMonitoringListen = commListen(pComm, LINKTYPE_UNSPEC, 0, iListeningPortNum, PCLStatusMonitoringHandleCB, 0, 0, 0);

	return !!spPCLStatusMonitoringListen;
}

//the only threadsafe thing in the whole file, basically
static char **sppIDStringsToAdd = NULL;

void PCLStatusMonitoring_AddInternal(char *pIDString);

void PCLStatusMonitoring_Tick(void)
{
	U32 iCurTime;
	int iNumTimeoutIntervals;
	if (!sbStatusMonitoringActive)
	{
		return;
	}

	THREAD_ALERT();

	if (sppIDStringsToAdd)
	{
		char **ppArrayCopy;
		EnterCriticalSection(&sPCLStatusMonCritSec);
		ppArrayCopy = sppIDStringsToAdd;
		sppIDStringsToAdd = NULL;
		LeaveCriticalSection(&sPCLStatusMonCritSec);
		FOR_EACH_IN_EARRAY(ppArrayCopy, char, pStringToAdd)
		{
			PCLStatusMonitoring_AddInternal(pStringToAdd);
		}
		FOR_EACH_END;
		eaDestroyEx(&ppArrayCopy, NULL);
	}

	if (!sUpdatesByIDString)
	{
		return;
	}

	if (!((iNumTimeoutIntervals = ea32Size(&spTimeoutIntervals))))
	{
		return;
	}

	iCurTime = timeSecondsSince2000();

	FOR_EACH_IN_STASHTABLE(sUpdatesByIDString, PCLStatusMonitoringUpdate, pStatus)
	{
		U32 iLag = timeSecondsSince2000() - pStatus->iMostRecentUpdateTime;
		
		if (iLag >= spTimeoutIntervals[0])
		{
			if (iLag >= spTimeoutIntervals[iNumTimeoutIntervals - 1])
			{
				pStatus->internalStatus.eState = PCLSMS_FAILED_TIMEOUT;
				pStatus->iTimeoutLength = spTimeoutIntervals[iNumTimeoutIntervals - 1];
				pStatus->iTimeoutIndex = iNumTimeoutIntervals - 1;
				if (spCB)
				{
					spCB(pStatus);
				}

				stashRemovePointer(sUpdatesByIDString, pStatus->internalStatus.pMyIDString, NULL);
				stashRemovePointer(sAllUpdatesForMonitoring, pStatus->internalStatus.pMyIDString, NULL);
				ManageSucceededOrFailedStatus(pStatus);
			}
			else
			{
				//find the highest timeout that we have surpassed
				int iTimeoutIndex = iNumTimeoutIntervals - 2;

				while (iTimeoutIndex && iLag < spTimeoutIntervals[iTimeoutIndex])
				{
					iTimeoutIndex--;
				}

				if (iTimeoutIndex < 0)
				{
					AssertOrAlert("PCLSTATUSMONITORING_TIMEOUTCORRUPTION", "Logic error while checking PCL statuses for timeout");
				}
				else
				{
					if (pStatus->iTimeoutLength != spTimeoutIntervals[iTimeoutIndex])
					{
						pStatus->internalStatus.eState = PCLSMS_TIMEOUT;
						pStatus->iTimeoutLength = spTimeoutIntervals[iTimeoutIndex];
						pStatus->iTimeoutIndex = iTimeoutIndex;

						if (spCB)
						{
							spCB(pStatus);
						}
					}
				}
			}
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(sFailedOrSucceededUpdatesByString, PCLStatusMonitoringUpdate, pStatus)
	{
		int iTimeout;
		if (pStatus->internalStatus.eState == PCLSMS_SUCCEEDED)
		{
			iTimeout = siPersistTimeAfterSucceed;
		}
		else
		{
			iTimeout = siPersistTimeAfterFail;
		}

		if (pStatus->iSucceededOrFailedTime < timeSecondsSince2000() - iTimeout)
		{
			if (spCB)
			{	
				pStatus->internalStatus.eState = PCLSMS_DELETING;
				spCB(pStatus);
			}

			stashRemovePointer(sFailedOrSucceededUpdatesByString, pStatus->internalStatus.pMyIDString, NULL);
			stashRemovePointer(sAllUpdatesForMonitoring, pStatus->internalStatus.pMyIDString, NULL);
			StructDestroy(parse_PCLStatusMonitoringUpdate, pStatus);
		}
	}
	FOR_EACH_END;
}


void PCLStatusMonitoring_Add(char *pIDString)
{
	EnterCriticalSection(&sPCLStatusMonCritSec);
	eaPush(&sppIDStringsToAdd, strdup(pIDString));
	LeaveCriticalSection(&sPCLStatusMonCritSec);
}

static void PCLStatusMonitoring_AddInternal(char *pIDString)
{
	PCLStatusMonitoringUpdate *pNewStatus;

	if (!sbStatusMonitoringActive)
	{
		AssertOrAlert("PCLSTATUSMONITORING_INACTIVE", "Someone trying to add a PCL stats monitoring status when monitoring is inactive");
		return;
	}

	//no harm no foul if it already exists... this presumably just means that your request to start patching got out there REALLY fast, but set
	//update time to now just to avoid some weird timeout case
	if (stashFindPointer(sUpdatesByIDString, pIDString, &pNewStatus))
	{
		pNewStatus->iMostRecentUpdateTime = timeSecondsSince2000();
		return;
	}

	pNewStatus = StructCreate(parse_PCLStatusMonitoringUpdate);

	pNewStatus->internalStatus.pMyIDString = strdup(pIDString);
	pNewStatus->internalStatus.eState = PCLSMS_INTERNAL_CREATE;
	pNewStatus->iMostRecentUpdateTime = pNewStatus->iTimeBegan = timeSecondsSince2000();

	LazyCreateStashTable();

	stashAddPointer(sUpdatesByIDString, pNewStatus->internalStatus.pMyIDString, pNewStatus, false);
	stashAddPointer(sAllUpdatesForMonitoring, pNewStatus->internalStatus.pMyIDString, pNewStatus, false);

	RemoveSucceededOrFailedStatus(pIDString);
}


bool PCLStatusMonitoring_IsActive(void)
{
	return sbStatusMonitoringActive;
}


typedef struct PCLStatusMonitoringIterator
{
	bool bFinishedMainStash;
	StashTableIterator stashIterator;
	int iFailedOrSucceededIndex;
} PCLStatusMonitoringIterator;



static void CheckForDeletionRequests(void)
{
	FOR_EACH_IN_STASHTABLE(sUpdatesByIDString, PCLStatusMonitoringUpdate, pStatus)
	{
		if (pStatus->bDestroyRequested)
		{
			stashRemovePointer(sUpdatesByIDString, pStatus->internalStatus.pMyIDString, NULL);
			stashRemovePointer(sAllUpdatesForMonitoring, pStatus->internalStatus.pMyIDString, NULL);
			StructDestroy(parse_PCLStatusMonitoringUpdate, pStatus);
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_STASHTABLE(sFailedOrSucceededUpdatesByString, PCLStatusMonitoringUpdate, pStatus)
	{
		if (pStatus->bDestroyRequested)
		{
			stashRemovePointer(sFailedOrSucceededUpdatesByString, pStatus->internalStatus.pMyIDString, NULL);
			stashRemovePointer(sAllUpdatesForMonitoring, pStatus->internalStatus.pMyIDString, NULL);
			StructDestroy(parse_PCLStatusMonitoringUpdate, pStatus);
		}
	}
	FOR_EACH_END;
}

PCLStatusMonitoringUpdate *PCLStatusMonitoring_GetNextUpdateFromIterator(PCLStatusMonitoringIterator **ppIterator)
{
	PCLStatusMonitoringIterator *pIterator;
	StashElement elem;

	if (!ppIterator)
	{
		return NULL;
	}

	THREAD_ALERT();

	if (!sUpdatesByIDString)
	{
		return NULL;
	}

	if (!(*ppIterator))
	{
		*ppIterator = calloc(sizeof(PCLStatusMonitoringIterator), 1);
		stashGetIterator(sUpdatesByIDString, &(*ppIterator)->stashIterator);
	}
	
	pIterator = *ppIterator;

	if (!pIterator->bFinishedMainStash)
	{
		if (!stashGetNextElement(&pIterator->stashIterator, &elem))
		{
			pIterator->bFinishedMainStash = true;
			stashGetIterator(sFailedOrSucceededUpdatesByString, &(*ppIterator)->stashIterator);
		}
		else
		{
			return stashElementGetPointer(elem);
		}
	}

	if (!stashGetNextElement(&pIterator->stashIterator, &elem))
	{
		free(pIterator);
		*ppIterator = NULL;

		CheckForDeletionRequests();
		return NULL;
	}
	else
	{
		return stashElementGetPointer(elem);
	}
}

PCLStatusMonitoringUpdate *PCLStatusMonitoring_FindStatusByName(char *pIDString)
{
	PCLStatusMonitoringUpdate *pRetVal;

	THREAD_ALERT();

	if (stashFindPointer(sUpdatesByIDString, pIDString, &pRetVal))
	{
		return pRetVal;
	}

	if (stashFindPointer(sFailedOrSucceededUpdatesByString, pIDString, &pRetVal))
	{
		return pRetVal;
	}

	return NULL;
}

static int PCLStatusMonitoringAbortCB(NetLink* link, S32 index, void *link_user_data, void *func_data)
{
	if (stricmp_safe((char*)link_user_data, (char*)func_data) == 0)
	{
		Packet *pPak = pktCreate(link, TO_PATCHCLIENT_FOR_STATUSREPORTING_ABORT);
		pktSend(&pPak);
	}

	return 1;
}


void PCLStatusMonitoring_AbortPatchingTask(char *pIDString)
{
	linkIterate2(spPCLStatusMonitoringListen, PCLStatusMonitoringAbortCB, pIDString); // For when you need more data
}
	


void OVERRIDE_LATELINK_PCLStatusMonitoring_GetAllStatuses(PCLStatusMonitoringUpdate ***pppUpdates)
{
	PCLStatusMonitoringIterator *pIter = NULL;
	PCLStatusMonitoringUpdate *pUpdate = NULL;

	while ((pUpdate = PCLStatusMonitoring_GetNextUpdateFromIterator(&pIter)))
	{
		eaPush(pppUpdates, pUpdate);
	}
}









#include "PatchClientLibStatusMonitoring_h_ast.c"