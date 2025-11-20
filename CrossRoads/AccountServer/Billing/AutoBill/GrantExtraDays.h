#pragma once
#include "billing.h"

/************************************************************************/
/* Used for new super sub-create                                        */
/************************************************************************/

typedef void (*GrantDaysCallback)(bool success,
								  SA_PARAM_OP_VALID BillingTransaction *pTrans,
								  SA_PARAM_OP_VALID void *pUserData);

SA_ORET_OP_VALID BillingTransaction *
btGrantDays(SA_PARAM_NN_VALID AccountInfo *pAccount,
			SA_PARAM_NN_STR const char *VID,
			int iDays,
			SA_PARAM_OP_VALID BillingTransaction *pTrans,
			SA_PARAM_OP_VALID GrantDaysCallback pCallback,
			SA_PARAM_OP_VALID void *pUserData);

SA_ORET_OP_VALID BillingTransaction *
btSetDate(SA_PARAM_NN_VALID AccountInfo *pAccount,
		  SA_PARAM_NN_STR const char *VID,
		  U32 uDate,
		  SA_PARAM_OP_VALID BillingTransaction *pTrans,
		  bool bForce,
		  SA_PARAM_OP_VALID GrantDaysCallback pCallback,
		  SA_PARAM_OP_VALID void *pUserData);