/***************************************************************************



***************************************************************************/

#ifndef _TRANSACTION_REQUESTMANAGER_H_
#define _TRANSACTION_REQUESTMANAGER_H_

#include "TransactionSystem.h"
#include "LocalTransactionManager.h"
typedef struct StashTableImp* StashTable;

void InitTransactionRequestManager(void);

void HandleTransactionReturnVal(Packet *pPacket);

TransactionReturnVal *GetReturnValFromID(U32 iID);

void HandleContainerOwner(LocalTransactionManager *pManager, Packet *pPak);


#endif

