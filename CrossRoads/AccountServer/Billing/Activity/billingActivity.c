#include <time.h>

#include "AccountManagement.h"
#include "billingActivity.h"
#include "error.h"
#include "estring.h"
#include "logging.h"
#include "sock.h"
#include "timing_profiler.h"

// Set to true if activity reporting should be disabled.
static bool gbActivityReportingDisabled = false;

// EArray of activities that are waiting to be sent to the server
static struct vin__Activity **gppPendingActivities = NULL;

// Record result of reporting activities to Vindicia.
static void btActivityRecord_Complete(SA_PARAM_NN_VALID BillingTransaction *pTrans)
{
	VindiciaXMLtoObjResult *pResult;
	struct act__recordResponse *pResponse;

	PERFINFO_AUTO_START_FUNC();
	// Parse response from Vindicia.
	pResult = vindiciaCreateResponse(pTrans, VINDICIA_TYPE(act, recordResponse));
	if(!pResult)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	btFreeObjResult(pTrans, pResult);
	pResponse = pResult->pObj;
	BILLING_DEBUG_RESPONSE("act__record", pResponse);

	// Make sure it worked.
	if (pResponse->_return_->returnCode != VINDICIA_SUCCESS_CODE)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_RESPONSE", "Received unknown response %d from Vindicia in %s: %s", (int)pResponse->_return_->returnCode,
			__FUNCTION__, pResponse->_return_->returnString);
		btFail(pTrans, "Unknown response from Vindicia");
	}
	PERFINFO_AUTO_STOP();
}

// Create a new activity.
SA_RET_NN_VALID static struct vin__Activity *NewActivity(enum vin__ActivityType eActivityType, SA_PARAM_NN_VALID const AccountInfo *pAccount)
{
	// Create the activity object.
	// These subobjects will be freed in DestroyActivity() after the activity batch is assembled.
	struct vin__Activity *activity = NULL;

	PERFINFO_AUTO_START_FUNC();
	activity = callocStruct(struct vin__Activity);
	activity->account = callocStruct(struct vin__Account);
	activity->account->merchantAccountId = getMerchantAccountID(pAccount);
	activity->activityType = eActivityType;
	activity->activityArgs = callocStruct(struct vin__ActivityTypeArg);
	activity->timestamp = time(NULL);

	// Allocate argument holder depending on type.
	switch (eActivityType)
	{
		case vin__ActivityType__Login:
			activity->activityArgs->loginArgs = callocStruct(struct vin__ActivityLogin);
			break;
		case vin__ActivityType__Logout:
			activity->activityArgs->logoutArgs = callocStruct(struct vin__ActivityLogout);
			break;
		case vin__ActivityType__URIView:
			activity->activityArgs->uriviewArgs = callocStruct(struct vin__ActivityURIView);
			break;
		case vin__ActivityType__Phone:
			activity->activityArgs->phoneArgs = callocStruct(struct vin__ActivityPhoneContact);
			break;
		case vin__ActivityType__Email:
			activity->activityArgs->emailArgs = callocStruct(struct vin__ActivityEmailContact);
			break;
		case vin__ActivityType__Fulfillment:
			activity->activityArgs->fulfillmentArgs = callocStruct(struct vin__ActivityFulfillment);
			break;
		case vin__ActivityType__Usage:
			activity->activityArgs->usageArgs = callocStruct(struct vin__ActivityUsage);
			break;
		case vin__ActivityType__NamedValue:
			activity->activityArgs->namedvalueArgs = callocStruct(struct vin__ActivityNamedValue);
			break;
		case vin__ActivityType__Cancellation:
			activity->activityArgs->cancellationArgs = callocStruct(struct vin__ActivityCancellation);
			break;
		case vin__ActivityType__Note:
			activity->activityArgs->noteArgs = callocStruct(struct vin__ActivityNote);
			break;
		default:
			devassert(0);
	}

	PERFINFO_AUTO_STOP();
	return activity;
};

// Destroy an activity object and all its subobjects.
static void DestroyActivity(SA_PARAM_NN_VALID struct vin__Activity *activity)
{
	// Don't crash the server.
	devassert(activity);
	if (!activity)
		return;

	PERFINFO_AUTO_START_FUNC();
	// Free the object and its subobjects.
	if (activity->activityArgs->cancellationArgs)
		free(activity->activityArgs->cancellationArgs->reason);
	if (activity->activityArgs->loginArgs)
		free(activity->activityArgs->loginArgs->ip);
	if (activity->activityArgs->noteArgs)
		free(activity->activityArgs->noteArgs->note);
	free(activity->activityArgs->loginArgs);
	free(activity->activityArgs->logoutArgs);
	free(activity->activityArgs->noteArgs);
	free(activity->activityArgs);
	free(activity->account);
	free(activity);
	PERFINFO_AUTO_STOP();
}

// Record a login to an account.
void btActivityRecordCancellation(const AccountInfo *pAccount, bool bMerchantInitiated, const char *pReason)
{
	struct vin__Activity *activity;

	// Only record activity if activity reporting is enabled and the account is billing-enabled.
	verify(pAccount);
	if (gbActivityReportingDisabled || !pAccount || !pAccount->bBillingEnabled)
		return;

	PERFINFO_AUTO_START_FUNC();
	// Build login activity.
	activity = NewActivity(vin__ActivityType__Cancellation, pAccount);
	activity->activityArgs->cancellationArgs->reason = strdup(pReason);
	activity->activityArgs->cancellationArgs->initiator = bMerchantInitiated
		? vin__ActivityCancelInitType__Merchant : vin__ActivityCancelInitType__Customer;

	// Queue activity to be sent.
	eaPush(&gppPendingActivities, activity);
	PERFINFO_AUTO_STOP();
}

// Record a login to an account.
void btActivityRecordLogin(const AccountInfo *pAccount, U32 uIPAddress)
{
	struct vin__Activity *activity;

	// Only record activity if activity reporting is enabled and the account is billing-enabled.
	verify(pAccount);
	if (gbActivityReportingDisabled || !pAccount || !pAccount->bBillingEnabled)
		return;

	PERFINFO_AUTO_START_FUNC();
	// Build login activity.
	activity = NewActivity(vin__ActivityType__Login, pAccount);
	activity->activityArgs->loginArgs->ip = strdup(makeIpStr(uIPAddress));

	// Queue activity to be sent.
	eaPush(&gppPendingActivities, activity);
	PERFINFO_AUTO_STOP();
}

// Record a logout from an account.
void btActivityRecordLogout(const AccountInfo *pAccount, U32 uIPAddress, U32 uPlayTime)
{
	struct vin__Activity *activity;

	// Only record activity if activity reporting is enabled and the account is billing-enabled.
	verify(pAccount);
	if (gbActivityReportingDisabled || !pAccount || !pAccount->bBillingEnabled)
		return;

	PERFINFO_AUTO_START_FUNC();
	// Build logout activity.
	activity = NewActivity(vin__ActivityType__Logout, pAccount);
	activity->activityArgs->logoutArgs->ip = strdup(makeIpStr(uIPAddress));

	// Queue activity to be sent.
	eaPush(&gppPendingActivities, activity);

	// Record playtime information as a note annotation in a separate activity object.
	if (uPlayTime)
	{
		char *note = NULL;
		estrStackCreate(&note);
		estrPrintf(&note, "Play time for the last session (seconds): %lu", uPlayTime);
		btActivityRecordNote(pAccount, note);
		estrDestroy(&note);
	}
	PERFINFO_AUTO_STOP();
}

// Record some activity associated with an account.
void btActivityRecordNote(const AccountInfo *pAccount, const char *pNoteText)
{
	struct vin__Activity *activity;

	// Only record activity if activity reporting is enabled and the account is billing-enabled.
	verify(pAccount);
	if (gbActivityReportingDisabled || !pAccount || !pAccount->bBillingEnabled)
		return;

	PERFINFO_AUTO_START_FUNC();
	// Build logout activity.
	activity = NewActivity(vin__ActivityType__Note, pAccount);
	activity->activityArgs->noteArgs->note = strdup(pNoteText);

	// Queue activity to be sent.
	eaPush(&gppPendingActivities, activity);
	PERFINFO_AUTO_STOP();
}

// Enable or disable activity reporting.
void SetActivityReportingStatus(bool bEnabled)
{
	// Do nothing if this isn't a change of state.
	if (gbActivityReportingDisabled == !bEnabled)
		return;

	// Record activity status change.
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Activity reporting %s.", bEnabled ? "enabled" : "disabled");

	// Enable or disable.
	gbActivityReportingDisabled = !bEnabled;
}

// Send any pending activities if necessary.
// This will return a BillingTransaction if activities were actually sent, otherwise NULL.
SA_RET_OP_VALID BillingTransaction *btSendActivities()
{
	time_t now;
	BillingTransaction *pTrans = NULL;
	struct act__record record;
	struct ArrayOfActivities array;
	char *xml = NULL;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	// Return if it is not necessary to send activities.
	now = time(NULL);
	if (gbActivityReportingDisabled || !eaSize(&gppPendingActivities)
		|| gppPendingActivities[0]->timestamp + billingGetActivityPeriod() >= now && eaSize(&gppPendingActivities) < (int)billingGetMaxActivities())
	{
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	BILLING_DEBUG_START;

	// Create record object.
	record._auth = getVindiciaAuth();
	record._activities = &array;
	record._activities->__size = eaSize(&gppPendingActivities);
	record._activities->__ptr = gppPendingActivities;
	estrStackCreate(&xml);
	success = vindiciaObjtoXML(&xml, &record, VO2X_OBJ(act, record));
	if (!success)
	{
		AssertOrAlert("ACCOUNTSERVER_VINDICIA_ACTIVITY_CREATE_FAIL", "Creation of XML activity to send to Vindicia failed.");
		PERFINFO_AUTO_STOP_FUNC();
		return NULL;
	}

	// Log this action.
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Recording %d activities to Vindicia", eaSize(&gppPendingActivities));

	// Free activity objects.
	EARRAY_CONST_FOREACH_BEGIN(gppPendingActivities, i, n);
		DestroyActivity(gppPendingActivities[i]);
	EARRAY_FOREACH_END;
	eaClear(&gppPendingActivities);

	// Create transaction.
	pTrans = btCreate("act:record", xml, btActivityRecord_Complete, NULL, false);
	estrDestroy(&xml);

	PERFINFO_AUTO_STOP_FUNC();
	return pTrans;
}
