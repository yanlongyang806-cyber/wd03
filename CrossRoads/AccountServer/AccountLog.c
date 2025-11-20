#include "AccountLog.h"
#include "AccountServer.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "EString.h"
#include "logging.h"
#include "objTransactions.h"
#include "timing.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "AccountManagement.h"
#include "HttpXpathSupport.h"
#include "ThreadSafeMemoryPool.h"

#include "AccountLog_c_ast.h"

// Whether or not to redirect account logs to log files instead of being stored in the DB
static bool gbAccountLogRedirect = false;
AUTO_CMD_INT(gbAccountLogRedirect, AccountLogRedirect) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// The size of the account log buffer
static int giAccountLogBufferSize = 0;
AUTO_CMD_INT(giAccountLogBufferSize, AccountLogBufferSize) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

// The size of the account log batch
static int giAccountLogBatchSize = 400;
AUTO_CMD_INT(giAccountLogBatchSize, AccountLogBatchSize) ACMD_CMDLINE ACMD_CATEGORY(Account_Server);

//static ContainerID *geaiMigratingAccountIDs = NULL;

TSMP_DEFINE(AccountLogEntry);

void initAccountLogEntryMempool(void)
{
	TSMP_SMART_CREATE(AccountLogEntry, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_AccountLogEntry, &TSMP_NAME(AccountLogEntry));
}

static const AccountLogBatch *getAccountLogBatch(U32 uBatchID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, uBatchID);

	if (pContainer)
	{
		return pContainer->containerData;
	}

	return NULL;
}

static void repackAccountLogBatch(U32 uBatchID)
{
	Container *pContainer = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, uBatchID);

	if (pContainer)
	{
		objUnloadContainer(pContainer);
	}
}

static const AccountLogBatch *getMostRecentAccountLogBatch(const AccountInfo *pAccount)
{
	U32 uBatchID = 0;

	if (!eaiSize(&pAccount->eauIndexedLogBatchIDs))
	{
		return NULL;
	}

	uBatchID = pAccount->eauIndexedLogBatchIDs[eaiSize(&pAccount->eauIndexedLogBatchIDs)-1];
	return getAccountLogBatch(uBatchID);
}

bool isAccountUsingIndexedLogs(const AccountInfo *pAccount)
{
	return pAccount->uNextLogID != 0;
}

AUTO_TRANS_HELPER_SIMPLE;
NOCONST(AccountLogEntry) *trhAccountLog_MakeNewEntry(U32 uIndex, const char *pLogString)
{
	NOCONST(AccountLogEntry) *pEntry = StructCreateNoConst(parse_AccountLogEntry);

	pEntry->uID = uIndex;
	pEntry->uSecondsSince2000 = timeSecondsSince2000();
	estrCopy2(&pEntry->pMessage, pLogString);

	return pEntry;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppindexedlogbuffer[]")
ATR_LOCKS(pBatch, ".Eaindexedlogentries[]");
enumTransactionOutcome trAccountLog_MoveBufferEntryToBatch(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NOCONST(AccountLogBatch) *pBatch, U32 uIndex)
{
	NOCONST(AccountLogEntry) *pEntry = eaIndexedRemoveUsingInt(&pAccount->ppIndexedLogBuffer, uIndex);

	if (!pEntry)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (!eaIndexedPushUsingIntIfPossible(&pBatch->eaIndexedLogEntries, uIndex, pEntry))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Eauindexedlogbatchids");
enumTransactionOutcome trAccountLog_AddNewBatch(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uBatchID)
{
	eaiPush(&pAccount->eauIndexedLogBatchIDs, uBatchID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Eaurebucketlogbatchids");
enumTransactionOutcome trAccountLog_AddRebucketBatch(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uBatchID)
{
	eaiPush(&pAccount->eauRebucketLogBatchIDs, uBatchID);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Pptemporarylogbufferduringrebucketing[]");
enumTransactionOutcome trAccountLog_AddEntryDuringRebucketing(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uIndex, const char *pLogString)
{
	NOCONST(AccountLogEntry) *pEntry = trhAccountLog_MakeNewEntry(uIndex, pLogString);

	if (!eaIndexedPushUsingIntIfPossible(&pAccount->ppTemporaryLogBufferDuringRebucketing, uIndex, pEntry))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppindexedlogbuffer[], .Unextlogid");
enumTransactionOutcome trAccountLog_AddEntryToBuffer(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uIndex, const char *pLogString)
{
	NOCONST(AccountLogEntry) *pEntry = trhAccountLog_MakeNewEntry(uIndex, pLogString);

	if (!eaIndexedPushUsingIntIfPossible(&pAccount->ppIndexedLogBuffer, uIndex, pEntry))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	++pAccount->uNextLogID;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Unextlogid")
ATR_LOCKS(pBatch, ".Eaindexedlogentries[]");
enumTransactionOutcome trAccountLog_AddEntryToBatch(ATR_ARGS, NOCONST(AccountInfo) *pAccount, NOCONST(AccountLogBatch) *pBatch, U32 uIndex, const char *pLogString)
{
	NOCONST(AccountLogEntry) *pEntry = trhAccountLog_MakeNewEntry(uIndex, pLogString);

	if (!eaIndexedPushUsingIntIfPossible(&pBatch->eaIndexedLogEntries, uIndex, pEntry))
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	++pAccount->uNextLogID;
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pAccount, ".Eauindexedlogbatchids, .Eaurebucketlogbatchids");
void trhAccountLog_MoveRebucketBatchesToIndexed(ATH_ARG NOCONST(AccountInfo) *pAccount)
{
	pAccount->eauIndexedLogBatchIDs = pAccount->eauRebucketLogBatchIDs;
	pAccount->eauRebucketLogBatchIDs = NULL;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Eauindexedlogbatchids, .Eaurebucketlogbatchids, .Ppindexedlogbuffer, .Pptemporarylogbufferduringrebucketing, .Unextlogid, .flags");
enumTransactionOutcome trAccountLog_CommitRebucketing(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uNextLogID)
{
	assert(uNextLogID == pAccount->uNextLogID + eaSize(&pAccount->ppTemporaryLogBufferDuringRebucketing));

	// Destroy existing indexed batches, indexed buffer, and rebucketing buffer
	eaiDestroy(&pAccount->eauIndexedLogBatchIDs);
	eaDestroyStructNoConst(&pAccount->ppIndexedLogBuffer, parse_AccountLogEntry);
	eaDestroyStructNoConst(&pAccount->ppTemporaryLogBufferDuringRebucketing, parse_AccountLogEntry);

	// Move rebucket batches into indexed batches
	trhAccountLog_MoveRebucketBatchesToIndexed(pAccount);

	// Set next log ID and mark rebucketing complete
	pAccount->uNextLogID = uNextLogID;
	trhAccountSetFlags(pAccount, ACCOUNT_FLAG_LOGS_REBUCKETED);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Eaulogbatchids, .Eauindexedlogbatchids, .Eaurebucketlogbatchids, .Pplogentriesbuffer, .Pplogentries, .Pptemporarylogbufferduringrebucketing, .Unextlogid, .flags");
enumTransactionOutcome trAccountLog_CommitRebucketing_NonIndexed(ATR_ARGS, NOCONST(AccountInfo) *pAccount, U32 uNextLogID)
{
	// Destroy existing batches, buffer, loose logs, and rebucketing buffer
	eaiDestroy(&pAccount->eauLogBatchIDs);
	eaDestroyStructNoConst(&pAccount->ppLogEntriesBuffer, parse_AccountLogEntry);
	eaDestroyStructNoConst(&pAccount->ppLogEntries, parse_AccountLogEntry);
	eaDestroyStructNoConst(&pAccount->ppTemporaryLogBufferDuringRebucketing, parse_AccountLogEntry);

	// Move rebucket batches into indexed batches
	trhAccountLog_MoveRebucketBatchesToIndexed(pAccount);

	// Set next log ID and mark rebucketing complete
	pAccount->uNextLogID = uNextLogID;
	trhAccountSetFlags(pAccount, ACCOUNT_FLAG_LOGS_REBUCKETED);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Eaurebucketlogbatchids");
enumTransactionOutcome trAccountLog_ClearRebucketBatches(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	eaiDestroy(&pAccount->eauRebucketLogBatchIDs);
	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_STRUCT;
typedef struct AccountLogRebucketingStatus
{
	int iCurrentAccountRebucketing;
	bool bAlreadyIndexed;
	int iCurrentExistingBucket;
	int iCurrentExistingIndexInBucket;
	U32 uNextLogID;
} AccountLogRebucketingStatus;

enum {
	REBUCKET_INITIAL = -1,
	REBUCKET_LOOSE_LOGS = -2,
	REBUCKET_LOG_BUFFER = -3,
	REBUCKET_TEMP_BUFFER = -4,
	REBUCKET_COMPLETE = -5,
};

static UINT_EARRAY spAccountsToRebucket = NULL;
static AccountLogRebucketingStatus sAccountLogRebucketingStatus = {0, 0, REBUCKET_INITIAL, 0, 1};

// Deactivate rebucketing entirely
static bool sbDeactivateRebucketing = false;
AUTO_CMD_INT(sbDeactivateRebucketing, DeactivateRebucketing) ACMD_CMDLINE;

// How many frames to wait between rebucket ticks (i.e. creating new log containers)
static int siFramesPerRebucketTick = 1;
AUTO_CMD_INT(siFramesPerRebucketTick, FramesPerRebucketTick) ACMD_CMDLINE;

void queueAccountForRebucketing(const AccountInfo *pAccount)
{
	eaiPush(&spAccountsToRebucket, pAccount->uID);
}

bool isAccountCurrentlyRebucketing(const AccountInfo *pAccount)
{
	if (sAccountLogRebucketingStatus.iCurrentAccountRebucketing >= eaiSize(&spAccountsToRebucket))
	{
		return false;
	}

	return spAccountsToRebucket[sAccountLogRebucketingStatus.iCurrentAccountRebucketing] == pAccount->uID;
}

static bool isLogSourceFullyConsumed(CONST_EARRAY_OF(AccountLogEntry) ppLogSource)
{
	return (sAccountLogRebucketingStatus.iCurrentExistingIndexInBucket >= eaSize(&ppLogSource));
}

static void consumeLogFromSource(NOCONST(AccountLogBatch) *pBatch, CONST_EARRAY_OF(AccountLogEntry) ppLogSource)
{
	NOCONST(AccountLogEntry) *pNewEntry = StructCloneDeConst(parse_AccountLogEntry, ppLogSource[sAccountLogRebucketingStatus.iCurrentExistingIndexInBucket]);
	pNewEntry->uID = sAccountLogRebucketingStatus.uNextLogID++;
	eaPush(&pBatch->eaIndexedLogEntries, pNewEntry);
}

static void advanceLogRebucketing(int iSpecialNextBucket)
{
	if (iSpecialNextBucket)
	{
		sAccountLogRebucketingStatus.iCurrentExistingBucket = iSpecialNextBucket;
	}
	else
	{
		++sAccountLogRebucketingStatus.iCurrentExistingBucket;
	}

	sAccountLogRebucketingStatus.iCurrentExistingIndexInBucket = 0;
}

// Returns true if it consumes a log, false otherwise
static bool consumeFromSourceOrAdvance(NOCONST(AccountLogBatch) *pBatch, CONST_EARRAY_OF(AccountLogEntry) ppLogSource, int iSpecialNextBucket)
{
	if (isLogSourceFullyConsumed(ppLogSource))
	{
		advanceLogRebucketing(iSpecialNextBucket);
		return false;
	}

	consumeLogFromSource(pBatch, ppLogSource);
	return true;
}

static void commitRebucketing(const AccountInfo *pAccount)
{
	if (isAccountUsingIndexedLogs(pAccount))
	{
		// If the account is indexed already, then we need to clear out the indexed log batches
		EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauIndexedLogBatchIDs, iBatch, iNumBatches);
		{
			objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, pAccount->eauIndexedLogBatchIDs[iBatch]);
		}
		EARRAY_FOREACH_END;

		AutoTrans_trAccountLog_CommitRebucketing(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, sAccountLogRebucketingStatus.uNextLogID);
	}
	else
	{
		// If it's not indexed, then we need to clear out the normal log batches
		EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauLogBatchIDs, iBatch, iNumBatches);
		{
			objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, pAccount->eauLogBatchIDs[iBatch]);
		}
		EARRAY_FOREACH_END;

		AutoTrans_trAccountLog_CommitRebucketing_NonIndexed(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, sAccountLogRebucketingStatus.uNextLogID);
	}
}

void accountLogRebucketingTick(void)
{
	AccountInfo *pAccount = NULL;
	NOCONST(AccountLogBatch) newBatch = {0};
	static int iCounter = 0;

	if (sbDeactivateRebucketing || ++iCounter < siFramesPerRebucketTick)
	{
		return;
	}
	
	if (sAccountLogRebucketingStatus.iCurrentAccountRebucketing >= eaiSize(&spAccountsToRebucket))
	{
		if (sAccountLogRebucketingStatus.iCurrentAccountRebucketing > 0)
		{
			sAccountLogRebucketingStatus.iCurrentAccountRebucketing = 0;
			eaiDestroy(&spAccountsToRebucket);
		}

		return;
	}

	PERFINFO_AUTO_START_FUNC();
	coarseTimerAddInstance(NULL, __FUNCTION__);

	iCounter = 0;
	pAccount = findAccountByID(spAccountsToRebucket[sAccountLogRebucketingStatus.iCurrentAccountRebucketing]);
	
	if (sAccountLogRebucketingStatus.iCurrentExistingBucket == REBUCKET_INITIAL)
	{
		sAccountLogRebucketingStatus.bAlreadyIndexed = isAccountUsingIndexedLogs(pAccount);
		sAccountLogRebucketingStatus.iCurrentExistingBucket = 0;

		if (eaiSize(&pAccount->eauRebucketLogBatchIDs))
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauRebucketLogBatchIDs, iBatch, iNumBatches);
			{
				objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, pAccount->eauRebucketLogBatchIDs[iBatch]);
			}
			EARRAY_FOREACH_END;

			AutoTrans_trAccountLog_ClearRebucketBatches(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID);
			coarseTimerStopInstance(NULL, __FUNCTION__);
			PERFINFO_AUTO_STOP();
			return;
		}
	}

	// Accumulate entries until we hit 400 (or giAccountLogBatchSize), or we consume everything there is - whichever comes first
	while (eaSize(&newBatch.eaIndexedLogEntries) < giAccountLogBatchSize)
	{
		NOCONST(AccountLogEntry) *pNewEntry = NULL;

		if (sAccountLogRebucketingStatus.iCurrentExistingBucket == REBUCKET_TEMP_BUFFER)
		{
			// Consume from the temporary buffer until we exhaust it - then we're done with everything
			if (!consumeFromSourceOrAdvance(&newBatch, pAccount->ppTemporaryLogBufferDuringRebucketing, REBUCKET_COMPLETE))
				break;
		}
		else if (sAccountLogRebucketingStatus.iCurrentExistingBucket == REBUCKET_LOG_BUFFER)
		{
			// Consume from the appropriate log buffer for indexed and non-indexed accounts, then go to temp buffer
			if (sAccountLogRebucketingStatus.bAlreadyIndexed)
			{
				if (!consumeFromSourceOrAdvance(&newBatch, pAccount->ppIndexedLogBuffer, REBUCKET_TEMP_BUFFER))
					continue;
			}
			else
			{
				if (!consumeFromSourceOrAdvance(&newBatch, pAccount->ppLogEntriesBuffer, REBUCKET_TEMP_BUFFER))
					continue;
			}
		}
		else if (sAccountLogRebucketingStatus.iCurrentExistingBucket == REBUCKET_LOOSE_LOGS)
		{
			// Only possible if the account isn't indexed already - consume from loose entries, then go to log buffer
			if (!consumeFromSourceOrAdvance(&newBatch, pAccount->ppLogEntries, REBUCKET_LOG_BUFFER))
				continue;
		}
		else
		{
			const AccountLogBatch *pBatch = NULL;

			if (sAccountLogRebucketingStatus.bAlreadyIndexed)
			{
				// If the account is indexed, consume from indexed batches, then go to log buffer
				if (sAccountLogRebucketingStatus.iCurrentExistingBucket >= eaiSize(&pAccount->eauIndexedLogBatchIDs))
				{
					// Indexed accounts can't have loose logs, so skip straight to the buffer
					advanceLogRebucketing(REBUCKET_LOG_BUFFER);
					continue;
				}

				pBatch = getAccountLogBatch(pAccount->eauIndexedLogBatchIDs[sAccountLogRebucketingStatus.iCurrentExistingBucket]);
			}
			else
			{
				// If the account isn't indexed, consume from batches, then go to log buffer
				// If there are zero batches, then the account must have loose logs, so go to that first
				if (sAccountLogRebucketingStatus.iCurrentExistingBucket >= eaiSize(&pAccount->eauLogBatchIDs))
				{
					if (!eaiSize(&pAccount->eauLogBatchIDs))
					{
						advanceLogRebucketing(REBUCKET_LOOSE_LOGS);
						continue;
					}
					else
					{
						advanceLogRebucketing(REBUCKET_LOG_BUFFER);
						continue;
					}
				}

				pBatch = getAccountLogBatch(pAccount->eauLogBatchIDs[sAccountLogRebucketingStatus.iCurrentExistingBucket]);
			}

			if (eaSize(&pBatch->eaIndexedLogEntries))
			{
				if (!consumeFromSourceOrAdvance(&newBatch, pBatch->eaIndexedLogEntries, 0))
					continue;
			}
			else
			{
				if (!consumeFromSourceOrAdvance(&newBatch, pBatch->eaLogEntries, 0))
					continue;
			}
		}

		// If we made it here, we must have consumed an actual log - go to the next one
		++sAccountLogRebucketingStatus.iCurrentExistingIndexInBucket;
	}

	// Now, as long as we have a batch with N > 0 logs - create and add it
	if (eaSize(&newBatch.eaIndexedLogEntries) > 0)
	{
		newBatch.uBatchID = objReserveNewContainerID(objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH));
		newBatch.uAccountID = pAccount->uID;

		objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, &newBatch);
		assert(getAccountLogBatch(newBatch.uBatchID));
		repackAccountLogBatch(newBatch.uBatchID);
		AutoTrans_trAccountLog_AddRebucketBatch(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, newBatch.uBatchID);
		StructDeInitNoConst(parse_AccountLogBatch, &newBatch);
	}

	// If we're done rebucketing this account, commit it and advance to the next one
	// This has to happen right after the last bucket is created
	// If we delay, then a log might be written to the temporary buffer in the intervening frame, which would void our completeness guarantee
	if (sAccountLogRebucketingStatus.iCurrentExistingBucket == REBUCKET_COMPLETE)
	{
		commitRebucketing(pAccount);
		++sAccountLogRebucketingStatus.iCurrentAccountRebucketing;
		sAccountLogRebucketingStatus.iCurrentExistingBucket = REBUCKET_INITIAL;
		sAccountLogRebucketingStatus.iCurrentExistingIndexInBucket = 0;
		sAccountLogRebucketingStatus.uNextLogID = 1;
	}

	coarseTimerStopInstance(NULL, __FUNCTION__);
	PERFINFO_AUTO_STOP_FUNC();
}

AUTO_STRUCT;
typedef struct RebucketProgressForServerMon
{
	char *pStatus; AST(ESTRING)
	U32 uCurrentAccountBeingRebucketed;
	AccountLogRebucketingStatus *pRebucketStatus;
} RebucketProgressForServerMon;

static bool GetRebucketProgressForServerMon(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	RebucketProgressForServerMon progress = {0};
	bool bRetVal;
	int iNumRemaining = eaiSize(&spAccountsToRebucket);

	progress.pRebucketStatus = &sAccountLogRebucketingStatus;

	if (sAccountLogRebucketingStatus.iCurrentAccountRebucketing < eaiSize(&spAccountsToRebucket))
	{
		progress.uCurrentAccountBeingRebucketed = spAccountsToRebucket[sAccountLogRebucketingStatus.iCurrentAccountRebucketing];
	}

	if (iNumRemaining == 0)
	{
		estrPrintf(&progress.pStatus, "No rebucketing queued.");
	}
	else
	{
		if (sbDeactivateRebucketing)
		{
			estrAppend2(&progress.pStatus, "REBUCKETING DISABLED. ");
		}

		iNumRemaining -= sAccountLogRebucketingStatus.iCurrentAccountRebucketing;
		estrConcatf(&progress.pStatus, "Accounts waiting to be rebucketed: %d (%d total)", iNumRemaining, eaiSize(&spAccountsToRebucket));
	}

	bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList, &progress, parse_RebucketProgressForServerMon, iAccessLevel, 0, pStructInfo, eFlags);
	progress.pRebucketStatus = NULL;
	StructDeInit(parse_RebucketProgressForServerMon, &progress);
	return bRetVal;
}

AUTO_RUN;
void initAccountLogRebucketingHTTP(void)
{
	RegisterCustomXPathDomain(".rebucket", GetRebucketProgressForServerMon, NULL);
}

static const AccountLogBatch *ensureAvailableBatch(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	const AccountLogBatch *pBatch = getMostRecentAccountLogBatch(pAccount);

	// If there's no existing batch, or the batch exceeds the batch size limit, make a new one
	if (!pBatch || (giAccountLogBatchSize && eaSize(&pBatch->eaIndexedLogEntries) >= giAccountLogBatchSize))
	{
		NOCONST(AccountLogBatch) newBatch = {0};
		U32 uNewID = objReserveNewContainerID(objFindContainerStoreFromType(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH));

		assert(uNewID);
		newBatch.uBatchID = uNewID;
		newBatch.uAccountID = pAccount->uID;
		objRequestContainerCreateLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, &newBatch);

		pBatch = getAccountLogBatch(uNewID);
		assert(pBatch);

		AutoTrans_trAccountLog_AddNewBatch(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, uNewID);
	}

	return pBatch;
}

static void flushOneBufferedLog(SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	const AccountLogBatch *pBatch = NULL;
	const AccountLogEntry *pEntry = pAccount->ppIndexedLogBuffer[0];

	pBatch = ensureAvailableBatch(pAccount);
	AutoTrans_trAccountLog_MoveBufferEntryToBatch(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, pBatch->uBatchID, pEntry->uID);
}

static void addAccountLog(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PARAM_NN_STR const char *pLogString)
{
	U32 uNewLogID = 0;

	if (isAccountCurrentlyRebucketing(pAccount) || !isAccountUsingIndexedLogs(pAccount))
	{
		// If the account is being rebucketed right now, or it isn't indexed yet, dump it in the rebucketing buffer
		uNewLogID = eaSize(&pAccount->ppTemporaryLogBufferDuringRebucketing) + 1;
		AutoTrans_trAccountLog_AddEntryDuringRebucketing(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, uNewLogID, pLogString);
		return;
	}

	// If the account is indexed and isn't currently rebucketing, process it into the indexed log entries as normal

	// First check that the buffer doesn't exceed the maximum - if it does, pare it down until there's space for one, or it's empty
	while (eaSize(&pAccount->ppIndexedLogBuffer) > 0 && eaSize(&pAccount->ppIndexedLogBuffer) >= giAccountLogBufferSize)
	{
		flushOneBufferedLog(pAccount);
	}

	// Assign the new log the next available ID
	uNewLogID = pAccount->uNextLogID;

	if (eaSize(&pAccount->ppIndexedLogBuffer) < giAccountLogBufferSize)
	{
		// If there's room in the buffer, put it there
		AutoTrans_trAccountLog_AddEntryToBuffer(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, uNewLogID, pLogString);
	}
	else
	{
		// Otherwise, add it directly to the first available batch
		const AccountLogBatch *pBatch = ensureAvailableBatch(pAccount);
		assert(pBatch);
		AutoTrans_trAccountLog_AddEntryToBatch(NULL, GLOBALTYPE_ACCOUNTSERVER, GLOBALTYPE_ACCOUNT, pAccount->uID, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, pBatch->uBatchID, uNewLogID, pLogString);
	}
}

void accountLog(SA_PARAM_NN_VALID const AccountInfo *pAccount, FORMAT_STR const char * pFormat, ...)
{
	char *pFullString = NULL;
	char *pEscapedString = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Format account log string.
	estrGetVarArgs(&pFullString, pFormat);

	if (!pFullString)
	{
		estrDestroy(&pFullString);
		AssertOrAlert("ACCOUNTSERVER_INVALID_LOG_ENTRY", "Invalid message passed to accountLog. Could not log message.");
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	if (!pAccount->uID)
	{
		estrDestroy(&pFullString);
		AssertOrAlert("ACCOUNTSERVER_INVALID_ACCOUNT", "Invalid account passed to accountLog. Could not log message: %s", pFullString);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Create real logs from account logs.
	estrCreate(&pEscapedString);
	estrAppendEscaped(&pEscapedString, pFullString);
	servLog(LOG_ACCOUNTLOG, "AccountLog", "Time %u InDB %u account %u string \"%s\"", timeSecondsSince2000(), !gbAccountLogRedirect, pAccount->uID, pEscapedString);
	estrDestroy(&pEscapedString);

	if (!gbAccountLogRedirect)
	{
		addAccountLog(pAccount, pFullString);
	}

	estrDestroy(&pFullString);
	PERFINFO_AUTO_STOP_FUNC();
}

// Destroy an AccountLogBatch from the DB - does not clean up batch ID in the account
void destroyAccountLogBatch(U32 uBatchID)
{
	objRequestContainerDestroyLocal(NULL, GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, uBatchID);
}

U32 getFirstAccountLogTimestampFromBatch(U32 uBatchID)
{
	Container *pBatchContainer = NULL;
	U32 uTimestamp = 0;

	PERFINFO_AUTO_START_FUNC();
	pBatchContainer = objGetContainerEx(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH, uBatchID, false, false, false);
	if (pBatchContainer)
	{
		AccountLogBatch *pBatch = pBatchContainer->containerData;
		ContainerSchema *pSchema = NULL;

		if (!pBatch)
		{
			pSchema = objFindContainerSchema(GLOBALTYPE_ACCOUNTSERVER_LOG_BATCH);
			objUnpackContainerEx(pSchema, pBatchContainer, &pBatch, true, false, false);
		}

		if (devassert(pBatch))
		{
			if (eaSize(&pBatch->eaLogEntries) > 0)
				uTimestamp = pBatch->eaLogEntries[0]->uSecondsSince2000;

			if (eaSize(&pBatch->eaIndexedLogEntries) > 0)
				uTimestamp = pBatch->eaIndexedLogEntries[0]->uSecondsSince2000;

			if (pSchema)
			{
				objDeInitContainerObject(pSchema, pBatch);
				objDestroyContainerObject(pSchema, pBatch);
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return uTimestamp;
}

/************************************************************************/
/* Get all activity logs for an account                                 */
/************************************************************************/

static void accountAddLogs(EARRAY_OF(const AccountLogEntry) *eaEntriesOut, CONST_EARRAY_OF(AccountLogEntry) *eaEntriesIn, int curCount, int offset, int limit)
{
	int size = eaSize(eaEntriesIn);
	int start = (offset > curCount) ? (offset - curCount) : 0;
	if (start < size)
	{
		int numToAdd = (limit == 0) ? (size - start) : min(limit - eaSize(eaEntriesOut), size - start);
		if (start == 0 && numToAdd == size)
			eaPushEArray(eaEntriesOut, eaEntriesIn);
		else
		{
			int i;
			for (i=start; i<start+numToAdd; i++)
				eaPush(eaEntriesOut, (*eaEntriesIn)[i]);
		}
	}
}

#define AddAccountLogs(eaLogs) { if (!bDone) {\
				accountAddLogs(eaEntriesOut, &(eaLogs), count, offset, limit);\
				if (limit)\
					bDone = eaSize(eaEntriesOut) >= limit;\
			}\
			count += eaSize(&(eaLogs)); }

int accountGetLogEntries(const AccountInfo *pAccount, EARRAY_OF(const AccountLogEntry) *eaEntriesOut, int offset, int limit)
{
	int count = 0;
	bool bDone = false;
	if (!verify(pAccount)) return false;

	PERFINFO_AUTO_START_FUNC();

	if (isAccountUsingIndexedLogs(pAccount))
	{
		// If the account's logs have been indexed, then they're on eauIndexedLogBatchIDs and ppIndexedLogBuffer
		EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauIndexedLogBatchIDs, iBatch, iNumBatches);
		{
			const AccountLogBatch *pBatch = getAccountLogBatch(pAccount->eauIndexedLogBatchIDs[iBatch]);
			if (devassert(pBatch))
				AddAccountLogs(pBatch->eaIndexedLogEntries);
		}
		EARRAY_FOREACH_END;
		AddAccountLogs(pAccount->ppIndexedLogBuffer);
	}
	else
	{
		// Accounts that haven't been indexed are either on eauLogBatchIDs or ppLogEntries
		// Those are followed in either case by ppLogEntriesBuffer
		if (eaiSize(&pAccount->eauLogBatchIDs))
		{
			EARRAY_INT_CONST_FOREACH_BEGIN(pAccount->eauLogBatchIDs, iBatch, iNumBatches);
			{
				const AccountLogBatch *pBatch = getAccountLogBatch(pAccount->eauLogBatchIDs[iBatch]);
				if (devassert(pBatch))
					AddAccountLogs(pBatch->eaLogEntries);
			}
			EARRAY_FOREACH_END;
		}
		else
		{
			AddAccountLogs(pAccount->ppLogEntries);
		}
		AddAccountLogs(pAccount->ppLogEntriesBuffer);
	}

	// Also include the temporary rebucketing buffer, just in case
	AddAccountLogs(pAccount->ppTemporaryLogBufferDuringRebucketing);

	PERFINFO_AUTO_STOP_FUNC();

	return count;
}

#include "AccountLog_c_ast.c"