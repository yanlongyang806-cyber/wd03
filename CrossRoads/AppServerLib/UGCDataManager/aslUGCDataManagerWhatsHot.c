#include "aslUGCDataManagerWhatsHot.h"
#include "aslMapManagerPub.h"
#include "aslMapManagerPub_h_ast.h"
#include "StashTable.h"
#include "aslUGCDataManagerWhatsHot_c_ast.h"
#include "Textparser.h"
#include "timing.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "file.h"
#include "alerts.h"
#include "rand.h"
#include "UGCCommon.h"
#include "stringUtil.h"
#include "UGCProjectUtils.h"
#include "objContainer.h"
#include "ugcProjectCommon.h"
#include "httpXPathSupport.h"

#include "aslUGCDataManager.h"
#include "../../libs/serverlib/objects/ServerLibPrefStore.h"

#define MULTIPLETIMEPERIODS_DB_NAME "UGC_WhatsHotData"

//bucked size in seconds for whats-hot buckets. The whats hot list will update every n seconds, and old
//launches will fade out in n-second-size buckets
static int sWhatsHotTimePeriod = 3600;
AUTO_CMD_INT(sWhatsHotTimePeriod, WhatsHotTimePeriod);

//save this many buckets
static int iWhatsHotOldTimePeriodsToSave = 24 * 21; //3 weeks
AUTO_CMD_INT(iWhatsHotOldTimePeriodsToSave, WhatsHotOldTimePeriodsToSave);

//oldest saved periods have this weight reduction
static float fWhatsHotOldTimePeriodWeight = 0.25f;
AUTO_CMD_FLOAT(fWhatsHotOldTimePeriodWeight, WhatsHotOldTimePeriodWeight);

//save this many data points in each bucket when sending to object DB
static int siProjectsPerTimePeriodToSave = 250;
AUTO_CMD_INT(siProjectsPerTimePeriodToSave, ProjectsPerTimePeriodToSave);

static int siMaxWhatsHotListLength = 300;
AUTO_CMD_INT(siMaxWhatsHotListLength, MaxWhatsHotListLength);

static ContainerID *spUGCSearchManagerIDs = NULL;

static WhatsHotList sWhatsHotList = {0};

WhatsHotList *GetWhatsHotList(void)
{
	return &sWhatsHotList;
}
AUTO_STRUCT;
typedef struct ProjLaunchCount
{
	U32 iProjID;
	int iLaunchCount;
} ProjLaunchCount;

AUTO_STRUCT;
typedef struct SingleTimePeriodCountTracker
{
	U32 iStartingTime;
	U32 iEndingTime;
	StashTable hProjLaunchCountsByID; NO_AST
	ProjLaunchCount **ppProjLaunchCounts; //used for load/save
} SingleTimePeriodCountTracker;

static SingleTimePeriodCountTracker *spCurrentTimePeriod = NULL;


AUTO_STRUCT;
typedef struct MultipleTimePeriods
{
	SingleTimePeriodCountTracker **ppTimePeriods; //oldest = [0]
} MultipleTimePeriods;

AUTO_STRUCT;
typedef struct SingleProjWhatsHotTracker
{
	U32 iProjID;
	float fWeight; 
} SingleProjWhatsHotTracker;

static MultipleTimePeriods *spMultipleTimePeriods = NULL;


AUTO_FIXUPFUNC;
TextParserResult SingleTimePeriodCountTracker_Fixup(SingleTimePeriodCountTracker *pTracker, enumTextParserFixupType eFixupType, void *pExtraData)
{
	switch (eFixupType)
	{
	case FIXUPTYPE_DESTRUCTOR:
		stashTableDestroyStruct(pTracker->hProjLaunchCountsByID, NULL, parse_ProjLaunchCount);
		break;

	case FIXUPTYPE_CONSTRUCTOR:
		pTracker->hProjLaunchCountsByID = stashTableCreateInt(1024);
		break;

	}

	return true;
}

static void LazyCreateTimePeriods(void)
{
	if (!spCurrentTimePeriod)
	{
		spCurrentTimePeriod = StructCreate(parse_SingleTimePeriodCountTracker);
		spCurrentTimePeriod->iStartingTime = timeSecondsSince2000();
		spCurrentTimePeriod->iEndingTime = spCurrentTimePeriod->iStartingTime + sWhatsHotTimePeriod;
	}

	if (!spMultipleTimePeriods)
	{
		spMultipleTimePeriods = StructCreate(parse_MultipleTimePeriods);
	}
}



int SortTrackersByWeight(const SingleProjWhatsHotTracker **pTracker1, const SingleProjWhatsHotTracker **pTracker2)
{
	if ((*pTracker1)->fWeight > (*pTracker2)->fWeight)
	{
		return -1;
	}
	else if ((*pTracker1)->fWeight < (*pTracker2)->fWeight)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void WhatsHotList_CalculateFromMultipleTimePeriods(WhatsHotList *pHotList, MultipleTimePeriods *pTimePeriods)
{	
	StashTable hTrackersByID = stashTableCreateInt(1024);
	int i;
	U32 iCurTime = timeSecondsSince2000();

	SingleProjWhatsHotTracker **ppTrackers = NULL;

	for (i=0; i < eaSize(&pTimePeriods->ppTimePeriods); i++)
	{
		SingleTimePeriodCountTracker *pCurTimePeriod = pTimePeriods->ppTimePeriods[i];
		float fWeight = 1.0f - ((iCurTime - pCurTimePeriod->iEndingTime) / (iWhatsHotOldTimePeriodsToSave * sWhatsHotTimePeriod)) * (1.0f - fWhatsHotOldTimePeriodWeight);
		fWeight = CLAMP(fWeight, fWhatsHotOldTimePeriodWeight, 1.0f);

		if (pCurTimePeriod->hProjLaunchCountsByID)
		{
			FOR_EACH_IN_STASHTABLE(pCurTimePeriod->hProjLaunchCountsByID, ProjLaunchCount, pSingleLaunchCount)
			{
				SingleProjWhatsHotTracker *pSingleProjWhatsHotTracker;
				if (!stashIntFindPointer(hTrackersByID, pSingleLaunchCount->iProjID, &pSingleProjWhatsHotTracker))
				{
					pSingleProjWhatsHotTracker = StructCreate(parse_SingleProjWhatsHotTracker);
					pSingleProjWhatsHotTracker->iProjID = pSingleLaunchCount->iProjID;
					stashIntAddPointer(hTrackersByID, pSingleProjWhatsHotTracker->iProjID, pSingleProjWhatsHotTracker, false);
				}

				pSingleProjWhatsHotTracker->fWeight += (float)(pSingleLaunchCount->iLaunchCount) * fWeight;

			}
			FOR_EACH_END
		}
	}

	FOR_EACH_IN_STASHTABLE(hTrackersByID, SingleProjWhatsHotTracker, pSingleProjWhatsHotTracker)
	{
		eaPush(&ppTrackers, pSingleProjWhatsHotTracker);

	}
	FOR_EACH_END

	stashTableDestroy(hTrackersByID);

	eaQSort(ppTrackers, SortTrackersByWeight);

	ea32Clear(&pHotList->pProjectIDs);
	ea32SetCapacity(&pHotList->pProjectIDs, eaSize(&ppTrackers));
	for (i=0; i < eaSize(&ppTrackers) && i < siMaxWhatsHotListLength; i++)
	{
		ea32Push(&pHotList->pProjectIDs, ppTrackers[i]->iProjID);
	}

	eaDestroyStruct(&ppTrackers, parse_SingleProjWhatsHotTracker);
}

static void SingleTimePeriodCountTracker_AddCount(SingleTimePeriodCountTracker *pTracker, U32 iProjID, int iCount)
{
	ProjLaunchCount *pProjLaunchCount;

	if (!stashIntFindPointer(pTracker->hProjLaunchCountsByID, iProjID, &pProjLaunchCount))
	{
		pProjLaunchCount = StructCreate(parse_ProjLaunchCount);
		pProjLaunchCount->iProjID = iProjID;
		stashIntAddPointer(pTracker->hProjLaunchCountsByID, iProjID, pProjLaunchCount, false);
	}

	pProjLaunchCount->iLaunchCount += iCount;
}

void MultipleTimePeriods_ListsToStashTables(MultipleTimePeriods *pMultipleTimePeriods)
{
	int iWhichTimePeriod, i;
	for (iWhichTimePeriod = 0; iWhichTimePeriod < eaSize(&pMultipleTimePeriods->ppTimePeriods); iWhichTimePeriod++)
	{
		SingleTimePeriodCountTracker *pTimePeriod = pMultipleTimePeriods->ppTimePeriods[iWhichTimePeriod];

		if (!pTimePeriod->hProjLaunchCountsByID)
		{
			pTimePeriod->hProjLaunchCountsByID = stashTableCreateInt(1024);
		}

		for (i=0; i < eaSize(&pTimePeriod->ppProjLaunchCounts); i++)
		{
			stashIntAddPointer(pTimePeriod->hProjLaunchCountsByID, pTimePeriod->ppProjLaunchCounts[i]->iProjID,
				pTimePeriod->ppProjLaunchCounts[i], true);
		}

		eaDestroy(&pTimePeriod->ppProjLaunchCounts);
	}
}

int SortLaunchCountsByCount(const ProjLaunchCount **pCount1, const ProjLaunchCount **pCount2)
{
	if ((*pCount1)->iLaunchCount > (*pCount2)->iLaunchCount)
	{
		return -1;
	}
	else if ((*pCount1)->iLaunchCount < (*pCount2)->iLaunchCount)
	{
		return 1;
	}
	else
	{
		return 0;
	}
}

void TimePeriod_StashTableToTempList(SingleTimePeriodCountTracker *pTimePeriod)
{
	eaDestroy(&pTimePeriod->ppProjLaunchCounts);

		FOR_EACH_IN_STASHTABLE(pTimePeriod->hProjLaunchCountsByID, ProjLaunchCount, pSingleLaunchCount)
		{
			eaPush(&pTimePeriod->ppProjLaunchCounts, pSingleLaunchCount);
		}
		FOR_EACH_END

		eaQSort(pTimePeriod->ppProjLaunchCounts, SortLaunchCountsByCount);

		if (eaSize(&pTimePeriod->ppProjLaunchCounts) > siProjectsPerTimePeriodToSave)
		{
			eaSetSize(&pTimePeriod->ppProjLaunchCounts, siProjectsPerTimePeriodToSave);
		}

}

void MultipleTimePeriods_StashTablesToTempLists(MultipleTimePeriods *pMultipleTimePeriods)
{
	int iWhichTimePeriod;
	for (iWhichTimePeriod = 0; iWhichTimePeriod < eaSize(&pMultipleTimePeriods->ppTimePeriods); iWhichTimePeriod++)
	{
		TimePeriod_StashTableToTempList( pMultipleTimePeriods->ppTimePeriods[iWhichTimePeriod]);		
	}
}


void TimePeriod_ClearTempLists(SingleTimePeriodCountTracker *pTimePeriod)
{
	eaDestroy(&pTimePeriod->ppProjLaunchCounts);
}

void MultipleTimePeriods_ClearTempLists(MultipleTimePeriods *pMultipleTimePeriods)
{	
	int iWhichTimePeriod;
	for (iWhichTimePeriod = 0; iWhichTimePeriod < eaSize(&pMultipleTimePeriods->ppTimePeriods); iWhichTimePeriod++)
	{
		TimePeriod_ClearTempLists(pMultipleTimePeriods->ppTimePeriods[iWhichTimePeriod]);
	}
}

void SendListToSearchManagerFailureCB(void *pUserData1, void *pUserData2)
{
	ea32FindAndRemoveFast(&spUGCSearchManagerIDs, (U32)((intptr_t)(pUserData1)));
}

void SendListToSearchManagers(void)
{
	int i;

	for (i=0; i < ea32Size(&spUGCSearchManagerIDs); i++)
	{
		RemoteCommand_SendWhatsHotListToSearchManager(GLOBALTYPE_UGCSEARCHMANAGER, spUGCSearchManagerIDs[i],
			&sWhatsHotList, SendListToSearchManagerFailureCB, (void*)((intptr_t)(spUGCSearchManagerIDs[i])), NULL);

	}
}
		
AUTO_COMMAND_REMOTE ACMD_PACKETERRORCALLBACK;
void RequestWhatsHotList(ContainerID iUGCSearchManagerID)
{
	ea32Push(&spUGCSearchManagerIDs, iUGCSearchManagerID);

	RemoteCommand_SendWhatsHotListToSearchManager(GLOBALTYPE_UGCSEARCHMANAGER, iUGCSearchManagerID,
		&sWhatsHotList, SendListToSearchManagerFailureCB, (void*)((intptr_t)(iUGCSearchManagerID)), NULL);
}


void UpdateDBCopyOfMultipleTimePeriods(void)
{
	char *pStr = NULL;

	MultipleTimePeriods_StashTablesToTempLists(spMultipleTimePeriods);
	ParserWriteText(&pStr, parse_MultipleTimePeriods, spMultipleTimePeriods, 0, 0, 0);
	MultipleTimePeriods_ClearTempLists(spMultipleTimePeriods);

	PrefStore_SetString( MULTIPLETIMEPERIODS_DB_NAME, pStr, NULL, NULL);

	estrDestroy(&pStr);
}
void aslUGCDataManagerWhatsHot_PeriodicUpdate(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	U32 iCurTime = timeSecondsSince2000();
	
	LazyCreateTimePeriods();

	if (spCurrentTimePeriod->iEndingTime <= iCurTime || userData)
	{
		U32 iCutoffTime;
		eaPush(&spMultipleTimePeriods->ppTimePeriods, spCurrentTimePeriod);
		spCurrentTimePeriod = NULL;
		LazyCreateTimePeriods();

		iCutoffTime = iCurTime - sWhatsHotTimePeriod * iWhatsHotOldTimePeriodsToSave;

		while (eaSize(&spMultipleTimePeriods->ppTimePeriods) && spMultipleTimePeriods->ppTimePeriods[0]->iEndingTime < iCutoffTime)
		{
			StructDestroy(parse_SingleTimePeriodCountTracker, spMultipleTimePeriods->ppTimePeriods[0]);
			eaRemove(&spMultipleTimePeriods->ppTimePeriods, 0);
		}

		UpdateDBCopyOfMultipleTimePeriods();
		WhatsHotList_CalculateFromMultipleTimePeriods(&sWhatsHotList, spMultipleTimePeriods);
		SendListToSearchManagers();
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ReportUGCProjectWasPlayedForWhatsHot(U32 iID)
{
	LazyCreateTimePeriods();

	SingleTimePeriodCountTracker_AddCount(spCurrentTimePeriod, iID, 1);

	AutoTrans_trUpdateProjectPlayTime(NULL, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iID);
}

static bool sbInitComplete = false;

bool aslUGCDataManagerWhatsHot_InitComplete(void)
{
	return sbInitComplete;
}

void GetMultipleTimePeriods_CB(bool bSucceeded, char *pRetVal, void *pUserData)
{
	if (!bSucceeded)
	{
		if (isProductionMode())
		{
			CRITICAL_NETOPS_ALERT("NO_WHATSHOT_READY", "The map manager could not read its stored WhatsHot data on the object DB. It is normal for this alert to happen ONCE the first time a shard runs with this code, but if you see it after that, something is wrong");
		}
		LazyCreateTimePeriods();
		UpdateDBCopyOfMultipleTimePeriods();
		sbInitComplete = true;	
		return;
	}


	sbInitComplete = true;
	if (!pRetVal || !pRetVal[0])
	{
		if (isProductionMode())
		{
			CRITICAL_NETOPS_ALERT("NO_WHATSHOT_READY", "The map manager could not read its stored WhatsHot data on the object DB. It is normal for this alert to happen ONCE the first time a shard runs with this code, but if you see it after that, something is wrong");
		}
		LazyCreateTimePeriods();
		UpdateDBCopyOfMultipleTimePeriods();
	}
	else
	{
		int iResult;
		spMultipleTimePeriods = StructCreate(parse_MultipleTimePeriods);
		iResult = ParserReadText(pRetVal, parse_MultipleTimePeriods, spMultipleTimePeriods, 0);
		MultipleTimePeriods_ListsToStashTables(spMultipleTimePeriods);
		WhatsHotList_CalculateFromMultipleTimePeriods(&sWhatsHotList, spMultipleTimePeriods);
		SendListToSearchManagers();
	}

}




AUTO_COMMAND;
void WhatsHotTest(void)
{
	U32 iRandomID;
	int iRandomCount;
	int i;

	for (i=0; i < 5; i++)
	{
		iRandomCount = randomIntRange(1,5);

		iRandomID = randomIntRange(20,100);
		
		printf("about to pretend that map %d was played %d times\n", iRandomID, iRandomCount);

		for ( ; iRandomCount > 0; iRandomCount--)
		{
			aslUGCDataManager_ReportUGCProjectWasPlayedForWhatsHot(iRandomID);
		}
	}

	aslUGCDataManagerWhatsHot_PeriodicUpdate(NULL, 0, (UserData)0x1);

}

AUTO_COMMAND ACMD_CATEGORY(UGC);
void ForceWhatsHotUpdate(void)
{
	aslUGCDataManagerWhatsHot_PeriodicUpdate(NULL, 0, (void*)1);
}


AUTO_COMMAND ACMD_CATEGORY(UGC);
char *AddProjectToWhatsHot(char *pIDString, int iArtificialPlayCount)
{
	ContainerID iID;
	bool bIsSeries;
	SingleTimePeriodCountTracker *pTimePeriod;

	if (!UGCIDString_StringToInt(pIDString, &iID, &bIsSeries) || bIsSeries)
	{
		if (!StringToUint(pIDString, &iID))
		{
			return "Invalid project ID";
		}
	}

	LazyCreateTimePeriods();

	if (!eaSize(&spMultipleTimePeriods->ppTimePeriods))
	{
		pTimePeriod = StructCreate(parse_SingleTimePeriodCountTracker);
		pTimePeriod->iEndingTime = spCurrentTimePeriod->iStartingTime;
		pTimePeriod->iStartingTime = pTimePeriod->iEndingTime - sWhatsHotTimePeriod;
		eaPush(&spMultipleTimePeriods->ppTimePeriods, pTimePeriod);
	}
	else
	{
		pTimePeriod = eaTail(&spMultipleTimePeriods->ppTimePeriods);
	}

	SingleTimePeriodCountTracker_AddCount(pTimePeriod, iID, iArtificialPlayCount);

	UpdateDBCopyOfMultipleTimePeriods();
	WhatsHotList_CalculateFromMultipleTimePeriods(&sWhatsHotList, spMultipleTimePeriods);
	SendListToSearchManagers();

	return "Whats Hot updated";
}


AUTO_STRUCT;
typedef struct WhatsHotSummaryForHTML
{
	char *pString; AST(ESTRING FORMATSTRING(HTML_NO_HEADER = 1, HTML_PREFORMATTED = 1))
} WhatsHotSummaryForHTML;

static int siWhatsHotSummaryLength = 5;
AUTO_CMD_INT(siWhatsHotSummaryLength, WhatsHotSummaryLength) ACMD_CATEGORY(UGC);

void DumpTimePeriodIntoFriendlyEString(SingleTimePeriodCountTracker *pTimePeriod, char **ppEString)
{
	char *pStartTime = strdup(timeGetLocalDateStringFromSecondsSince2000(pTimePeriod->iStartingTime));
	char *pEndTime = strdup(timeGetLocalDateStringFromSecondsSince2000(pTimePeriod->iEndingTime));
	int iNumToShow;
	int i;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	iNumToShow = MIN(siWhatsHotSummaryLength, eaSize(&pTimePeriod->ppProjLaunchCounts));

	estrConcatf(ppEString, "WhatsHot summary for missions played between %s and %s\n------------\n", pStartTime, pEndTime);

	if (pTimePeriod->ppProjLaunchCounts)
	{
		estrConcatf(ppEString, "  %d distinct missions were played. Top %d\n", eaSize(&pTimePeriod->ppProjLaunchCounts), iNumToShow);
		for (i=0; i < iNumToShow; i++)
		{
			UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, pTimePeriod->ppProjLaunchCounts[i]->iProjID);
			const UGCProjectVersion *pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

			if (!pProject)
			{
				estrConcatf(ppEString, "    %d plays: Project %u, which no longer seems to exist\n", pTimePeriod->ppProjLaunchCounts[i]->iLaunchCount, pTimePeriod->ppProjLaunchCounts[i]->iProjID);
			}
			else
			{
				estrConcatf(ppEString, "    %d plays: %s (%s) by %s\n",
							pTimePeriod->ppProjLaunchCounts[i]->iLaunchCount, pProject->pIDString, UGCProject_GetVersionName(pProject, pVersion), pProject->pOwnerAccountName);
			}
		}
	}
	else
	{
		estrConcatf(ppEString, "  No missions played turing this time period\n");
	}

	free(pStartTime);
	free(pEndTime);
}

void FillInWhatsHotSummary(WhatsHotSummaryForHTML *pSummary)
{
	int i;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	StructReset(parse_WhatsHotSummaryForHTML, pSummary);

	if (!eaSize(&spMultipleTimePeriods->ppTimePeriods) && !spCurrentTimePeriod)
	{
		estrPrintf(&pSummary->pString, "Whats Hot list is totally empty");
		return;
	}

	MultipleTimePeriods_StashTablesToTempLists(spMultipleTimePeriods);

	for (i=eaSize(&spMultipleTimePeriods->ppTimePeriods)-1; i >= 0; i--)
	{
		DumpTimePeriodIntoFriendlyEString(spMultipleTimePeriods->ppTimePeriods[i], &pSummary->pString);
	}

	MultipleTimePeriods_ClearTempLists(spMultipleTimePeriods);

	if (spCurrentTimePeriod)
	{
		TimePeriod_StashTableToTempList(spCurrentTimePeriod);
		estrConcatf(&pSummary->pString, "CURRENT::: ");
		DumpTimePeriodIntoFriendlyEString(spCurrentTimePeriod, &pSummary->pString);
		TimePeriod_ClearTempLists(spCurrentTimePeriod);
	}

	estrConcatf(&pSummary->pString, "\n\nSummarized What's Hot List:\n");
	for (i=0; i < siWhatsHotSummaryLength && i < ea32Size(&sWhatsHotList.pProjectIDs); i++)
	{
		UGCProject *pProject = objGetContainerData(GLOBALTYPE_UGCPROJECT, sWhatsHotList.pProjectIDs[i]);
		const UGCProjectVersion *pVersion = UGCProject_GetMostRecentPublishedVersion(pProject);

		if (!pProject)
		{
			estrConcatf(&pSummary->pString, "  %d: Project %u, which no longer seems to exist\n", i+1, sWhatsHotList.pProjectIDs[i]);
		}
		else
		{
			estrConcatf(&pSummary->pString, "  %d: %s (%s) by %s\n",
						i + 1, pProject->pIDString, UGCProject_GetVersionName(pProject, pVersion), pProject->pOwnerAccountName);
		}
	}
}




bool GetWhatsHotOverviewForHttp(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	static WhatsHotSummaryForHTML sSummary = {0};
	static U32 sLastTime = 0;
	U32 iCurTime = timeSecondsSince2000();
	bool bRetVal;

	if (sLastTime < iCurTime - 5)
	{
		FillInWhatsHotSummary(&sSummary);
		sLastTime = iCurTime;
	}

    bRetVal =  ProcessStructIntoStructInfoForHttp(pLocalXPath, pArgList,
		&sSummary, parse_WhatsHotSummaryForHTML, iAccessLevel, 0, pStructInfo, eFlags);

    return bRetVal;
}


void aslUGCDataManagerWhatsHot_Init(void)
{
	RegisterCustomXPathDomain(".WhatsHOT", GetWhatsHotOverviewForHttp, NULL);

	PrefStore_GetString( MULTIPLETIMEPERIODS_DB_NAME, GetMultipleTimePeriods_CB, NULL);
}















#include "aslUGCDataManagerWhatsHot_c_ast.c"
#include "aslMapManagerPub_h_ast.c"
