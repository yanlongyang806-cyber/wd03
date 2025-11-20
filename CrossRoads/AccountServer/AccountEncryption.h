#pragma once

#include "AccountServer.h"

/*
 * Note that this file is for database encryption, not to be confused
 * with network decryption (in AccountLoginDecrypt.h)
 */

void AccountEncryption_Startup(void);

//0 means no encryption is being used
int AccountEncryption_GetCurEncryptionKeyIndex(void);

//returns a pointer to the 32-byte AES key
char *AccountEncryption_FindKeyFromIndex(int iIndex);



bool accountNeedsPasswordOrEncryptionFixup(AccountInfo *pAccount);

//true on success, false on failure (will generate an alert)
bool accountDoPasswordOrEncryptionFixup(AccountInfo *pAccount);

void AccountEncryption_FixupSomeAccounts(void);

bool accountDecryptPassword(AccountInfo *pAccount);
static __forceinline bool accountDecryptPasswordIfNecessary(AccountInfo *pAccount) 
{ 
	if (pAccount->passwordInfo && pAccount->passwordInfo->password_ForRAM[0] == 0) 
		return accountDecryptPassword(pAccount); 
	return true; 
}

bool accountDecryptBackupPassword(AccountInfo *pAccount);
static __forceinline bool accountDecryptBackupPasswordIfNecessary(AccountInfo *pAccount) 
{ 
	if (pAccount->pBackupPasswordInfo && pAccount->pBackupPasswordInfo->password_ForRAM[0] == 0) 
		return accountDecryptBackupPassword(pAccount); 
	return true; 
}


bool accountSetAndEncryptPassword(ATH_ARG NOCONST(AccountInfo) *pAccount, const char *pHashedPassword);

//returns true on success, false on failure
bool accountDoCreationTimePasswordFixup(NOCONST(AccountInfo) *pAccount);


//given a string, encrypts it with the current encryption key, base 64 encodes it, and returns an alloced string (or just
//copies it if there is no encryption)
char *AccountEncryption_EncryptEncodeAndAllocString(const char *pInString);
//reverse the above process
char *AccountEncryption_DecodeDecryptAndAllocString(const char *pInString);
//with a specified key
char *AccountEncryption_DecodeDecryptAndAllocStringWithKeyVersion(const char *pInString, int iKeyIndex);