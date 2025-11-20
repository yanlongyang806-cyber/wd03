#include "AccountLoginDecrypt.h"
#include "AccountLoginDecrypt_c_ast.h"

#include "AccountServer.h"
#include "accountnet.h"
#include "accountnet_h_ast.h"
#include "crypt.h"
#include "MultiWorkerThread.h"
#include "structDefines.h"
#include "OsPermissions.h"
#include "file.h"
#include <Windows.h>


/************************************************************************/
/* Command-line options                                                 */
/************************************************************************/

// Maximum input queue size
static int giDecryptThreadsInMax = 10000;
AUTO_CMD_INT(giDecryptThreadsInMax, DecryptThreadsInMax) ACMD_CMDLINE;

// Maximum output queue size
static int giDecryptThreadsOutMax = 10000;
AUTO_CMD_INT(giDecryptThreadsOutMax, DecryptThreadsOutMax) ACMD_CMDLINE;

// Number of threads with which to decrypt passwords
static int giDecryptThreads = 2;
AUTO_CMD_INT(giDecryptThreads, DecryptThreads) ACMD_CMDLINE;

// Decrypt passwords; if false, just give back what was passed in
static bool gbDecryptPasswords = true;
AUTO_CMD_INT(gbDecryptPasswords, DecryptPasswords) ACMD_CMDLINE;

// Don't load the keys on startup
static bool gbDontLoadKeys = false;
AUTO_CMD_INT(gbDontLoadKeys, DontLoadKeys) ACMD_CMDLINE;


/************************************************************************/
/* Globals                                                              */
/************************************************************************/

// A pad that is XOR'd with the private keys loaded from disk.
// This was generated as a bogus 1024-bit private key.
//
// !!! WARNING: Changing this will prevent existing keys from working!!!
static const char gszPrivKeyPad[] = "\
AAAAgAzDOuxx5VnSBY6JouvGpS1p0DtIzy04yJdL8+OEEUWlF2Gi1tWJ3K4ULEnU\
Reub81yiEiWUKQn7tsPrZymZM4RTvMhmdEVFMlB5xfn/6xkvJYALEyrzehKYuaOd\
+YQih4BnpQmxet8IUodV2wAUco91Hpi5UXZdXQyqPrOcHWJVAAAAQQDVO42ygDnh\
YD/AL3nuxSpkyqsp0dq3r3dJlVO5vN7S7JBX0YYIufo+hDY8Geqc2y43HRvol8Me\
/mCL6AbE/djdAAAAQQC8+dfKXsRPA2k4SnZZUSf0omor/H1iZIszWNwLm/5YpZKP\
tRUffMJ0SJLKHlfzgPWO/3nEaqigWNGzlRLvQ1yrAAAAQHQoDxVgY1IwZH8M34hw\
COZh15nVYkb/dQ7lvAcMXFLXl9sB/CxD0cunexxOYMXucksrdm6MDK4hUKHvHQkd\
rS8=";

static const size_t guIgnoredKeys = 2; // none & identity are ignored
static STRING_EARRAY geaszPrivKeys = NULL;

static MultiWorkerThreadManager * gpDecryptThreadManager = NULL;

static void ald_ProcessOutput(void * pUserData);
static void ald_ProcessInput(void * pUserData);

extern bool bKeyFileSecurity;


/************************************************************************/
/* Init/deinit                                                          */
/************************************************************************/

static void ald_DecryptKey(SA_PARAM_NN_STR char * szEncryptionKey)
{
	static char szRawPad[4096] = {0};
	char szRawKey[4096] = {0};
	size_t uCurChar = 0;
	size_t uEncryptionKeySize = 0;
	size_t uMaxChar = 0;
	
	PERFINFO_AUTO_START_FUNC();

	ATOMIC_INIT_BEGIN;

	decodeBase64String(SAFESTR(gszPrivKeyPad), SAFESTR(szRawPad));

	ATOMIC_INIT_END;

	uEncryptionKeySize = strlen(szEncryptionKey);
	uMaxChar = decodeBase64StringSize(szEncryptionKey);

	decodeBase64String(szEncryptionKey, uEncryptionKeySize, SAFESTR(szRawKey));

	for (uCurChar = 0; uCurChar < uMaxChar; uCurChar++)
	{
		szRawKey[uCurChar] ^= szRawPad[uCurChar];
	}

	encodeBase64String(szRawKey, uMaxChar, szEncryptionKey, uEncryptionKeySize + 1); // +1 for NULL

	PERFINFO_AUTO_STOP();
}

static void ald_LoadKeys(void)
{
	size_t uNumKeys = ASKEY_MAX - guIgnoredKeys;
	size_t uCurKey = 0;

	PERFINFO_AUTO_START_FUNC();

	assert(!geaszPrivKeys);

	// Get access to open the file, if we need it.
	if (bKeyFileSecurity && !EnableBypassReadAcls(true))
	{
		StartupFail("Unable to get read privilege");
		PERFINFO_AUTO_STOP();
		return;
	}

	eaCreate(&geaszPrivKeys);

	for (uCurKey = 0; uCurKey < uNumKeys; uCurKey++)
	{
		char * szEncryptionKey = NULL;
		const char * szKeyName = StaticDefineIntRevLookup(AccountServerEncryptionKeyVersionEnum,
			(int)(uCurKey + guIgnoredKeys)); // skip over special cases
		int iFileSize = 0;
		char szFileName[FILENAME_MAX] = {0};

		assert(szKeyName);

		if (!isAccountServerLive() && strStartsWith(szKeyName, "prod_"))
		{
			// Skip "prod_" keys if this isn't the LIVE Account Server
			eaPush(&geaszPrivKeys, NULL); // push NULL, so that the array indexing doesn't have to care
			continue;
		}

		sprintf(szFileName, "server\\AccountServer\\Keys\\%s.key", szKeyName);
		szEncryptionKey = fileAlloc(szFileName, &iFileSize);

		if (!szEncryptionKey)
		{
			StartupFail("Unable to load key: %s", szFileName);
			PERFINFO_AUTO_STOP();
			return;
		}

		ald_DecryptKey(szEncryptionKey);

		eaPush(&geaszPrivKeys, szEncryptionKey);
	}

	// Relinquish special access.
	if (bKeyFileSecurity && !EnableBypassReadAcls(false)) {
		StartupFail("Unable to relinquish read privilege");
	}

	PERFINFO_AUTO_STOP();
}

static void ald_DestroyKeys(void)
{
	PERFINFO_AUTO_START_FUNC();

	assert(geaszPrivKeys);

	eaDestroyEx(&geaszPrivKeys, NULL);

	PERFINFO_AUTO_STOP();
}

void ALDInit(void)
{
	if (gbDontLoadKeys) return;

	PERFINFO_AUTO_START_FUNC();

	assert(!gpDecryptThreadManager);

	ald_LoadKeys();

	gpDecryptThreadManager = mwtCreate(giDecryptThreadsInMax, giDecryptThreadsOutMax,
		giDecryptThreads, NULL, NULL, ald_ProcessInput, ald_ProcessOutput, "AccountDecrypt");

	PERFINFO_AUTO_STOP();
}

void ALDDeinit(void)
{
	// Note that this will NOT clean up memory entirely!
	// This will leak encryption state stored in each thread

	if (!gpDecryptThreadManager) return;

	PERFINFO_AUTO_START_FUNC();

	mwtProcessOutputQueue(gpDecryptThreadManager);
	mwtSleepUntilDone(gpDecryptThreadManager);
	mwtProcessOutputQueue(gpDecryptThreadManager);
	mwtDestroy(gpDecryptThreadManager);
	gpDecryptThreadManager = NULL;

	ald_DestroyKeys();

	PERFINFO_AUTO_STOP();
}


/************************************************************************/
/* Decryption logic                                                     */
/************************************************************************/

AUTO_STRUCT;
typedef struct ALDData
{
	AccountServerEncryptionKeyVersion eKeyVersion;
	char szEncryptedPassword[MAX_PASSWORD_ENCRYPTED_BASE64];
	char szPlaintextPassword[MAX_PASSWORD_PLAINTEXT];
	AccountDecryptPasswordCB pCallback; NO_AST
	void * pUserData; NO_AST
} ALDData;

static void ald_ProcessOutput(void * pUserData)
{
	ALDData * pData = pUserData;

	PERFINFO_AUTO_START_FUNC();

	pData->pCallback(pData->szPlaintextPassword, pData->pUserData);

	StructDestroy(parse_ALDData, pData);

	PERFINFO_AUTO_STOP();
}

static void ald_ProcessInput(void * pUserData)
{
	ALDData * pData = pUserData;
	EncryptionState * * * pppEncryptionState = NULL;
	char szEncryptedRaw[MAX_PASSWORD_ENCRYPTED] = {0};

	STATIC_THREAD_ALLOC(pppEncryptionState);

	PERFINFO_AUTO_START_FUNC();

	if (!*pppEncryptionState)
	{
		*pppEncryptionState = calloc(eaSize(&geaszPrivKeys), sizeof(**pppEncryptionState));

		EARRAY_CONST_FOREACH_BEGIN(geaszPrivKeys, iCurKey, iNumKeys);
		{
			if (!geaszPrivKeys[iCurKey]) continue; // skip unloaded prod_ keys etc.

			(*pppEncryptionState)[iCurKey] = cryptRSACreateState();
			cryptLoadRSAKeyPair((*pppEncryptionState)[iCurKey],
				accountNetGetPubKey(iCurKey + (int)guIgnoredKeys),
				geaszPrivKeys[iCurKey]);
		}
		EARRAY_FOREACH_END;
	}

	if (devassert(geaszPrivKeys[pData->eKeyVersion - guIgnoredKeys]))
	{
		decodeBase64String(SAFESTR(pData->szEncryptedPassword), SAFESTR(szEncryptedRaw));

		assert(pData->eKeyVersion - guIgnoredKeys >= 0);
		cryptRSADecrypt((*pppEncryptionState)[pData->eKeyVersion - guIgnoredKeys],
			SAFESTR(szEncryptedRaw), SAFESTR(pData->szPlaintextPassword));

		mwtQueueOutput(gpDecryptThreadManager, pData);
	}

	PERFINFO_AUTO_STOP();
}

void ALDGetPassword(
	AccountServerEncryptionKeyVersion eKeyVersion,
	SA_PARAM_NN_STR const char * szEncryptedPassword,
	SA_PARAM_NN_VALID AccountDecryptPasswordCB pCallback,
	void * pUserData)
{
	ALDData * pData = NULL;

	PERFINFO_AUTO_START_FUNC();

	if (!gpDecryptThreadManager || !gbDecryptPasswords ||
		eKeyVersion == ASKEY_none || eKeyVersion == ASKEY_identity)
	{
		pCallback(szEncryptedPassword, pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	if (eKeyVersion - (int)guIgnoredKeys < 0 ||
		eKeyVersion - (int)guIgnoredKeys >= eaSize(&geaszPrivKeys) ||
	    !geaszPrivKeys[eKeyVersion - guIgnoredKeys])
	{
		AssertOrAlert("AS_DECRYPT_INVALID_KEY", "Requested decryption with invalid key: %d (%s)",
			eKeyVersion, StaticDefineIntRevLookupNonNull(AccountServerEncryptionKeyVersionEnum, eKeyVersion));
		pCallback(szEncryptedPassword, pUserData);
		PERFINFO_AUTO_STOP();
		return;
	}

	pData = StructCreate(parse_ALDData);
	pData->eKeyVersion = eKeyVersion;
	pData->pCallback = pCallback;
	pData->pUserData = pUserData;
	strcpy(pData->szEncryptedPassword, szEncryptedPassword);

	mwtQueueInput(gpDecryptThreadManager, pData, true);
		
	PERFINFO_AUTO_STOP();
}

void ALDProcessQueue(void)
{
	if (gpDecryptThreadManager)
	{
		mwtProcessOutputQueue(gpDecryptThreadManager);
	}
}


#include "AccountLoginDecrypt_c_ast.c"