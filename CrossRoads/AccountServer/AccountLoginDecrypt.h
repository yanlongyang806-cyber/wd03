#pragma once

/*
 * Note that this file is for network decryption, not to be confused
 * with database encryption (in AccountEncryption.h)
 */

typedef enum AccountServerEncryptionKeyVersion AccountServerEncryptionKeyVersion;

// Must be called before any of the other functions
void ALDInit(void);

// Should be called before shutdown
void ALDDeinit(void);

typedef void (*AccountDecryptPasswordCB)(
	SA_PARAM_OP_STR const char * szPlaintextPassword,
	void * pUserData);

// Decrypt a password and call a callback when it's ready
void ALDGetPassword(
	AccountServerEncryptionKeyVersion eKeyVersion,
	SA_PARAM_NN_STR const char * szEncryptedPassword,
	SA_PARAM_NN_VALID AccountDecryptPasswordCB pCallback,
	void * pUserData);

// Service background threads
void ALDProcessQueue(void);