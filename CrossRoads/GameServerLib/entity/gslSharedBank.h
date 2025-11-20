/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef GSL_SHARED_BANK_H
#define GSL_SHARED_BANK_H

#include "ReferenceSystem.h"
#include "GlobalTypeEnum.h"
#include "GlobalEnums.h"
#include "TransactionOutcomes.h"

typedef struct SharedBankCBData SharedBankCBData;

typedef void (*SharedBankCallback)(TransactionReturnVal *pReturn, void *pCBData);

typedef struct SharedBankCBData
{
	GlobalType ownerType;
	ContainerID ownerID;
	U32 accountID;

	SharedBankCallback pFunc;
	void* pUserData;

} SharedBankCBData;

bool gslSharedBankLoadOrCreate(Entity *pEntity);
void gslSharedBank_TransferNumeric(Entity *pEnt, S32 iToBank, const char *pchNumeric);
void SharedBankFixupCheck(Entity *pEnt);
void RestoreSharedBank_CB(TransactionReturnVal *returnVal, SharedBankCBData *cbData);

#endif	// GSL_SHARED_BANK_H