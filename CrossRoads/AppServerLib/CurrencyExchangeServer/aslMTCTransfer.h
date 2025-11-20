#pragma once
/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "CurrencyExchangeCommon.h"

typedef struct CurrencyExchangeOverview CurrencyExchangeOverview;

typedef void (*MTCTransferCallback)(CurrencyExchangeResultType result, ContainerID srcLockID, ContainerID destLockID, char *failureDetailString, void *userData);

AUTO_STRUCT;
typedef struct CurrencyExchangeMTCTransferData
{
    // A unique ID to refer to this transfer.
    U32 transferID;									AST(KEY)

    // The time in seconds that this transfer was originally requested.
    U32 requestTime;

    // The time in seconds that this transfer was actually started.
    U32 startTime;

    // The time in seconds that this transfer should be retried.  This is only used if the initial attempt to acquire the locks fails.
    U32 retryTime;

    // How many times has this transfer been re-tried.
    U32 retryCount;

    void *userData;                                 NO_AST

    MTCTransferCallback callback;                   NO_AST

    // The account ID for the source of the transfer.
    ContainerID sourceAccountID;

    // The account ID for the destination of the transfer.
    ContainerID destinationAccountID;

    // The amount of MTC to transfer.
    U32 quantity;

    // The source key/value or chain for the transfer.  sourceKey points to global config data, so it should not be freed.
    const char *sourceKey;                          AST(UNOWNED)

    // the container ID of the account proxy lock for the source key/value or chain
    ContainerID sourceLock;

    // The destination key/value or chain for the transfer.  destinationKey points to global config data, so it should not be freed.
    const char *destinationKey;                     AST(UNOWNED)

    // The container ID of the account proxy lock for the source key/value or chain.
    ContainerID destinationLock;

} CurrencyExchangeMTCTransferData;

void MTCTransferInit(void);

U32 MTCTransferRequest(ContainerID sourceAccountID, ContainerID destinationAccountID, const char *srcKey, const char *destKey, U32 quantity, void *userData, MTCTransferCallback func);
void MTCTransferCommitFailed(U32 transferID);
void MTCTransferCommitSuccessful(U32 transferID);
void MTCTransfer_BeginFrame(void);
void MTCTransfer_GetOverviewInfo(CurrencyExchangeOverview *overview);