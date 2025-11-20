#include "AccountManagement.h"
#include "cmdparse.h"
#include "objContainer.h"
#include "StringUtil.h"
#include "billing.h"
#include "ProductKey.h"
#include "AccountReporting.h"
#include "GlobalData.h"
#include "Money.h"
#include "Activity/billingActivity.h"
#include "InternalSubs.h"

// Prompts used for dangerous commands
#define DC_CONSOLE_ONLY_ERROR "This command must be executed from the console directly.\n"
#define DC_CONFIRM_ANSWER "yes"
#define DC_CONFIRM_ERROR "This is a potentially dangerous command. Please confirm you really wish to execute it by passing '" DC_CONFIRM_ANSWER "' to it.\n"

// Handle the error state for a dangerous command
__forceinline static void HandleDangerousError(SA_PARAM_NN_VALID const CmdContext *pContext)
{
	if (pContext->eHowCalled != CMD_CONTEXT_HOWCALLED_DDCONSOLE)
	{
		printf(DC_CONSOLE_ONLY_ERROR);
		return;
	}

	printf(DC_CONFIRM_ERROR);
}

// Make sure the dangerous command has been confirmed
__forceinline static bool DangerousConfirmed(SA_PARAM_NN_VALID const CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm)
{
	if (pContext->eHowCalled != CMD_CONTEXT_HOWCALLED_DDCONSOLE)
	{
		printf(DC_CONSOLE_ONLY_ERROR);
		return false;
	}

	if (stricmp_safe(pConfirm, DC_CONFIRM_ANSWER))
	{
		printf(DC_CONFIRM_ERROR);
		return false;
	}

	return true;
}

/************************************************************************/
/* Remove all billing enabled flags                                     */
/************************************************************************/

// Set the billingEnabled flag on all accounts to false
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void removeAllBillingEnabled(CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm)
{
	Container *con;
	ContainerIterator iter = {0};

	if (!DangerousConfirmed(pContext, pConfirm)) return;

	printf("Disabling billing for all accounts...\n");

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		AccountInfo *account = (AccountInfo *) con->containerData;
		accountSetBillingDisabled(account);
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(removeAllBillingEnabled);
void removeAllBillingEnabledError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}

/************************************************************************/
/* Remove all internal subs by product                                  */
/************************************************************************/

// Remove all internal subs by product
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void removeAllInternalSubsByProduct(CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm, SA_PARAM_OP_STR const char *pProductName)
{
	const ProductContainer *pProduct;
	
	if (!pProductName)
		return;

	pProduct = findProductByName(pProductName);

	if (!DangerousConfirmed(pContext, pConfirm)) return;

	if (!pProduct)
	{
		printf("Could not find product.\n");
		return;
	}

	printf("Removing internal subs given by product %s...\n", pProduct->pName);

	printf("Removed: %d\n", internalSubRemoveByProduct(pProduct->uID));
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(removeAllInternalSubsByProduct);
void removeAllInternalSubsByProductError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}


/************************************************************************/
/* Clear all play history                                               */
/************************************************************************/

// Clear the play history of all accounts
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void clearPlayHistory(CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm)
{
	Container *con;
	ContainerIterator iter = {0};

	if (!DangerousConfirmed(pContext, pConfirm)) return;

	printf("Clearing play history for all accounts...\n");

	objInitContainerIteratorFromType(GLOBALTYPE_ACCOUNT, &iter);
	con = objGetNextContainerFromIterator(&iter);
	while (con)
	{
		const AccountInfo *account = (const AccountInfo *) con->containerData;
		accountResetPlayTime(account->uID);
		con = objGetNextContainerFromIterator(&iter);
	}
	objClearContainerIterator(&iter);

	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(clearPlayHistory);
void clearPlayHistoryError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}

/************************************************************************/
/* Update account subscriptions                                         */
/************************************************************************/

// Update all player's subscription status by asking Vindicia for changes since a specified date
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Server_Billing);
void updateAccountSubscriptions(ACMD_SENTENCE pDate)
{
	U32 uSecondsSince2000;
	
	if (!pDate)
	{
		printf("You must provide a date to fetch changes from.\n");
		return;
	}

	uSecondsSince2000 = timeGetSecondsSince2000FromLocalDateString(pDate);

	if (!uSecondsSince2000)
	{
		printf("You must provide a valid date of the form (yyyy-mm-dd) to fetch changes from.\n");
		return;
	}

	billingUpdateAccountSubscriptions(uSecondsSince2000);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(updateAccountSubscriptions);
void updateAccountSubscriptionsError(CmdContext *pContext)
{
	printf("Please provide a date of the form (yyyy-mm-dd) to fetch changes from.\n");
}

/************************************************************************/
/* Find an Undistributed Product Key                                    */
/************************************************************************/

// Find an undistributed product key
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Server);
void findUndistributedKey(const char *pPrefix)
{
	ProductKey key = {0};
	bool success;
	char *keyName = NULL;

	success = findUndistributedProductKey(&key, pPrefix);

	if (!success)
	{
		printf("No undistributed keys for that prefix were found.\n");
		return;
	}

	estrStackCreate(&keyName);
	copyProductKeyName(&keyName, &key);
	printf("Key: %s\n", keyName);
	estrDestroy(&keyName);
	return;
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(findUndistributedKey);
void findUndistributedKeyError(CmdContext *pContext)
{
	printf("Please provide a prefix to search for.\n");
}

/************************************************************************/
/* Reset reporting statistics                                           */
/************************************************************************/

// Reset reporting statistics
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Account_Server);
void ResetAccountStatistics(CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm)
{
	if (!DangerousConfirmed(pContext, pConfirm)) return;

	printf("Resetting all tracked accounted statistics...\n");
	ResetAccountStats();
	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(ResetAccountStatistics);
void ResetAccountStatisticsError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}

/************************************************************************/
/* Disable logging reporting statistics                                 */
/************************************************************************/

// Disable reporting.
// This does not take a confirmation because that would make it unable to execute from the command line.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_COMMANDLINE ACMD_CATEGORY(Account_Server);
void setDisableReporting(bool bDisabled)
{
	printf("Setting reporting to %s...\n", bDisabled ? "disabled" : "enabled");
	SetReportingStatus(!bDisabled);
	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(setDisableReporting);
void setDisableReportingError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}

/************************************************************************/
/* Reset reporting statistics                                           */
/************************************************************************/

// Reset reporting statistics
// WARNING: This command needs to scan all AccountDB data, and thus may take a few seconds to run.
// This account will clean up any discrepancy in the tracked statistics.  This should normally have no effect, and should only
// be necessary if there is a bug in the statistics tracking code.
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void ScanAndUpdateAccountStats(CmdContext *pContext, SA_PARAM_OP_STR const char *pConfirm)
{
	if (!DangerousConfirmed(pContext, pConfirm)) return;

	printf("Scanning account data...\n");
	ScanAndUpdateStats();
	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(ScanAndUpdateAccountStats);
void ScanAndUpdateAccountStatsError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}

/************************************************************************/
/* Disable reporting activities to Vindicia.                            */
/************************************************************************/

// Disable activities reporting
// This does not take a confirmation because that would make it unable to execute from the command line.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_COMMANDLINE ACMD_CATEGORY(Account_Server_Billing);
void setDisableActivity(bool bDisabled)
{
	printf("Setting activity reporting to %s...\n", bDisabled ? "disabled" : "enabled");
	SetActivityReportingStatus(!bDisabled);
	printf("Complete.\n");
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(setDisableActivity);
void setDisableActivityError(CmdContext *pContext)
{
	HandleDangerousError(pContext);
}
