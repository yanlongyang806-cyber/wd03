#ifndef BILLINGSUBSCRIPTION_H
#define BILLINGSUBSCRIPTION_H

#include "AccountServer.h"
#include "billing.h"

typedef struct SubscriptionContainer SubscriptionContainer;

// Push = "to Vindicia"
// Pull = "from Vindicia"

typedef void (*SubscriptionPushFinishCallback)(void *pUserData, bool success, SA_PARAM_OP_VALID const char *pReason);

void btSubscriptionPush(SA_PARAM_NN_VALID const SubscriptionContainer *pSub,
						SA_PARAM_OP_VALID SubscriptionPushFinishCallback fpCallback,
						SA_PARAM_OP_VALID void *pUserData);

#endif
