
#undef sprintf
#undef sprintf_s
#undef toupper
#undef tolower

#include <stdlib.h>

#include "error.h"
#include "file.h"
#include "mathUtil.h"
#include "StringUtil.h"
#undef strcpy_s
#undef FILE

#if !_PS3
#pragma warning (disable:4505) // unreferenced local function has been removed (in cryptopp)
#endif

#include "../../3rdparty/cryptopp/cryptlib.h"
#include "../../3rdparty/cryptopp/rng.h"
#include "../../3rdparty/cryptopp/dh.h"
#include "../../3rdparty/cryptopp/md5.h"
#include "../../3rdparty/cryptopp/adler32.h"

#include "../../3rdparty/cryptopp/rsa.h"
#include "../../3rdparty/cryptopp/sha.h"
#include "../../3rdparty/cryptopp/integer.h"

#include "../../3rdparty/cryptopp/rijndael.h"
#include "../../3rdparty/cryptopp/aes.h"

#if !_PS3
#include "../../3rdparty/cryptopp/hmac.h"
#endif

#include "stdtypes.h"
#include "timing.h"
#include "wininclude.h"
#include "ScratchStack.h"
#include "error.h"

#include "endian.h"
#include "crypt.h"

#if _PS3
#elif _XBOX
// #pragma comment(lib, "cryptlibxbox.lib") // removed from main source tree
#else
#ifdef _WIN64
#if _MSC_VER >= 1600
#pragma comment(lib, "cryptlibX64_vs10.lib")
#else
// #pragma comment(lib, "cryptlibX64.lib") // removed from main source tree
#endif
#else
#if _MSC_VER >= 1600
#pragma comment(lib, "cryptlib_vs10.lib")
#else
// #pragma comment(lib, "cryptlib.lib") // removed from main source tree
#endif
#endif
#endif

using namespace CryptoPP;

extern "C" {

static LC_RNG *fixed_rnd_gen = 0,*rnd_gen;
static DH *dh;


#if !defined(PROFILE) || !_XBOX
void _invalid_parameter_noinfo()
{
	assert(0);
}
#endif

void cryptInit()
{
	S64		seed64;
	U32		seed;

	if (rnd_gen)
		return;  // let cryptInit be called multiple times

	PERFINFO_AUTO_START_FUNC();
	
	GET_CPU_TICKS_64(seed64);
	seed = (U32)seed64;

    fixed_rnd_gen = new LC_RNG(0xa1c5d8f3);
	rnd_gen = new LC_RNG(seed);
	dh = new DH(*fixed_rnd_gen,512);
	dh->Precompute();
	//printf("In cryptInit\n");
	
	PERFINFO_AUTO_STOP();
}

void cryptMakeKeyPair(U32 *private_key,U32 *public_key)
{
	PERFINFO_AUTO_START_FUNC();
	cryptInit();
	dh->GenerateKeyPair(*rnd_gen,(U8 *)private_key,(U8 *)public_key);
	PERFINFO_AUTO_STOP();
}

int cryptMakeSharedSecret(U32 *shared_secret,U32 *my_private_key,U32 *their_public_key)
{
	return dh->Agree((U8 *)shared_secret, (U8 *)my_private_key, (U8 *)their_public_key, true);
}

static MD5 *threadLocalMD5()
{
	MD5 **md5=0;
	STATIC_THREAD_ALLOC_TYPE(md5,MD5 **);

	if (!*md5)
		*md5 = new MD5;
	return *md5;
}

void cryptMD5Init()
{
	threadLocalMD5();
}

void cryptMD5Update(const U8 *data, unsigned int len)
{
	threadLocalMD5()->Update((byte *)data,len);
}

void cryptMD5Final(U32 hash[4])
{
	threadLocalMD5()->Final((byte*)hash);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < 4; i++)
			hash[i] = endianSwapU32(hash[i]);
	}
}

void cryptMD5(U8 *data,int len,U32 hash[4])
{
	MD5 *md5 = threadLocalMD5();

	md5->Update((byte *)data,len);
	md5->Final((byte*)hash);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < 4; i++)
			hash[i] = endianSwapU32(hash[i]);
	}
}

void cryptMD5Hex(const U8 *data,int len,char *out, size_t out_size)
{
	MD5 *md5 = threadLocalMD5();
	U32 hash_buf[4];
	assertmsg(out_size >= 33, "Not enough space for an MD5 hash");

	md5->Update((byte *)data,len);
	md5->Final((byte*)hash_buf);
	sprintf_s(SAFESTR2(out), "%08x%08x%08x%08x", endianSwapU32(hash_buf[0]), endianSwapU32(hash_buf[1]), endianSwapU32(hash_buf[2]), endianSwapU32(hash_buf[3]));
}

void cryptSHA256(U8 *data,int len,U32 hash[8])
{
	SHA256 sha;
	sha.CalculateDigest((byte*) hash, (byte*) data, len);
	if (isBigEndian())
	{
		int i;
		for (i = 0; i < 8; i++)
			hash[i] = endianSwapU32(hash[i]);
	}
}

// ---------------------------------------
// Adler-32

void Adler32CalculateCRC(const byte *input, unsigned int length, byte *hash)
{
	static const unsigned long BASE = 65521;

	unsigned long s1 = 1;
	unsigned long s2 = 0;
	word16 m_s1, m_s2;

	if (length % 8 != 0)
	{
		do
		{
			s1 += *input++;
			s2 += s1;
			length--;
		} while (length % 8 != 0);

		if (s1 >= BASE)
			s1 -= BASE;
		s2 %= BASE;
	}

	while (length > 0)
	{
		s1 += input[0]; s2 += s1;
		s1 += input[1]; s2 += s1;
		s1 += input[2]; s2 += s1;
		s1 += input[3]; s2 += s1;
		s1 += input[4]; s2 += s1;
		s1 += input[5]; s2 += s1;
		s1 += input[6]; s2 += s1;
		s1 += input[7]; s2 += s1;

		length -= 8;
		input += 8;

		if (s1 >= BASE)
			s1 -= BASE;
		if (length % 0x8000 == 0)
			s2 %= BASE;
	}

	assert(s1 < BASE);
	assert(s2 < BASE);

	m_s1 = (word16)s1;
	m_s2 = (word16)s2;

	hash[3] = byte(m_s1);
	hash[2] = byte(m_s1 >> 8);
	hash[1] = byte(m_s2);
	hash[0] = byte(m_s2 >> 8);
}

static Adler32 *threadLocalAdler32()
{
	Adler32 **adler32=0;
	STATIC_THREAD_ALLOC_TYPE(adler32,Adler32 **);

	if (!*adler32)
	{
		*adler32 = new Adler32;
		(*adler32)->Reset();
	}
	return *adler32;
}

U32 *cryptAdler32GetPtr()
{
    Adler32	*adler32 = threadLocalAdler32();
    return (U32*)adler32 + 1;
}

U32 cryptAdlerGetCurValue(void)
{
	U32 *p = cryptAdler32GetPtr();
	return *p;
}


void cryptAdler32Init()
{
	Adler32	*adler32 = threadLocalAdler32();
	adler32->Reset();
}

int g_adler_debug_printf;
void cryptAdler32Update(const U8 *data, int len)
{
	Adler32	*adler32 = threadLocalAdler32();
	if (g_adler_debug_printf)
	{
		int i;
		printf("Adler: ");
		for (i = 0; i < len; ++i)
			printf("0x%02x ", data[i]);
		printf("\n");
	}

	adler32->Update((byte *)data,len);
}

void cryptAdler32Update_IgnoreCase(const U8 *data_in, int len)
{
	char *pTempData;
	Adler32	*adler32 = threadLocalAdler32();
	int i;

	pTempData = (char*)ScratchAlloc(len);
	for (i=0; i < len ; i++)
	{
		pTempData[i] = __ascii_toupper(data_in[i]);
	}

	if (g_adler_debug_printf)
	{
		printf("Adler: ");
		for (i = 0; i < len; ++i)
			printf("0x%02x ", pTempData[i]);
		printf("\n");
	}

	adler32->Update((byte *)pTempData,len);
	ScratchFree(pTempData);
}

void cryptAdler32Update_AutoEndian(const U8 *data, int len)
{
	U8 buf[8];
	int i;
	assert(len <= ARRAY_SIZE(buf));
	if (isBigEndian()) {
		for (i=0; i<len; i++) 
			buf[i] = data[len - i - 1];
	} else {
		for (i=0; i<len; i++) 
			buf[i] = data[i];
	}
	cryptAdler32Update(buf, len);
}

U32 cryptAdler32Final()
{
	U32	hash;
	threadLocalAdler32()->Final((byte *)&hash);
	threadLocalAdler32()->Reset();
	hash = endianSwapIfBig(U32, hash);
	return hash;
}

U32 cryptAdler32(const U8 *data,size_t len)
{
	U32	hash;
	//Adler32	*adler32;

	//adler32 = threadLocalAdler32();
	//adler32->Reset();
	//adler32->Update((byte *)data,(U32)len);
	//adler32->Final((byte *)&hash);
	Adler32CalculateCRC(data, (U32) len, (byte*) &hash);
	//printf("In cryptAdler32\n");
	hash = endianSwapIfBig(U32, hash);
	return hash;
}


U32 cryptAdlerFile(const char *pFileName)
{
	int iSize;
	U8 *pData = (U8*)fileAlloc(pFileName, &iSize);
	U32 retVal;

	if (!pData)
	{
		return 0;
	}

	retVal = cryptAdler32(pData, iSize);

	free(pData);

	return retVal;
}

	
S32 cryptAdlerFileNoAlloc(	const char *fileName,
							U32* checksumOut)
{
	FileWrapper*	f;
	U8				buffer[1024];
	U32				chunkSize;
	Adler32*		adler32;

	if(	!fileName ||
		!checksumOut)
	{
		return 0;
	}

	f = fopen(fileName, "rb");

	if(!f){
		return 0;
	}

	adler32 = threadLocalAdler32();
	adler32->Reset();

	while(chunkSize = (U32)fread(buffer, 1, sizeof(buffer), f)){
		adler32->Update(buffer, chunkSize);
	}

	adler32->Final((byte*)checksumOut);

	fclose(f);

	return 1;
}

S32 cryptAdlerFileAndFileNameNoAlloc(	const char *fileName,
							U32* checksumOut)
{
	FileWrapper*	f;
	U8				buffer[1024];
	U32				chunkSize;
	Adler32*		adler32;

	if(	!fileName ||
		!checksumOut)
	{
		return 0;
	}

	f = fopen(fileName, "rb");

	if(!f){
		return 0;
	}

	adler32 = threadLocalAdler32();
	adler32->Reset();

	adler32->Update((U8*)fileName, (int)strlen(fileName));

	while(chunkSize = (U32)fread(buffer, 1, sizeof(buffer), f)){
		adler32->Update(buffer, chunkSize);
	}

	adler32->Final((byte*)checksumOut);

	fclose(f);

	return 1;
}

#if 0
byte	private_key_buf[2][10000],public_key_buf[2][100000],agree_buf[2][10000];

void testcryptopp_loop()
{
	int i;
	word32 hash[4],in_words[4] = {1,2,3,4};

	cryptMD5((U8*)hash,(U8*)in_words,16);

	for(i=0;i<1000;i++)
	{
		cryptMakeKeyPair(private_key_buf[0],public_key_buf[0]);
		cryptMakeKeyPair(private_key_buf[1],public_key_buf[1]);

		cryptMakeSharedSecret(agree_buf[0], private_key_buf[0], public_key_buf[1]);
		cryptMakeSharedSecret(agree_buf[1], private_key_buf[1], public_key_buf[0]);
	}

}
#endif

#define swap_byte(x,y) t = (x); (x) = (y); (y) = t

void cryptInitRc4(rc4_key *key,unsigned char *key_data_ptr, int key_data_len)
{
	unsigned char t;
	unsigned char x;
	unsigned char y;
	unsigned char *state;
	int counter;

	state = &key->state[0];
	for(counter = 0; counter < 256; counter++)
		state[counter] = counter;
	x = key->x = 0;
	y = key->y = 0;
	for(counter = 0; counter < 256; counter++)
	{
		y += key_data_ptr[x] + state[counter];
		swap_byte(state[counter], state[y]);
		x = (x + 1) % key_data_len;
	}
}

void cryptRc4(rc4_key *key,unsigned char *buffer_ptr, int buffer_len)
{
	unsigned char t;
	unsigned char x;
	unsigned char y;
	unsigned char *state;
	int counter;

	x = key->x;
	y = key->y;
	state = key->state;
	for(counter = 0; counter < buffer_len; counter++)
	{
		x++;
		y += state[x];
		swap_byte(state[x], state[y]);
		buffer_ptr[counter] ^= state[(U8)(state[x] + state[y])];
	}
	key->x = x;
	key->y = y;
}

typedef struct EncryptionState
{
	RSAES_PKCS1v15_Encryptor *encrypt;
	RSAES_PKCS1v15_Decryptor *decrypt;
	RSASSA_PKCS1v15_SHA_Signer *signer;
	RSASSA_PKCS1v15_SHA_Verifier *verifier;
	LC_RNG *rnd_gen;
} EncryptionState;

unsigned int readBigEndianUInt(byte *in)
{
	byte temp[4];
	temp[0] = in[3];
	temp[1] = in[2];
	temp[2] = in[1];
	temp[3] = in[0];

	return *((unsigned int*)temp);
}

void extendedEuclidean(Integer a, Integer b, Integer *x, Integer *y)
{
	if (a % b == Integer::Zero())
	{
		*x = Integer::Zero();
		*y = Integer::One();
		return;
	}
	Integer x1, y1;
	extendedEuclidean(b, a % b, &x1, &y1);
	*x = y1;
	*y = x1 - (y1 * (a.DividedBy(b)));
}

unsigned int readMultiplePrecisionInteger(byte *in, unsigned int in_len, Integer &mpint)
{
	unsigned int mplen = readBigEndianUInt(in);
	if (mplen + 4 > in_len)
	{
		mpint = Integer::Zero();
		return 0;
	}
	in += 4;
	if (mplen > 0)
	{
		mpint = Integer(in, mplen);
	}
	else mpint = Integer::Zero();
	return mplen + 4;
}

void clearRSAKeys(EncryptionState *state)
{
	if (!state) return;

	if (state->decrypt)
	{
		delete state->decrypt;
		state->decrypt = NULL;
	}

	if (state->signer)
	{
		delete state->signer;
		state->signer = NULL;
	}

	if (state->encrypt)
	{
		delete state->encrypt;
		state->encrypt = NULL;
	}

	if (state->verifier)
	{
		delete state->verifier;
		state->verifier = NULL;
	}

	if (state->rnd_gen)
	{
		delete state->rnd_gen;
		state->rnd_gen = NULL;
	}
}

EncryptionState * cryptRSACreateState(void)
{
	return callocStruct(EncryptionState);
}

void cryptRSADestroyState(EncryptionState *state)
{
	clearRSAKeys(state);
	free(state);
}

void cryptLoadRSAPublicKey(EncryptionState *state, const char *pubKey)
{
	char* buffer = (char*) malloc (2048);
	char* pCur = buffer;
	Integer e,n;
	unsigned int len_read;
	S64 seed64;

	int len = decodeBase64String(pubKey, strlen(pubKey), buffer, 2048);
	pCur += 11;
	len -= 11; // skip the "ssh-rsa" variable length string
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, e);
	pCur += len_read;
	len -= len_read;

	len_read = readMultiplePrecisionInteger((byte*)pCur, len, n);
	pCur += len_read;
	len -= len_read;

	clearRSAKeys(state);

	GET_CPU_TICKS_64(seed64);
	state->rnd_gen = new LC_RNG((U32)seed64);

	state->encrypt = new RSAES_PKCS1v15_Encryptor(n, e);
	state->verifier = new RSASSA_PKCS1v15_SHA_Verifier(state->encrypt->GetTrapdoorFunction());
	
	free(buffer);
}

void cryptLoadRSAKeyPair(EncryptionState *state, const char *pubKey, const char *privKey)
{
	char* buffer = (char*) malloc (2048);
	char* pCur = buffer;
	Integer e,n,d,p,q,a;
	unsigned int len_read;
	S64 seed64;

	// Read public key values
	int len = decodeBase64String(pubKey, strlen(pubKey), buffer, 2048);
	pCur += 11;
	len -= 11; // skip the "ssh-rsa" variable length string
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, e);
	pCur += len_read;
	len -= len_read;

	len_read = readMultiplePrecisionInteger((byte*)pCur, len, n);
	pCur += len_read;
	len -= len_read;

	// Read private key values
	len = decodeBase64String(privKey, strlen(privKey), buffer, 2048);
	pCur = buffer;
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, d);
	pCur += len_read;
	len -= len_read;
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, p);
	pCur += len_read;
	len -= len_read;
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, q);
	pCur += len_read;
	len -= len_read;
	len_read = readMultiplePrecisionInteger((byte*)pCur, len, a);
	pCur += len_read;
	len -= len_read;

	Integer x,y;
	extendedEuclidean(p, q, &x, &y); // find y s.t. q*y == 1 mod p

	clearRSAKeys(state);

	GET_CPU_TICKS_64(seed64);
	state->rnd_gen = new LC_RNG((U32)seed64);

	state->decrypt = new RSAES_PKCS1v15_Decryptor(n, e, d, p, q, d % (p-1), d % (q-1), y);
	state->signer = new RSASSA_PKCS1v15_SHA_Signer(state->decrypt->GetTrapdoorFunction());

	state->encrypt = new RSAES_PKCS1v15_Encryptor(n, e);
	state->verifier = new RSASSA_PKCS1v15_SHA_Verifier(state->encrypt->GetTrapdoorFunction());
	
	free(buffer);
}

char * cryptRSAGenerateSignature(EncryptionState *state, const char *message)
{
	if (!state->signer)
		return NULL;

	size_t plainlen = strlen(message) + 1;
	size_t len = (size_t) state->signer->SignatureLength();
	char *buffer = (char*) calloc(sizeof(char), len);

	state->signer->SignMessage(*state->rnd_gen, (const byte*) message, (unsigned int) plainlen, (byte*) buffer);

	size_t base64len = (len + 2) / 3 * 4 + 1;
	char *encoded = (char*) malloc(base64len);

	// encode raw sig as Base64-String
	encodeBase64String((unsigned char*) buffer, len, encoded, base64len); 

	free(buffer);
	return encoded;
}

int cryptRSAVerifySignature(EncryptionState *state, const char *message, const char *signature)
{
	if (!state->verifier || !message)
		return -1;
	if (!signature)
		return 0;

	size_t len =  strlen(message) + 1;
	size_t siglen = state->verifier->SignatureLength();
	char *buffer = (char*) malloc(siglen);
	size_t bytesRead = decodeBase64String(signature, strlen(signature), buffer, siglen);
	if (bytesRead != siglen)
		return -1;

	bool verified = state->verifier->VerifyMessage((const byte*) message, (unsigned int) len, (const byte*) buffer);
	free(buffer);

	return verified ? 1 : 0;
}

size_t cryptRSACipherTextLength(EncryptionState *state)
{
	if (!state->encrypt)
		return 0;

	return (size_t) state->encrypt->CipherTextLength();
}

bool cryptRSAEncrypt(EncryptionState *state, const char * message, size_t message_size, char * out_buffer, size_t out_buffer_size)
{
	if (!state->encrypt)
		return false;

	assert(message_size <= state->encrypt->MaxPlainTextLength());
	assert(out_buffer_size >= cryptRSACipherTextLength(state));
	state->encrypt->Encrypt(*state->rnd_gen, (byte const*) message, (unsigned int) message_size, (byte*) out_buffer);
	return true;
}

bool cryptRSADecrypt(EncryptionState *state, const char * message, size_t message_size, char * out_buffer, size_t out_buffer_size)
{
	if (!state->decrypt)
		return false;

	assert(message_size <= cryptRSACipherTextLength(state));
	assert(out_buffer_size >= state->decrypt->MaxPlainTextLength());
	state->decrypt->Decrypt((const byte*) message, (byte*) out_buffer);
	return true;
}

int cryptGetBase64SHASize(void)
{
	SHA256 sha;
	return ((sha.DigestSize() + 2) / 3 * 4 + 1);
}

char * cryptCalculateSHAHash_s(const char *string, char *buffer, size_t buffer_size, bool bUseHex)
{
	if (!string)
		return NULL;

	SHA256 sha;
	size_t len = sha.DigestSize();
	char *digest = (char*) malloc(len);

	sha.CalculateDigest((byte*) digest, (byte*) string, (unsigned int) strlen(string));

	if (bUseHex)
	{
		size_t hexLen = len * 2 + 1;
		if (buffer_size < hexLen)
			return NULL;
		encodeHexString((unsigned char*) digest, len, buffer, buffer_size);
	}
	else
	{
		size_t base64len = (len + 2) / 3 * 4 + 1;
		if (buffer_size < base64len)
			return NULL;
		encodeBase64String((unsigned char*) digest, len, buffer, buffer_size);
	}
	
	free(digest);
	return buffer;
}

#define BUF_SIZE_FOR_RAW_HASH 64

char *cryptAddSaltToHash(const char *pExistingHash, const char *pSaltString, bool bCaseInsensitiveSalt, char *buffer, size_t buffer_size)
{
	char rawExistingHash[BUF_SIZE_FOR_RAW_HASH];
	char newHash[BUF_SIZE_FOR_RAW_HASH];
	int iRawExistingHashSize;
	char *pTempStringForConcatting = NULL;
	size_t i;
	SHA256 sha;
	size_t hashLen = sha.DigestSize();
	size_t base64len = (hashLen + 2) / 3 * 4 + 1;


	assert(BUF_SIZE_FOR_RAW_HASH >= hashLen);

	iRawExistingHashSize = decodeBase64String(pExistingHash, strlen(pExistingHash), SAFESTR(rawExistingHash));

	if (!iRawExistingHashSize)
	{
		return NULL;
	}

	estrStackCreate(&pTempStringForConcatting);
	if (bCaseInsensitiveSalt)
	{
		estrCopy2(&pTempStringForConcatting, pSaltString);
		string_tolower(pTempStringForConcatting);
		estrConcatf(&pTempStringForConcatting, ":%s", pExistingHash);
	}
	else
	{
		estrPrintf(&pTempStringForConcatting, "%s:%s", pSaltString, pExistingHash);
	}

	sha.CalculateDigest((byte*)newHash, (byte*) pTempStringForConcatting, estrLength(&pTempStringForConcatting));

	estrDestroy(&pTempStringForConcatting);

	for (i = 0; i < hashLen; i++)
	{
		newHash[i] ^= rawExistingHash[i];
	}

	if (buffer_size < base64len)
		return NULL;
	encodeBase64String((unsigned char*) newHash, hashLen, buffer, buffer_size);

	return buffer;
}




int cryptVerifySHAHash(const char *string, const char *hash)
{
	SHA256 sha;

	size_t len =  strlen(string);
	size_t digestlen = sha.DigestSize();
	char *buffer = (char*) malloc(digestlen);
	size_t bytesRead = decodeBase64String(hash, strlen(hash), buffer, digestlen);

	if (bytesRead != digestlen)
		return -1;

	bool verified = sha.VerifyDigest((const byte*) buffer, (const byte*) string, (unsigned int) len);
	free(buffer);

	return verified ? 1 : 0;
}

// ------------------------------------------------------------------------
// Base64 Encoding/Decoding
const static char sgBase64Table[64] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 
	'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z', 
	'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm', 
	'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z', 
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static char decodeBase64ToByte (const char encoded) {
	if ('A' <= encoded && encoded <= 'Z')
		return (encoded - 'A');
	if ('a' <= encoded && encoded <= 'z')
		return (encoded - 'a' + 26);
	if ('0' <= encoded && encoded <= '9')
		return (encoded - '0' + 52);
	if (encoded == '+')
		return 62;
	if (encoded == '/')
		return 63;
	if (encoded == '=')
		return 64;
	return 65;
}

size_t decodeBase64StringSize(SA_PRE_NN_STR const char *encoded)
{
	size_t src_len = strlen(encoded);
	size_t r = src_len * 6 / 8;

	if (src_len > 0 && encoded[src_len - 1] == '=') r--;
	if (src_len > 1 && encoded[src_len - 2] == '=') r--;

	return r;
}

int decodeBase64String(const char *encoded, size_t src_size, char *buffer, size_t buffer_size)
{
	int i;
	int index = 0;
	int offset = 0;
	char currentChar = 0;

	for (i=0; (size_t) i< src_size; i++)
	{
		char decoded = decodeBase64ToByte(encoded[i]);
		if (decoded == 65)
		{
			return 0;
		}
		if (decoded == 64)
			break;
		currentChar = currentChar << (8 - offset);
		currentChar = currentChar + (decoded >> MAX(0, offset - 2));
		offset += 6;
		if (offset >= 8)
		{
			if ((size_t) index >= buffer_size)
				return 0;

			buffer[index++] = currentChar;
			offset -= 8;
			currentChar = decoded & (0xFF >> (8 - offset));
		}
	}
	return index;
}

char encodeBase64OffsetToChar (const unsigned char offset)
{
	if (offset >= ARRAY_SIZE_CHECKED(sgBase64Table))
		return '=';

	return sgBase64Table[offset];
}

int encodeBase64String(const unsigned char *unencoded, size_t src_size, char *buffer, size_t buffer_size)
{
	size_t i = 0;
	int count = 0;
	char curChar = 0;
	int offset = 0;

	if (buffer_size < (src_size + 2) / 3 * 4 + 1)
	{
		return -1;
	}

	for (i=0; i < src_size; i+=3)
	{
		size_t iPadding = (i + 3 > src_size) ? (i + 3 - src_size) : 0;
		char encoded[4];
		U32 temp;
		switch(iPadding)
		{
		case 1:
			{
				// Pad '='
				temp = (((U32) unencoded[i]) << 16) | (((U32) unencoded[i+1]) << 8);
				encoded[0] = (temp >> 18) & 0x3F;
				encoded[1] = (temp >> 12) & 0x3F;
				encoded[2] = (temp >> 6)  & 0x3F;
				encoded[3] = -1;
				break;
			}
		case 2:
			{
				// Pad '=='
				temp = ((U32) unencoded[i]) << 16;
				encoded[0] = (temp >> 18) & 0x3F;
				encoded[1] = (temp >> 12) & 0x3F;
				encoded[2] = -1;
				encoded[3] = -1;
				break;
			}
		default:
			{
				temp = (((U32) unencoded[i]) << 16) | (((U32) unencoded[i+1]) << 8) | ((U32) unencoded[i+2]);
				encoded[0] = (temp >> 18) & 0x3F;
				encoded[1] = (temp >> 12) & 0x3F;
				encoded[2] = (temp >> 6)  & 0x3F;
				encoded[3] = temp & 0x3F;
			}
		}

		buffer[count++] = encodeBase64OffsetToChar(encoded[0]);
		buffer[count++] = encodeBase64OffsetToChar(encoded[1]);
		buffer[count++] = encodeBase64OffsetToChar(encoded[2]);
		buffer[count++] = encodeBase64OffsetToChar(encoded[3]);
	}
	buffer[count] = 0;
	return count;
}

// Convert Raw Bytes To/From Hexadecimal String
const static char sgHexTable[16] = {
	'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'
};

static char decodeHexCharToValue (const char encoded) {
	// Both uppercase and lowercase are OK
	if ('A' <= encoded && encoded <= 'Z')
		return (10 + (encoded - 'A'));
	if ('a' <= encoded && encoded <= 'z')
		return (10 + (encoded - 'a'));
	if ('0' <= encoded && encoded <= '9')
		return (encoded - '0');
	return -1;
}

// Hex string must contain an even number of characters to match byte offseting
int decodeHexString(const unsigned char *encoded, size_t src_size, char *buffer, size_t buffer_size)
{
	size_t i;
	int count = 0;
	
	if (buffer_size < src_size / 2) // round up, NULL terminator not necessary (since buffer is random bytes)
	{
		return -1;
	}

	if (buffer_size == src_size / 2 && src_size % 2)
	{
		return -1;
	}

	for (i=0; i<src_size; i++)
	{
		int val = decodeHexCharToValue(encoded[i]);
		if (val < 0)
			return -1;

		if (i % 2 == 0)
		{
			buffer[count] = val << 4;
		}
		else
		{
			buffer[count++] |= val & 0x0F;
		}
	}
	return count;
}

int encodeHexString(const unsigned char *unencoded, size_t src_size, char *buffer, size_t buffer_size)
{
	size_t i;
	int count = 0;

	if (buffer_size < src_size * 2 + 1) // include NULL terminator
	{
		return -1;
	}

	for (i=0; i<src_size; i++)
	{
		int index = (unencoded[i] >> 4) & 0x0F;
		buffer[count++] = sgHexTable[index];
		index = unencoded[i] & 0x0F;
		buffer[count++] = sgHexTable[index];
	}
	buffer[count] = 0;
	return count;
}

char *cryptHMACSHA1Create(const char *secret_key, const char *message, char *out_buf, size_t out_len)
{
#if !_PS3
	HMAC<SHA1> mac = HMAC<SHA1>((const byte *)secret_key, (unsigned int)strlen(secret_key));
	byte *mac_buf = (byte *)malloc(mac.DigestSize());
	mac.CalculateDigest(mac_buf, (const byte *)message, (unsigned int)strlen(message));
	encodeBase64String(mac_buf, mac.DigestSize(), out_buf, out_len);
#endif
	return out_buf;
}


int AESEncodeIntoBuffer(char key[32], const char *pData, int iDataSize, char *pBuffer, int iBufferSize)
{
	int iBlockCount;
	int iInternalBufferSize;

	byte *pWorkingBuf;
	int i;



	//need extra byte to store the actual data size
	iBlockCount = ((iDataSize + 1) + 15) / 16;
	iInternalBufferSize = iBlockCount * 16;

	if (iInternalBufferSize > iBufferSize)
	{
		return 0;
	}

	AESEncryption AESEncrypt((byte*)key, 32);

	pWorkingBuf = (byte*)ScratchAlloc(iInternalBufferSize);
	
	memcpy(pWorkingBuf, pData, iDataSize);
	pWorkingBuf[iInternalBufferSize - 1] = iDataSize % 16;

	for (i = 0; i < iBlockCount; i++)
	{
		AESEncrypt.ProcessBlock(pWorkingBuf + i * 16, (byte*)pBuffer + i * 16);
	}


	ScratchFree(pWorkingBuf);

	return iInternalBufferSize;
}


int AESDecodeIntoBuffer(char key[32], const char *pData, int iDataSize, char *pBuffer, int iBufferSize)
{

	if (iDataSize % 16 != 0)
	{
		return NULL;
	}

	AESDecryption AESDecrypt((byte*)key, 32);

	byte *pWorkingBuf = (byte*)ScratchAlloc(iDataSize);
	int iBlockCount = iDataSize / 16;
	int i;
	int iActualOutSize;
	int iBytesInLastBlock;

	for (i = 0; i < iBlockCount; i++)
	{
		AESDecrypt.ProcessBlock((byte*)pData + i * 16 , pWorkingBuf + i * 16);
	}

	iBytesInLastBlock = pWorkingBuf[iDataSize - 1];
	
	if (iBytesInLastBlock < 0 || iBytesInLastBlock > 15)
	{
		ScratchFree(pWorkingBuf);
		return 0;
	}

	iActualOutSize = (iBlockCount - 1) * 16 + iBytesInLastBlock;

	if (iActualOutSize > iBufferSize)
	{
		ScratchFree(pWorkingBuf);
		return 0;
	}


	memcpy(pBuffer, pWorkingBuf, iActualOutSize);
	ScratchFree(pWorkingBuf);

	return iActualOutSize;
}

int AESEncode_GetEncodeBufferSizeFromDataSize(int iDataSize)
{
	return ((iDataSize + 16) / 16) * 16;
}

int AESDecode_GetDecodeBufferSizeFromEncodedSize(int iEncodedSize)
{
	return iEncodedSize;
}


void *AESEncode(char key[32], const char *pData, int iDataSize, int *piOutSize)
{
	int iBufferSize = (((iDataSize + 1) + 15) / 16) * 16;
	char *pBuffer = (char*)malloc(iBufferSize);
	*piOutSize = AESEncodeIntoBuffer(key, pData, iDataSize, pBuffer, iBufferSize);
	if (!(*piOutSize))
	{
		free(pBuffer);
		return NULL;
	}

	return pBuffer;
}


void *AESDecode(char key[32], const char *pData, int iDataSize, int *piOutSize)
{
	char *pBuffer = (char*)malloc(iDataSize);
	*piOutSize = AESDecodeIntoBuffer(key, pData, iDataSize, pBuffer, iDataSize);
	if (!(*piOutSize))
	{
		free(pBuffer);
		return NULL;
	}

	return pBuffer;
}




// Like rand(), but secure.
int cryptSecureRand()
{
#if !PLATFORM_CONSOLE

	unsigned number;
	errno_t result;
	result = rand_s(&number);
	number >>= 1;
	if (result)
	{
		Errorf("Secure random failure!  Falling back to insecure rand()");
		return rand();
	}
	return number;

#else
	Errorf("cryptSecureRand() not implemented on this platform!  Falling back to insecure rand()");
	return rand();
#endif
}



#define SAMPLES_PER_BIT 8

static int GetBitFromMouseEntropy(void)
{
	int iLastX =0;
	int iLastY = 0;
	int iSameCount = 0;
	int iSamples[SAMPLES_PER_BIT];
	int iSampleCount = 0;
	int i;

	while (1)
	{
		POINT cursorPos;
		GetCursorPos(&cursorPos);

		if (cursorPos.x == iLastX && cursorPos.y == iLastY)
		{
			iSameCount++;
		}
		else
		{
			iSamples[iSampleCount++] = cursorPos.x ^ cursorPos.y ^ iSameCount;
			iSameCount = 0;

			if (iSampleCount == SAMPLES_PER_BIT)
			{
				int iXOR1 = 0, iXOR2 = 0;
				int iCount1, iCount2;
				
				for (i = 0; i < SAMPLES_PER_BIT / 2; i++)
				{
					iXOR1 ^= iSamples[i];
					iXOR2 ^= iSamples[i + SAMPLES_PER_BIT / 2];
				}

				iCount1 = countBitsFast(iXOR1);
				iCount2 = countBitsFast(iXOR2);

				iCount1 &= 1;
				iCount2 &= 1;

				if (iCount1 != iCount2)
				{
					return iCount1;
				}

				iSampleCount = 0;
			}
		}

		iLastX = cursorPos.x;
		iLastY = cursorPos.y;
	}
}

#define SECURE_RANDS_PER_HASH 16
#define MOUSE_BITS_PER_SECURE_RAND 4

void cryptGetRandomBitsWithMouseEntropyAndAllSortsOfCrazyStuff(U8 *pOutData, int iDataSize)
{
	SHA256 sha;
	int iHashSize = sha.DigestSize();
	int iNumHashesNeeded = (iDataSize + (iHashSize - 1)) / iHashSize;
	int iDataSizeInternal = iNumHashesNeeded * iHashSize;
	int iHashCount;
	int iSecureRandCount;
	int iMouseBitCount;
	int iSecureRands[SECURE_RANDS_PER_HASH];
	int iTotalMouseBits = iNumHashesNeeded * SECURE_RANDS_PER_HASH * MOUSE_BITS_PER_SECURE_RAND;
	
	printf("Need %d bits of mouse entropy, please...\n", iTotalMouseBits);

	assert(iDataSizeInternal % 4 == 0);

	byte *pInternalData = (byte*)malloc(iDataSizeInternal);

	for (iHashCount = 0; iHashCount < iNumHashesNeeded; iHashCount++)
	{
		for (iSecureRandCount = 0; iSecureRandCount < SECURE_RANDS_PER_HASH; iSecureRandCount++)
		{
			iSecureRands[iSecureRandCount] = cryptSecureRand();
			for (iMouseBitCount = 0; iMouseBitCount < MOUSE_BITS_PER_SECURE_RAND; iMouseBitCount++)
			{
				iTotalMouseBits--;
				printf("%d more...\n", iTotalMouseBits);
				iSecureRands[iSecureRandCount] ^= GetBitFromMouseEntropy() << iMouseBitCount;
			}
		}

		sha.CalculateDigest(pInternalData + iHashCount * iHashSize, (byte*)iSecureRands, sizeof(iSecureRands));
	}

	memcpy(pOutData, pInternalData, iDataSize);
	free(pInternalData);
}











} // extern "C" 
