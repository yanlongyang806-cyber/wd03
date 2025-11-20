#ifndef BILLINGACTIVITY_H
#define BILLINGACTIVITY_H

#include "billing.h"

// Record account activity.
void btActivityRecordCancellation(const AccountInfo *pAccount, bool bMerchantInitiated, const char *pReason);
void btActivityRecordLogin(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uIPAddress);
void btActivityRecordLogout(SA_PARAM_NN_VALID const AccountInfo *pAccount, U32 uIPAddress, U32 uPlayTime);
void btActivityRecordNote(SA_PARAM_NN_VALID const AccountInfo *pAccount, const char *pNoteText);

// Enable or disable activity reporting.
void SetActivityReportingStatus(bool bEnabled);

// Send any pending activities if necessary.
// This will return a BillingTransaction if activities were actually sent, otherwise NULL.
SA_RET_OP_VALID BillingTransaction *btSendActivities(void);

#endif
