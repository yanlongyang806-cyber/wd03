/***************************************************************************
*     Copyright (c) 2005-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef SHARED_BANK_COMMON_H
#define SHARED_BANK_COMMON_H

#include "ReferenceSystem.h"
#include "GlobalTypeEnum.h"
#include "GlobalEnums.h"
#include "TransactionOutcomes.h"

typedef struct Entity Entity;
typedef struct GameAccountDataExtract GameAccountDataExtract;

typedef enum SharedBankError
{
	SharedBankError_None = 0,
	SharedBankError_No_Ent,
	SharedBankError_No_Bank,
	SharedBankError_Numeric_Error,
	SharedBankError_Ent_Full,				
	SharedBankError_Bank_Full,				
	SharedBankError_Ent_Empty,				
	SharedBankError_Bank_Empty,				
}SharedBankError;


// additional keys to allow access to the shared bank
AUTO_STRUCT;
typedef struct SharedBankKeys
{
	const char *pchFile;				AST(CURRENTFILE)
		//The current file (for reloading)

	const char *pchKeyPermission;		AST(NAME(KeyPermission) POOL_STRING)
		// the permission key
}SharedBankKeys;

// additional keys to allow access to the shared bank
AUTO_STRUCT;
typedef struct SharedBankNumeric
{
	const char *pcNumeric;				AST(POOL_STRING)
		// Numeric that can be transfered

	const U32 iMaximumValue;
		// The maximum amount of this numeric

	const char *pchFile;				AST(CURRENTFILE)
		//The current file (for reloading)

}SharedBankNumeric;

AUTO_STRUCT;
typedef struct SharedBankConfig
{
	// You must be at a contact to access the shared bank
	bool bRequireContact;

	// If any eaKeyPermissions on gad then access to the bank is also allowed if gameaccountdata has it even if the character doesn't have GAME_PERMISSION_SHARED_BANK
	EARRAY_OF(SharedBankKeys) eaKeyPermissions;

	// The maximum number of slots that the bank can have through purchases or whatever
	U32 uMaximumSlots;

	EARRAY_OF(SharedBankNumeric) eaSharedBankNumerics;

	// Can access is not checked when buying slots
	bool bCanAlwaysBuy;

	U32 uMaxInboxSize;			//The maximum number of player-to-player messages you can have in your inbox at once.
	U32 uMaxInboxAttachments;	//The maximum number of player-to-player messages with attached items you can have in your inbox at once.

} SharedBankConfig;

extern SharedBankConfig g_SharedBankConfig;

bool SharedBank_CanAccess(Entity* pEnt, GameAccountDataExtract *pExtract);
U32 SharedBank_GetNumSlots(SA_PARAM_OP_VALID Entity *pEnt, SA_PARAM_OP_VALID GameAccountDataExtract *pExtract, bool bCountGamePermission);
bool SharedBank_SharedBankNeedsFixup(Entity *pEnt, Entity *pSharedBankEnt, GameAccountDataExtract *pExtract);
SharedBankError SharedBank_ValidateNumericTransfer(Entity *pEnt, S32 iToBank, const char *pcNumeric);

#endif	// SHARED_BANK_COMMON_H;
