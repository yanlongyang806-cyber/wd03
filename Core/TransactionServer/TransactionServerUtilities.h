#pragma once

AUTO_STRUCT;
typedef struct NamedTransactionCount
{
	const char *pTransName; AST(POOL_STRING)
	int iCount;
} NamedTransactionCount;

//structCreates NamedTransactionCounts, then adds them to list, sorted with most common first
void CountTransactionsByName(TransactionServer *pServer, NamedTransactionCount ***pppList);

void PrintTopTransactions(void);

void DumpTransCounts(TransactionServer *pServer);

//used to track info about all occurrences of each specifically-named transaction
AUTO_STRUCT AST_FORMATSTRING(HTML_DEF_FIELDS_TO_SHOW="TransName, NumBegun, NumActive, NumRunAsLocal, TimesWasBlocked");
typedef struct TransactionTracker
{
	const char *pTransName; AST(POOL_STRING, KEY)

	bool bDoVerboseLogging;

	U64 iNumBegun;
	U64 iNumActive;
	U64 iNumSucceeded;
	U64 iNumFailedOrOther;

	U64 iTimesWasBlocked;
	U64 iTimesBlockedOther;
	U64 iTimesBlockedSelf;
	
	U64 iObjectDBUpdateBytes;

	U64 iRecentTotal;
	U64 i1Second;
	U64 i2Seconds;
	U64 i4Seconds;
	U64 i8Seconds;
	U64 i16Seconds;
	U64 i32Seconds;
	U64 iMoreThan32Seconds;

	U64 iNumRunAsLocal;

	U64 iLastTimeVerboseLogged; NO_AST

	U64 iNextActiveAlertingCutoff; NO_AST

	U32 iLastFailedLogTime;

	//number of times that RunAutoTransCB has taken an excessive amount of time on a member server
	U32 iSlowCallbacksOnServer;

	char *pMostRecentLoggedFailString; AST(ESTRING)

	char *pVerboseLog_Complete; AST(ESTRING, FORMATSTRING(html=1))
	U64 iCompleteVerboseLogLifespan;

	char *pVerboseLog_Incomplete; AST(ESTRING, FORMATSTRING(html=1))

	TimedCallback *pAlertingCallback; NO_AST//if true, then our num active transactions has gone above our iNextActiveAlertingCutoff. If this
		//remains true for 10 seconds, our callback will be called which will trigger an alert and double the cutoff

	AST_COMMAND("Kill all old", "KillOldTransactions $FIELD(TransName) $CONFIRM(Really kill all $FIELD(TransName) transactions older than 5 seconds?)")
	AST_COMMAND("Reset time counts", "ResetTimeCounts $FIELD(TransName) $CONFIRM(Really reset the 1second, 2second, etc. counts for $FIELD(TransName)?)")
	AST_COMMAND("Begin verbose logging", "BeginVerboseLogging $FIELD(TransName) $CONFIRM(Really begin verbosely logging this transaction?) $NORETURN", "$FIELD(DoVerboseLogging) = 0")
	AST_COMMAND("End verbose logging", "EndVerboseLogging $FIELD(TransName) $CONFIRM(Really end verbosely logging this transaction?) $NORETURN", "$FIELD(DoVerboseLogging) = 1")
	AST_COMMAND("Clear verbose logs", "ClearVerboseLogs $FIELD(TransName) $CONFIRM(Clear the local verbose logs so they can be re-gotten?) $NORETURN")
} TransactionTracker;



//stuff relating to TransactionTrackers
TransactionTracker *FindTransactionTrackerEx(TransactionServer *pServer, const char *pTransName, bool bNewOnebegan, const char *pFile, int iLine);

#define FindTransactionTracker(pServer, pTransName, bNewOneBegan) FindTransactionTrackerEx(pServer, pTransName, bNewOneBegan, __FILE__, __LINE__)


static __forceinline void ReportOutcomeToTracker(TransactionTracker *pTracker, enumTransactionOutcome eResult)
{
	if (eResult == TRANSACTION_OUTCOME_SUCCESS)
	{
		pTracker->iNumSucceeded++;
	}
	else
	{
		pTracker->iNumFailedOrOther++;
	}

	

}

static __forceinline void ReportCleanupToTracker(TransactionTracker *pTracker, U32 iMilliSeconds)
{
	pTracker->iNumActive--;

	if (pTracker->pAlertingCallback && pTracker->iNumActive < pTracker->iNextActiveAlertingCutoff)
	{
		TimedCallback_Remove(pTracker->pAlertingCallback);
		pTracker->pAlertingCallback = NULL;
	}

	pTracker->iRecentTotal++;
	if (iMilliSeconds < 2000)
	{
		if (iMilliSeconds < 1000)
		{
			pTracker->i1Second++;
		}
		else
		{
			pTracker->i2Seconds++;
		}
	}
	else
	{
		if (iMilliSeconds < 8000)
		{
			if (iMilliSeconds < 4000)
			{
				pTracker->i4Seconds++;
			}
			else
			{
				pTracker->i8Seconds++;
			}
		}
		else
		{
			if (iMilliSeconds < 16000)
			{
				pTracker->i16Seconds++;
			}
			else if (iMilliSeconds < 32000)
			{
				pTracker->i32Seconds++;
			}
			else
			{
				pTracker->iMoreThan32Seconds++;
			}
		}
	}

}

//tracks which version mismatch alerts have already been sent do avoid duplication
bool VersionMismatchAlreadyReported(char *pVersionString, GlobalType eContainerType);
void VersionMismatchReported(char *pVersionString, GlobalType eContainerType);