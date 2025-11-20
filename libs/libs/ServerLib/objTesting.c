#include "AutoGen/ServerLib_autotransactions_autogen_wrappers.h"
#include "LocalTransactionManager.h"
#include "objSchema.h"
#include "objTesting_c_ast.h"
#include "objTransactions.h"

// Get the new container identifier from a completed transaction.
static ContainerID GetNewContainerId(const TransactionReturnVal *pReturnVal)
{
	ContainerID result = 0;

	devassert(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pReturnVal->iNumBaseTransactions == 1
		&& pReturnVal->pBaseReturnVals->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char *end;
		result = strtoul(pReturnVal->pBaseReturnVals->returnString, &end, 10);
		if (*end)
			result = 0;
	}
	return result;
}

// Return true if the test should be run using the local transaction functions.
static bool IsLocal()
{
	return IsLocalManagerFullyLocal(objLocalManager());
}

// Testing container
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct TestingContainer
{
	const ContainerID uID;			AST(KEY)
	CONST_INT_EARRAY array;
} TestingContainer;
AST_PREFIX()

// Data about a particular testing job.
struct TestingData
{
	ContainerID id;
	int job;
	int i;
	int n;
	U32 timer;
	int bChaining;
};

// Clean up one testing job.
static void FinishJob(struct TestingData *pData)
{
	F32 elapsed = timerElapsed(pData->timer);
	printf("Testing complete: Job %d, Count %d, Elapsed %f, Rate %f transactions/sec\n", pData->job, pData->n, elapsed, pData->n/elapsed);
	timerFree(pData->timer);
	if (IsLocal())
		objRequestContainerDestroyLocal(NULL, GLOBALTYPE_TESTING, pData->id);
	else
		objRequestContainerDestroy(NULL, GLOBALTYPE_TESTING, pData->id, objServerType(), objServerID());
	free(pData);
}

// No-op transaction callback.
static void TestingCallbackUnchained(TransactionReturnVal *pReturn, void *pData)
{
	struct TestingData *data = pData;
	if (data->i == data->n)
		FinishJob(data);
}

// Perform next testing transaction.
static void TestingCallbackChained(TransactionReturnVal *pReturn, void *pData)
{
	struct TestingData *data = pData;
	++data->i;
	if (data->i < data->n)
		AutoTrans_trTesting(objCreateManagedReturnVal(TestingCallbackChained, pData), objServerType(), GLOBALTYPE_TESTING, data->id);
	else
		FinishJob(data);
}

// Performing testing transactions.
static void CreateTestingCallback(TransactionReturnVal *pReturn, void *pData)
{
	struct TestingData *data = pData;
	data->id = GetNewContainerId(pReturn);
	devassert(data->id);

	if (data->bChaining)
		AutoTrans_trTesting(objCreateManagedReturnVal(TestingCallbackChained, pData), objServerType(), GLOBALTYPE_TESTING, data->id);
	else
	{
		for (; data->i < data->n; ++data->i)
			AutoTrans_trTesting(objCreateManagedReturnVal(TestingCallbackUnchained, pData), objServerType(), GLOBALTYPE_TESTING, data->id);
	}
}

// Test transaction performance.
AUTO_COMMAND;
void TransBench(int iSize, int iCount, int iJobs, bool bChaining)
{
	static bool init = false;
	NOCONST(TestingContainer) testing;
	int i;
	bool local;

	// Check parameters.
	if (iCount <= 0)
	{
		Errorf("Count must be positive.");
		return;
	}
	if (iJobs <= 0)
	{
		Errorf("Jobs must be positive.");
		return;
	}

	// Initialize schema.
	if (!init)
	{
		objRegisterNativeSchema(GLOBALTYPE_TESTING, parse_TestingContainer, NULL, NULL, NULL, NULL, NULL);
		init = true;
	}

	// Create testing containers.
	local = IsLocalManagerFullyLocal(objLocalManager());
	for (i = 0; i != iJobs; ++i)
	{
		struct TestingData *data = malloc(sizeof(*data));
		data->job = i;
		data->i = 0;
		data->n = iCount;
		data->timer = timerAlloc();
		data->bChaining = bChaining;
		timerStart(data->timer);
		testing.uID = 0;
		testing.array = NULL;
		eaiSetSize(&testing.array, iSize);
		if (local)
		{
			objRequestContainerCreateLocal(objCreateManagedReturnVal(CreateTestingCallback, data),
				GLOBALTYPE_TESTING, &testing);
		}
		else
			objRequestContainerCreate(objCreateManagedReturnVal(CreateTestingCallback, data),
				GLOBALTYPE_TESTING, &testing, objServerType(), objServerID());
	}
}

// Testing transaction
AUTO_TRANSACTION
ATR_LOCKS(pTesting, ".Array");
enumTransactionOutcome trTesting(ATR_ARGS, NOCONST(TestingContainer) *pTesting)
{
	EARRAY_INT_CONST_FOREACH_BEGIN(pTesting->array, i, n);
	{
		++pTesting->array[i];
	}
	EARRAY_FOREACH_END;

	return TRANSACTION_OUTCOME_SUCCESS;
}

#include "objTesting_c_ast.c"
