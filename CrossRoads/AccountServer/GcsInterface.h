#ifndef CRYPTIC_ACCOUNTSERVER_GCSINTERFACE_H
#define CRYPTIC_ACCOUNTSERVER_GCSINTERFACE_H

typedef struct NetLink NetLink;

// Initialize the Account Server interface to the Global Chat Server.
int GcsInterfaceInit(void);

// Send display name update notification to the Global Chat Server.
void AccountServer_SendDisplayNameUpdate(U32 uRequestID, SA_PARAM_NN_VALID const AccountInfo *pAccount);

// Send account creation notification to the Global Chat Server.
void AccountServer_SendCreateAccount(NetLink *link, U32 uRequestID, U32 uAccountID);

#endif  // CRYPTIC_ACCOUNTSERVER_GCSINTERFACE_H
