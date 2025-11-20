#ifndef _CRYPT_H
#define _CRYPT_H
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

C_DECLARATIONS_BEGIN

void cryptInit();
void cryptMD5Init();
void cryptMakeKeyPair(U32 *private_key,U32 *public_key);
int cryptMakeSharedSecret(U32 *shared_secret,U32 *my_private_key,U32 *their_public_key);
void cryptMD5Update(const U8 *data, unsigned int len);
void cryptMD5Final(U32 hash[4]);
void cryptMD5(U8 *data,int len,U32 hash[4]); // threadsafe one
void cryptMD5Hex(const U8 *data,int len, char *out, size_t out_size); // threadsafe one
void cryptSHA256(U8 *data,int len,U32 hash[8]); // threadsafe one
void cryptAdler32Init(void);
void cryptAdler32Update(const U8 *data, int len);
void cryptAdler32Update_IgnoreCase(const U8 *data, int len);
void cryptAdler32Update_AutoEndian(const U8 *data, int len);
U32 cryptAdler32Final(void);
U32 cryptAdler32(const U8 *data, size_t len);

//for debugging only
U32 cryptAdlerGetCurValue(void);

//gets the CRC of a file, or 0 if the file can't be loaded
U32 cryptAdlerFile(const char *pFileName);
S32 cryptAdlerFileNoAlloc(const char *fileName, U32* checksumOut);
S32 cryptAdlerFileAndFileNameNoAlloc(const char *fileName, U32* checksumOut);

__forceinline static void cryptAdler32UpdateString(SA_PARAM_NN_STR const char *str)
{
	cryptAdler32Update((const U8*)str, (int)strlen(str));
}

__forceinline static U32 cryptAdler32String(SA_PARAM_NN_STR const char *str)
{
	return cryptAdler32((const U8*)str, (int)strlen(str));
}

#define cryptPasswordHashString(str) (str ? cryptAdler32String(str) : 0)

typedef struct rc4_key
{      
   unsigned char state[256];       
   unsigned char x;        
   unsigned char y;
} rc4_key;

void cryptInitRc4(rc4_key *key,unsigned char *key_data_ptr, int key_data_len);
void cryptRc4(rc4_key *key,unsigned char *buffer_ptr, int buffer_len);

typedef struct EncryptionState EncryptionState;

EncryptionState * cryptRSACreateState(void);
void cryptRSADestroyState(EncryptionState *state);

size_t cryptRSACipherTextLength(EncryptionState *state);
bool cryptRSAEncrypt(EncryptionState *state, const char * message, size_t message_size, char * out_buffer, size_t out_buffer_size);
bool cryptRSADecrypt(EncryptionState *state, const char * message, size_t message_size, char * out_buffer, size_t out_buffer_size);

// Returns it as a Base64-MIME encoded string
char * cryptRSAGenerateSignature(EncryptionState *state, const char *message);
// Takes in a Base64-MIME encoded string as the signature
int cryptRSAVerifySignature(EncryptionState *state, const char *message, const char *signature);

void cryptLoadRSAKeyPair(EncryptionState *state, const char *pubKey, const char *privKey);
void cryptLoadRSAPublicKey(EncryptionState *state, const char *pubKey);

// Base-64 encoded SHA-256 hashes
int cryptGetBase64SHASize(void); // Returns the size of buffer needed for storing the base64-encoded SHA hash
char * cryptCalculateSHAHash_s(const char *string, char *buffer, size_t buffer_size, bool bUseHex);
int cryptVerifySHAHash(const char *string, const char *hash);

//given a base-64-encoded hash calculated by cryptCalculateSHAHash_s, or the equivalent, and a string,
//salts the hash and rehashes, then XORs in the original hash. Comes in two versions, one treats
//the salt as case insensitive
char *cryptAddSaltToHash(const char *pExistingHash, const char *pSaltString, bool bCaseInsensitiveSalt, char *buffer, size_t buffer_size);

#define cryptCalculateSHAHash(string,buffer,buffer_size) cryptCalculateSHAHash_s(string, buffer, buffer_size, false);
#define cryptCalculateSHAHashWithHex(string,buffer) cryptCalculateSHAHash_s(string, buffer, ARRAY_SIZE_CHECKED(buffer), true);

// Base64 Decoding
size_t decodeBase64StringSize(SA_PRE_NN_STR const char *encoded);
int decodeBase64String (SA_PRE_NN_RELEMS_VAR(src_size) const char *encoded, size_t src_size, SA_PARAM_NN_VALID char *buffer, size_t buffer_size);
int encodeBase64String (SA_PRE_NN_RELEMS_VAR(src_size) const unsigned char *unencoded, size_t src_size, SA_PARAM_NN_VALID char *buffer, size_t buffer_size);

// Encoding/Decoding Hex Strings
int decodeHexString(const unsigned char *encoded, size_t src_size, char *buffer, size_t buffer_size);
int encodeHexString(const unsigned char *unencoded, size_t src_size, char *buffer, size_t buffer_size);

// HMAC-SHA1 encryption
char *cryptHMACSHA1Create(const char *secret_key, const char *message, char *out_buf, size_t out_len);

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Networking););

// Like rand(), but secure.
int cryptSecureRand();

void *AESEncode(char key[32], const char *pData, int iDataSize, int *piOutSize);
void *AESDecode(char key[32], const char *pEncoded, int iEncodedSize, int *piOutSize);

int AESEncode_GetEncodeBufferSizeFromDataSize(int iDataSize);
int AESDecode_GetDecodeBufferSizeFromEncodedSize(int iEncodedSize);

//returns the size of bytes used in the buffer, or 0 if something goes wrong
int AESEncodeIntoBuffer(char key[32], const char *pData, int iDataSize, char *pBuffer, int iBufferSize);
int AESDecodeIntoBuffer(char key[32], const char *pEncoded, int iEncodedSize, char *pBuffer, int iBufferSize);

void cryptGetRandomBitsWithMouseEntropyAndAllSortsOfCrazyStuff(U8 *pOutData, int iDataSize);

C_DECLARATIONS_END

#endif
