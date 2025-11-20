#include "Transactionserver.h"
#include "StashTable.h"
#include "TransactionServerUtilities.h"
#include "StringCache.h"
#include "TextParser.h"
#include "TransactionServerUtilities_h_ast.h"
#include "earray.h"
#include "logging.h"
#include "alerts.h"
#include "GlobalTYpes.h"
#include "TransactionServer_h_ast.h"
#include "timing.h"
#include "estring.h"
#include "TimedCallback.h"

extern TransactionServer gTransactionServer;

int sortNamedTransactions(const NamedTransactionCount **pCount1, const NamedTransactionCount **pCount2)
{
	if ((*pCount1)->iCount < (*pCount2)->iCount)
	{
		return 1;
	}
	else if ((*pCount1)->iCount > (*pCount2)->iCount)
	{
		return -1;
	}
	else
	{
		return 0;
	}
}

void CountTransactionsByName(TransactionServer *pServer, NamedTransactionCount ***pppList)
{
	StashTable sTransactionsByName = stashTableCreateAddress(64);
	StashTableIterator stashIterator;
	StashElement element;

	int i;

	for (i=0; i < gMaxTransactions; i++)
	{
		if (pServer->pTransactions[i].eType != TRANS_TYPE_NONE)
		{
			NamedTransactionCount *pCount;

			const char *pPooledName = allocAddString(pServer->pTransactions[i].pTransactionName);

			if (stashFindPointer(sTransactionsByName, pPooledName, &pCount))
			{
				pCount->iCount++;
			}
			else
			{
				pCount = StructCreate(parse_NamedTransactionCount);
				pCount->pTransName = pPooledName;
				pCount->iCount = 1;
				stashAddPointer(sTransactionsByName, pPooledName, pCount, false);
			}
		}
	}

	eaDestroy(pppList);

	//need to iterate through all transactions in our transTable and possible abort them
	stashGetIterator(sTransactionsByName, &stashIterator);

	while (stashGetNextElement(&stashIterator, &element))
	{
		NamedTransactionCount *pCount = stashElementGetPointer(element);
		eaPush(pppList, pCount);
	}

	stashTableDestroy(sTransactionsByName);
	
	eaQSort(*pppList, sortNamedTransactions);

}

void PrintTopTransactions(void)
{
	NamedTransactionCount **ppList = NULL;
	int i;

	CountTransactionsByName(&gTransactionServer, &ppList);

	if (eaSize(&ppList) == 0)
	{
		printf("No transactions\n");
	}
	else
	{
		printf("Top transactions:\n");
	}

	for (i=0; i < 10 && i < eaSize(&ppList); i++)
	{
		printf("%s: %d\n", ppList[i]->pTransName, ppList[i]->iCount);
	}

	eaDestroyStruct(&ppList, parse_NamedTransactionCount);


}



void DumpTransCounts(TransactionServer *pServer)
{
	StashTableIterator iterator;
	StashElement element;
	
	PERFINFO_AUTO_START_FUNC();

	stashGetIterator(pServer->sTrackersByTransName, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		TransactionTracker *pTracker = stashElementGetPointer(element);

		if (pTracker->iRecentTotal)
		{
			log_printf(LOG_TRANSSUMMARY, "TransName %s  total %"FORM_LL"d  1Sec %"FORM_LL"d  2sec %"FORM_LL"d  4sec %"FORM_LL"d  8sec %"FORM_LL"d  16sec %"FORM_LL"d  32sec %"FORM_LL"d  long %"FORM_LL"d",
				(char*)stashElementGetKey(element), pTracker->iRecentTotal, pTracker->i1Second, pTracker->i2Seconds, pTracker->i4Seconds, pTracker->i8Seconds, pTracker->i16Seconds, pTracker->i32Seconds, pTracker->iMoreThan32Seconds);
		}
	}
	
	PERFINFO_AUTO_STOP();
}

//for the "There have been %"FORM_LL"d active transactions of type %s continuously for %f seconds" alert right below
int gTooManyContinuouslyAlertCutoff = 8192;
AUTO_CMD_INT(gTooManyContinuouslyAlertCutoff, TooManyContinuouslyAlertCutoff);

float gTooManyContinuouslyAlertDelay = 60.0f;
AUTO_CMD_FLOAT(gTooManyContinuouslyAlertDelay, TooManyContinuouslyAlertDelay);

void TransactionTrackerAlertCB(TimedCallback *callback, F32 timeSinceLastCallback, TransactionTracker *pTracker)
{
	pTracker->pAlertingCallback = NULL;
	TriggerAlertf("TOO_MANY_ACTIVE_TRANS", ALERTLEVEL_CRITICAL, ALERTCATEGORY_NETOPS, 0, 
		GetAppGlobalType(), GetAppGlobalID(), GetAppGlobalType(), GetAppGlobalID(), getHostName(),0, 
		"There have been %"FORM_LL"d active transactions of type %s continuously for %f seconds. Something is likely wrong. Look at transactionTrackers in the Trans Server servermonitor",
		pTracker->iNextActiveAlertingCutoff, pTracker->pTransName, gTooManyContinuouslyAlertDelay);
	pTracker->iNextActiveAlertingCutoff *= 2;
}


TransactionTracker *FindTransactionTrackerEx(TransactionServer *pServer, const char *pTransName, bool bNewOnebegan, const char *pFileName, int iLineNum)
{
	TransactionTracker *pRetVal;

	if (stashFindPointer(pServer->sTrackersByTransName, pTransName, &pRetVal))
	{
		if (bNewOnebegan)
		{
			pRetVal->iNumActive++;
			pRetVal->iNumBegun++;
			if (pRetVal->iNumActive >= pRetVal->iNextActiveAlertingCutoff && !pRetVal->pAlertingCallback)
			{
				pRetVal->pAlertingCallback = TimedCallback_Run(TransactionTrackerAlertCB, pRetVal, gTooManyContinuouslyAlertDelay);
			}
		}
		return pRetVal;
	}

	if (strchr(pTransName, ' '))
	{
		TriggerAlertf("BAD_TRANS_NAME_FOR_TRACKER", ALERTLEVEL_WARNING, ALERTCATEGORY_PROGRAMMER, 0, 0, 0, 0, 0, NULL, 0,
			"Called from %s(%d), trans tracker being created for invalid trans name %s",
			pFileName, iLineNum, pTransName);
	}

	pRetVal = StructCreate(parse_TransactionTracker);
	pRetVal->pTransName = pTransName;
	if (bNewOnebegan)
	{
		pRetVal->iNumActive = pRetVal->iNumBegun = 1;
	}
	pRetVal->iNextActiveAlertingCutoff = gTooManyContinuouslyAlertCutoff;
	stashAddPointer(pServer->sTrackersByTransName, pTransName, pRetVal, true);
	return pRetVal;
}

char *KillOldTransactions(char *pTransactionName_in);

void KillOldTransactionsCB(TimedCallback *callback, F32 timeSinceLastCallback, UserData userData)
{
	KillOldTransactions((char*)userData);
}


#define MAX_TO_KILL_AT_ONCE 5000

AUTO_COMMAND;
char *KillOldTransactions(char *pTransactionName_in)
{
	S64 iStartingTicks = timerCpuTicks64();
	U64 iTimeCutoff = timeMsecsSince2000() - 5000;

	const char *pPoolName = allocAddString(pTransactionName_in);

	int iKilledCount = 0;
	int iNotKilledCount = 0;
	static char *pResultString = NULL;
	int i;
	TransactionTracker *pTracker;
	bool bDidCallback = false;
 

	for (i=0; i < gMaxTransactions; i++)
	{
		if (gTransactionServer.pTransactions[i].eType != TRANS_TYPE_NONE)
		{
			if (gTransactionServer.pTransactions[i].pTransactionName == pPoolName)
			{
				int j;
				Transaction *pTransactionToKill = &gTransactionServer.pTransactions[i];

				if (pTransactionToKill->iTimeBegan < iTimeCutoff)
				{

					iKilledCount++;

					if (iKilledCount == 10)
					{
						log_printf(LOG_VERBOSETRANS, "Killing potentially very may transactions... temporarily disabling verbose transaction logging");
						gbNoVerboseLogging = true;
					}

					for (j=0; j < pTransactionToKill->iNumBaseTransactions; j++)
					{
						AbortBaseTransaction(&gTransactionServer, pTransactionToKill, j, "KillOldTransactions");
						
						//check if aborting the base transaction caused the entire transaction to be finished
						if (pTransactionToKill->eType == TRANS_TYPE_NONE)
						{
							break;
						}
					}

					if (iKilledCount == MAX_TO_KILL_AT_ONCE)
					{
						TimedCallback_Run(KillOldTransactionsCB, (void*)pPoolName, 0.5f);
						bDidCallback = true;
						break;

					}
				}
				else
				{
					iNotKilledCount++;
				}
			}
		}
	}
		
	gbNoVerboseLogging = false;


	estrPrintf(&pResultString, "Killing <%s> transactions that were > 5 seconds old. %d killed. %d not killed. Time to complete: %f seconds.",
		pPoolName, iKilledCount, iNotKilledCount, timerSeconds64(timerCpuTicks64() - iStartingTicks));

	if (iKilledCount == MAX_TO_KILL_AT_ONCE)
	{
		estrConcatf(&pResultString, " Because %d transacions were killed, stopping killing for now to avoid stall... will kill more in 0.1 seconds",
			MAX_TO_KILL_AT_ONCE);
	}

	log_printf(LOG_VERBOSETRANS, "%s", pResultString);


	if (!bDidCallback)
	{
		stashFindPointer(gTransactionServer.sTrackersByTransName, pPoolName, &pTracker);
		if (pTracker)
		{
			pTracker->iNextActiveAlertingCutoff /= 2;
			if (pTracker->iNextActiveAlertingCutoff < gTooManyContinuouslyAlertCutoff)
			{
				pTracker->iNextActiveAlertingCutoff = gTooManyContinuouslyAlertCutoff;
			}
		}
	}

	return pResultString;

}



AUTO_COMMAND;
char *ResetTimeCounts(char *pTransactionName_in)
{
	const char *pPoolName = allocAddString(pTransactionName_in);
	TransactionTracker *pTracker;

	stashFindPointer(gTransactionServer.sTrackersByTransName, pPoolName, &pTracker);
	if (!pTracker)
	{
		return "Transaction not found";
	}

	pTracker->iRecentTotal = 0;
	pTracker->i1Second = 0;
	pTracker->i2Seconds = 0;
	pTracker->i4Seconds = 0;
	pTracker->i8Seconds = 0;
	pTracker->i16Seconds = 0;
	pTracker->i32Seconds = 0;
	pTracker->iMoreThan32Seconds = 0;

	return "Reset";
}


AUTO_COMMAND;
void ResetAllRecentTimeCounts(void)
{
	StashTableIterator iterator;
	StashElement element;
	stashGetIterator(gTransactionServer.sTrackersByTransName, &iterator);

	while (stashGetNextElement(&iterator, &element))
	{
		TransactionTracker *pTracker = stashElementGetPointer(element);

		pTracker->iRecentTotal = 0;
		pTracker->i1Second = 0;
		pTracker->i2Seconds = 0;
		pTracker->i4Seconds = 0;
		pTracker->i8Seconds = 0;
		pTracker->i16Seconds = 0;
		pTracker->i32Seconds = 0;
		pTracker->iMoreThan32Seconds = 0;
	}
}

//note that this code is precisely duplicated in Controller_Utils.c
StashTable sVersionMismatches[GLOBALTYPE_MAX] = {0};

bool VersionMismatchAlreadyReported(char *pVersionString, GlobalType eContainerType)
{
	if (!sVersionMismatches[eContainerType])
	{
		return false;
	}

	return stashFindPointer(sVersionMismatches[eContainerType], pVersionString, NULL);
}

void VersionMismatchReported(char *pVersionString, GlobalType eContainerType)
{
	if (!sVersionMismatches[eContainerType])
	{
		sVersionMismatches[eContainerType] = stashTableCreateWithStringKeys(4, StashDeepCopyKeys);
	}

	stashAddPointer(sVersionMismatches[eContainerType], pVersionString, NULL, true);
}


#include "TransactionServerUtilities_h_ast.c"
