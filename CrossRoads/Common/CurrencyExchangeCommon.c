
#include "CurrencyExchangeCommon.h"
#include "GameBranch.h"
#include "textparser.h"
#include "objSchema.h"
#include "error.h"
#include "earray.h"
#include "timing.h"
#include "MicroTransactions.h"

#include "AutoGen/CurrencyExchangeCommon_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););
AUTO_RUN_ANON(memBudgetAddMapping("CurrencyExchangeConfig", BUDGET_UISystem););

CurrencyExchangeConfig gCurrencyExchangeConfig;

static EARRAY_OF(CurrencyExchangeSubscriptionState) sUIDataSubscribers = NULL;

const char *
CurrencyExchangeConfig_GetMtcReadyToClaimEscrowAccountKey(void)
{
	return microtrans_GetShardReadyToClaimBucketKey();
}

int
CurrencyExchange_NumSubscribers(void)
{
	return eaSize(&sUIDataSubscribers);
}

CurrencyExchangeSubscriptionState *
CurrencyExchange_GetSubscriberState(GlobalType subscriberType, ContainerID subscriberID)
{
	S64 key;
	CurrencyExchangeSubscriptionState *subscriberState;

	// compute the unique key
	key = ((S64)subscriberType) << 32 | subscriberID;

	// see if it already exists
	subscriberState = eaIndexedGetUsingInt(&sUIDataSubscribers, key);

	return subscriberState;
}

void
CurrencyExchange_AddSubscriber(GlobalType subscriberType, ContainerID subscriberID)
{
	S64 key;
	CurrencyExchangeSubscriptionState *subscriberState;

	// Turn on indexing if this is the first time accessing the subscriber's array.
	if ( sUIDataSubscribers == NULL )
	{
		eaIndexedEnable(&sUIDataSubscribers, parse_CurrencyExchangeSubscriptionState);
	}

	// If someone changes the size of ContainerID, this is going to need to change.
	devassert(sizeof(ContainerID) == sizeof(U32));

	// Compute the unique key.
	key = ((S64)subscriberType) << 32 | subscriberID;

	// See if it already exists.
	subscriberState = eaIndexedGetUsingInt(&sUIDataSubscribers, key);

	// If it doesn't already exist, then create it.
	if ( subscriberState == NULL )
	{
		subscriberState = StructCreate(parse_CurrencyExchangeSubscriptionState);
		subscriberState->key = key;
		subscriberState->subscriberType = subscriberType;
		subscriberState->subscriberID = subscriberID;
		eaIndexedAdd(&sUIDataSubscribers, subscriberState);
	}

	// Update last request time.
	subscriberState->lastUIDataRequestTime = timeSecondsSince2000();
}

//
// Call sendUpdateFunc() for any subscriber that has not been updated in sendInterval seconds.
// Remove any subscriber that has not sent a request in expireInterval seconds.
//
void
CurrencyExchange_UpdateSubscribers(CurrencyExchangeSubscriptionUpdateFunc sendUpdateFunc, U32 sendInterval, U32 expireInterval)
{
	int i;
	U32 curTime = timeSecondsSince2000();

	for ( i = eaSize(&sUIDataSubscribers) - 1; i >= 0; i-- )
	{
		CurrencyExchangeSubscriptionState *subscriberState = sUIDataSubscribers[i];
		// If the subscriber has not sent a request within the expireInterval, then remove it.
		if ( ( subscriberState->lastUIDataRequestTime + expireInterval ) < curTime )
		{
			eaRemove(&sUIDataSubscribers, i);
			StructDestroy(parse_CurrencyExchangeSubscriptionState, subscriberState);
		}
		else if ( ( subscriberState->lastUIDataUpdateTime + sendInterval ) < curTime )
		{
			// If the send interval has gone by, then send an update
			sendUpdateFunc(subscriberState->subscriberType, subscriberState->subscriberID);
			subscriberState->lastUIDataUpdateTime = curTime;
		}
	}
}


AUTO_STARTUP(CurrencyExchangeConfig) ASTRT_DEPS(MicroTransactionConfig);
void CurrencyExchangeConfig_Load(void)
{
	char *estrSharedMemory = NULL;
	char *pcBinFile = NULL;
	char *pcBuffer = NULL;
	char *estrFile = NULL;

    loadstart_printf("Loading CurrencyExchangeConfig...");

    // initialize with default values in case the config file doesn't exist
    StructReset(parse_CurrencyExchangeConfig, &gCurrencyExchangeConfig);

	MakeSharedMemoryName(GameBranch_GetFilename(&pcBinFile, "CurrencyExchangeConfig.bin"),&estrSharedMemory);
	estrPrintf(&estrFile, "defs/config/%s", GameBranch_GetFilename(&pcBuffer, "CurrencyExchangeConfig.def"));
	ParserLoadFilesShared(estrSharedMemory, NULL, estrFile, pcBinFile, PARSER_OPTIONALFLAG, parse_CurrencyExchangeConfig, &gCurrencyExchangeConfig);

	estrDestroy(&estrFile);

	loadend_printf("Done.");

	estrDestroy(&pcBuffer);
	estrDestroy(&pcBinFile);
	estrDestroy(&estrSharedMemory);
}

AUTO_STARTUP(CurrencyExchangeSchema);
void CurrencyExchange_RegisterSchema(void)
{
	objRegisterNativeSchema(GLOBALTYPE_CURRENCYEXCHANGE, parse_CurrencyExchangeAccountData, NULL, NULL, NULL, NULL, NULL);
}

#include "AutoGen/CurrencyExchangeCommon_h_ast.c"
