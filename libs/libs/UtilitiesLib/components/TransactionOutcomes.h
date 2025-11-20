/***************************************************************************



***************************************************************************/

#pragma once
GCC_SYSTEM

//these are the various outcomes that can be returned from a transaction. A transaction returns with a single
//outcome of the entire transaction, plus individual outcomes for each base transaction. For the atomic transaction
//types, the entire transaction will be a success if and only if each base transaction is a success.

AUTO_ENUM;
typedef enum enumTransactionOutcome
{
	// This outcome is unitialized, and it has not actually been requested
	TRANSACTION_OUTCOME_UNINITIALIZED = 0,

	TRANSACTION_OUTCOME_NONE,

	//a full transaction will always have one of these two outcomes:
	TRANSACTION_OUTCOME_FAILURE,
	TRANSACTION_OUTCOME_SUCCESS,
	
	//a base transaction of a full transaction might never have been processed (if it's a sequential atomic
	//transaction and an earlier step failed), so might have this outcome:
	TRANSACTION_OUTCOME_UNDEFINED,
} enumTransactionOutcome;
extern StaticDefineInt enumTransactionOutcomeEnum[];

// For returning a special case return string

#define TRANSACTION_RETURN_FAILURE(message, ...) { if (ATR_RESULT_FAIL) { estrConcatf(ATR_RESULT_FAIL, message, __VA_ARGS__); } return TRANSACTION_OUTCOME_FAILURE; }
#define TRANSACTION_RETURN_SUCCESS(message, ...) { if (ATR_RESULT_SUCCESS) { estrConcatf(ATR_RESULT_SUCCESS, message, __VA_ARGS__); } return TRANSACTION_OUTCOME_SUCCESS; }

// For returning success or failure, and appending to the default log

#define TRANSACTION_RETURN_LOG_FAILURE(message, ...) { if (ATR_RESULT_FAIL) { TRANSACTION_APPEND_LOG_FAILURE(message, __VA_ARGS__); } return TRANSACTION_OUTCOME_FAILURE; }
#define TRANSACTION_RETURN_LOG_SUCCESS(message, ...) { if (ATR_RESULT_SUCCESS) { TRANSACTION_APPEND_LOG_SUCCESS(message, __VA_ARGS__); } return TRANSACTION_OUTCOME_SUCCESS; }

// For appending to the default log based on transaction outcome

#define TRANSACTION_APPEND_LOG_FAILURE(message, ...) (ATR_RESULT_FAIL ? (((*ATR_RESULT_FAIL && **ATR_RESULT_FAIL)?estrConcatStatic(ATR_RESULT_FAIL, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_FAIL, "log "), estrAppendEscapedf(ATR_RESULT_FAIL, message, ##__VA_ARGS__)), 0 : 0)
#define TRANSACTION_APPEND_LOG_SUCCESS(message, ...) (ATR_RESULT_SUCCESS ? (((*ATR_RESULT_SUCCESS && **ATR_RESULT_SUCCESS)?estrConcatStatic(ATR_RESULT_SUCCESS, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_SUCCESS, "log "), estrAppendEscapedf(ATR_RESULT_SUCCESS, message, ##__VA_ARGS__)), 0 : 0)

// For appending to a specific log category (instead of the default log) based on transaction outcome

#define TRANSACTION_APPEND_LOG_TO_CATEGORY_FAILURE(eCategory, message, ...) (ATR_RESULT_FAIL ? (((*ATR_RESULT_FAIL && **ATR_RESULT_FAIL)?estrConcatStatic(ATR_RESULT_FAIL, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_FAIL, "logToCategory "), estrAppendEscapedf(ATR_RESULT_FAIL, "%d ", eCategory), estrAppendEscapedf(ATR_RESULT_FAIL, message, ##__VA_ARGS__)), 0 : 0)
#define TRANSACTION_APPEND_LOG_TO_CATEGORY_SUCCESS(eCategory, message, ...) (ATR_RESULT_SUCCESS ? (((*ATR_RESULT_SUCCESS && **ATR_RESULT_SUCCESS)?estrConcatStatic(ATR_RESULT_SUCCESS, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_SUCCESS, "logToCategory "), estrAppendEscapedf(ATR_RESULT_SUCCESS, "%d ", eCategory), estrAppendEscapedf(ATR_RESULT_SUCCESS, message, ##__VA_ARGS__)), 0 : 0)

// For appending to a specific log category and action name (instead of the default log and the transaction name) based on transaction outcome

#define TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_FAILURE(eCategory, actionname, message, ...) (ATR_RESULT_FAIL ? (((*ATR_RESULT_FAIL && **ATR_RESULT_FAIL)?estrConcatStatic(ATR_RESULT_FAIL, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_FAIL, "logToCategoryWithName "), estrAppendEscapedf(ATR_RESULT_FAIL, "%d ", eCategory), estrAppendEscapedf(ATR_RESULT_FAIL, "%s ", actionname), estrAppendEscapedf(ATR_RESULT_FAIL, message, ##__VA_ARGS__)), 0 : 0)
#define TRANSACTION_APPEND_LOG_TO_CATEGORY_WITH_NAME_SUCCESS(eCategory, actionname, message, ...) (ATR_RESULT_SUCCESS ? (((*ATR_RESULT_SUCCESS && **ATR_RESULT_SUCCESS)?estrConcatStatic(ATR_RESULT_SUCCESS, "\n"), 0 : 0), estrConcatStatic(ATR_RESULT_SUCCESS, "logToCategoryWithName "), estrAppendEscapedf(ATR_RESULT_SUCCESS, "%d ", eCategory), estrAppendEscapedf(ATR_RESULT_SUCCESS, "%s ", actionname), estrAppendEscapedf(ATR_RESULT_SUCCESS, message, ##__VA_ARGS__)), 0 : 0)





//This structure is created and filled in with the return value from a single base transaction, which consists of an outcome (success/failure)
//and a string (get strings out of stringhandles by calling GetStringFromHandle
typedef struct BaseTransactionReturnVal
{
	enumTransactionOutcome eOutcome;
	char *returnString;
} BaseTransactionReturnVal;

//this structure holds the return values for an entire transaction, namely an outcome and a bunch of BaseTransactionReturnVals,
//one per base transaction. When you request a transaction, if you want a return value, you have to pass in a pointer to one of these things.
//
//note that after you specify one of these structs and it gets filled in, you need to call ReleaseReturnValData to free up the return data.
#define TRANSACTIONRETURN_FLAG_MANAGED_RETURN_VAL (1 << 0)

//this transaction was being managed by the active transaction system, was created with objCreateManagedReturnVal_WithTimeOut, and 
//has timed out
#define TRANSACTIONRETURN_FLAG_TIMED_OUT (1 << 1)

//this transaction was created by one of the functions in LoggedTransactions.c
#define TRANSACTIONRETURN_FLAG_LOGGED_RETURN (1 << 2)

typedef struct TransactionReturnVal
{
	enumTransactionOutcome eOutcome;
	const char *pTransactionName;
	int iNumBaseTransactions;
	BaseTransactionReturnVal *pBaseReturnVals;

	//used internally
	U32 iID;
	U32 eFlags;

} TransactionReturnVal;
