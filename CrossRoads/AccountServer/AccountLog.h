#pragma once

typedef struct AccountInfo AccountInfo;
typedef struct AccountLogEntry AccountLogEntry;
typedef struct AccountLogBatch AccountLogBatch;

// All changes to an account should be logged by accountLog().
void accountLog(SA_PARAM_NN_VALID const AccountInfo *pAccount, FORMAT_STR const char * pFormat, ...);
bool isAccountUsingIndexedLogs(const AccountInfo *pAccount);

void initAccountLogEntryMempool(void);
void queueAccountForRebucketing(const AccountInfo *pAccount);
void accountLogRebucketingTick(void);
void destroyAccountLogBatch(U32 uBatchID);


U32 getFirstAccountLogTimestampFromBatch(U32 uBatchID);
// Returns the total number of logs for the AccountInfo
int accountGetLogEntries(SA_PARAM_NN_VALID const AccountInfo *pAccount, SA_PRE_NN_FREE SA_POST_NN_VALID EARRAY_OF(const AccountLogEntry) *eaEntriesOut, int offset, int limit);