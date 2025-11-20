#include "AccountEncryption.h"
#include "GlobalData.h"
#include "EString.h"
#include "earray.h"
#include "fileutil2.h"
#include "StringUtil.h"
#include "crypt.h"
#include "logging.h"
#include "objTransactions.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "Alerts.h"
#include "utilitiesLib.h"
#include "AccountServer.h"
#include "LocalTransactionManager.h"
#include "AutoTransDefs.h"
#include "objContainer.h"
#include "winInclude.h"
#include "AccountManagement.h"
#include "HttpXpathSupport.h"
#include "AccountEncryption_c_ast.h"
#include "AccountIntegration.h"
#include "OsPermissions.h"
#include "StayUp.h"

#define KEY_SIZE 32

static const char *spVerificationString = "CrypticRules!";
static const int siVerificationStringLength = 13;
static bool sbInitted = false;

static ContainerID *spAccountIDsToFixup = NULL;
static ContainerID *spPerfectWorldBatchIDsToFixup = NULL;

static bool sbCreatePasswordBackups = false;
AUTO_CMD_INT(sbCreatePasswordBackups, CreatePasswordBackups) ACMD_COMMANDLINE;

static bool sbDestroyPasswordBackups = false;
AUTO_CMD_INT(sbDestroyPasswordBackups, DestroyPasswordBackups) ACMD_COMMANDLINE;


char *pDirForEncryptionKeys = NULL;
AUTO_CMD_ESTRING(pDirForEncryptionKeys, DirForEncryptionKeys) ACMD_COMMANDLINE;

// Bypass operating system read ACLs on key files.
bool bKeyFileSecurity = false;
AUTO_CMD_INT(bKeyFileSecurity, KeyFileSecurity) ACMD_COMMANDLINE;


//if we need to revert to backup passwords, we also always reset the password version and encryption key
//number
static bool sbRestorePasswordBackups = false;
static int siPasswordVersionToResetTo;
static int siEncryptionKeyToResetTo;

AUTO_COMMAND ACMD_COMMANDLINE;
void RevertToBackupPasswords(int iPasswordVersion, int iEncryptionKey)
{
	sbRestorePasswordBackups = true;
	siPasswordVersionToResetTo = iPasswordVersion;
	siEncryptionKeyToResetTo = iEncryptionKey;
}

typedef struct IndexedKey
{
	int iIndex;
	char keyData[KEY_SIZE]; //not a string
} IndexedKey;

static IndexedKey **sppIndexedKeys = NULL;

int AccountEncryption_GetCurEncryptionKeyIndex(void)
{
	IndexedKey *pHighestKey = eaTail(&sppIndexedKeys);
	if (!sbInitted)
	{
		ONCE(AssertOrAlert("AS_STARTUP_ORDER_BAD", "Trying to call AccountEncryption_GetCurEncryptionKeyIndex before calling AccountEncryption_Startup, this is illegal"));
		return 0;
	}
	if (pHighestKey)
	{
		return pHighestKey->iIndex;
	}

	return 0;
}

char *AccountEncryption_FindKeyFromIndex(int iIndex)
{
	int i;

	for (i = eaSize(&sppIndexedKeys) - 1; i >= 0; i--)
	{
		if (sppIndexedKeys[i]->iIndex == iIndex)
		{
			return sppIndexedKeys[i]->keyData;
		}
	}

	return NULL;
}

int cmpIndexedKey(const IndexedKey **left, const IndexedKey **right)
{
	return (*left)->iIndex - (*right)->iIndex;
}

static void WaitForTypedWord(char *pWord)
{
	int iLen = (int)strlen(pWord);
	int iIndex = 0;

	while (1)
	{
		char ch = _getch();
		if (ch == pWord[iIndex])
		{
			iIndex++;
			if (iIndex == iLen)
			{
				return;
			}
		}
		else
		{
			iIndex = 0;
		}
	}
}


static void GetHumanVerification(FORMAT_STR const char* format, ...)
{
	char *pFullString = NULL;

	if (!isAccountServerLikeLive())
		return;

	if (!objCountTotalContainersWithType(GLOBALTYPE_ACCOUNT) && !objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT))
		return;

	estrGetVarArgs(&pFullString, format);
	log_printf(LOG_ACCOUNTLOG, "Checking human verification for: %s", pFullString);
	consolePushColor();
	consoleSetColor(0, COLOR_GREEN | COLOR_BRIGHT);
	printf("%s\n", pFullString);
	consolePopColor();
	estrDestroy(&pFullString);
	printf("Type \"yes\" to proceed\n");
	WaitForTypedWord("yes");
	printf("Proceeding...\n");
	log_printf(LOG_ACCOUNTLOG, "Human verification recived... proceeding");
}

char *DecodeBase64ThenAES(char key[32], const char *pBase64EncodedString, int *piOutDecodedSize)
{
	char *pTemp = NULL;
	char *pRetVal;
	estrBase64Decode(&pTemp, pBase64EncodedString, (int)strlen(pBase64EncodedString));
	if (!estrLength(&pTemp))
	{
		estrDestroy(&pTemp);
		return NULL;
	}

	pRetVal = AESDecode(key, pTemp, estrLength(&pTemp), piOutDecodedSize);
	estrDestroy(&pTemp);
	return pRetVal;
}

bool EncodeAESThenBase64(char key[32], const char *pData, int iDataSize, char **ppOutEString)
{
	int iEncodedSize;
	char *pEncoded = AESEncode(key, pData, iDataSize, &iEncodedSize);
	if (!pEncoded)
	{
		return false;
	}

	estrBase64Encode(ppOutEString, pEncoded, iEncodedSize);
	free(pEncoded);
	return true;
}

char keyKey[33] = "ndfsgh84w5ypidfgjghtguyhopukj;l4";

static void AddKeyFromFile(const char *pFileName, int iKeyIndex)
{
	bool success;
	int iFileSize;
	char *pBuf;
	char key[32];
	char oversizedKeyBuf[48];
	IndexedKey *pIndexedKey;

	// Get access to open the file, if we need it.
	if (bKeyFileSecurity)
	{
		success = EnableBypassReadAcls(true);
		if (!success)
			StartupFail("Unable to get read privilege");
	}

	// Open and read key file.
	pBuf = fileAlloc(pFileName, &iFileSize);

	// Relinquish special access.
	if (bKeyFileSecurity)
	{
		success = EnableBypassReadAcls(false);
		if (!success)
			StartupFail("Unable to relinquish read privilege");
	}

	if (!pBuf)
	{
		StartupFail("Couldn't open encryption key file %s", pFileName);
	}

	ANALYSIS_ASSUME(pBuf);
	if (decodeBase64String(pBuf, iFileSize, key, 32) != 32)
	{
		StartupFail("Couldn't 64-bit decode contents of key file %s into 32-byte key buffer... either badly formatted, or too big",
			pFileName);
	}

	

	pIndexedKey = calloc(sizeof(IndexedKey), 1);
	pIndexedKey->iIndex = iKeyIndex;

	//AESEncodeIntoBuffer does some magic we don't need involving encoding in the original size, we'll just truncate that
	//crap back off for this application
	if (!AESEncodeIntoBuffer(keyKey, key, 32, oversizedKeyBuf, 48))
	{
		StartupFail("Couldn't AESEncode the key we loaded from %s", pFileName);
	}

	memcpy(pIndexedKey->keyData, oversizedKeyBuf, 32);

	eaPush(&sppIndexedKeys, pIndexedKey);

	free(pBuf);
}

static void CheckOldKeys(int iMaxIndexToCheck)
{
	int i;
	CONST_EARRAY_OF(EncryptionKeyVerificationString) ppVerificationStrings = asgGetEncryptionKeyVerificationStrings();
	int *pKeysToRemoveFromDatabase = NULL;

	for (i = 0; i < eaSize(&sppIndexedKeys) && sppIndexedKeys[i]->iIndex <= iMaxIndexToCheck; i++)
	{
		if (!eaIndexedGetUsingInt(&ppVerificationStrings, sppIndexedKeys[i]->iIndex))
		{
			StartupFail("Old encryption key %d (current key is %d) exists on disk but not in the database, this indicates database corruption of some sort",
				sppIndexedKeys[i]->iIndex, sppIndexedKeys[eaSize(&sppIndexedKeys) - 1]->iIndex);
		}
	}

	for (i = 0; i < eaSize(&ppVerificationStrings) && ppVerificationStrings[i]->iKeyIndex <= iMaxIndexToCheck; i++)
	{
		if (!AccountEncryption_FindKeyFromIndex(ppVerificationStrings[i]->iKeyIndex))
		{
			GetHumanVerification("Old encryption key %d seems to have been removed from %s, and its verification string will now be removed. You should only have done this if you have verified, via watching in the servermonitor that all fixup was completed during a previous run, that it is no longer used to encrypt any accounts. Have you done so?",
				ppVerificationStrings[i]->iKeyIndex, pDirForEncryptionKeys);
			ea32Push(&pKeysToRemoveFromDatabase, ppVerificationStrings[i]->iKeyIndex);
		}
	}

	for (i = 0; i < ea32Size(&pKeysToRemoveFromDatabase); i++)
	{
		TransactionReturnVal result = {0};
	
		AutoTrans_trRemoveEncryptionKeyVerificationString(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pKeysToRemoveFromDatabase[i]);

		if (result.eOutcome != TRANSACTION_OUTCOME_SUCCESS)
		{
			StartupFail("Unable to remove verification encoding for key %d", pKeysToRemoveFromDatabase[i]);
		}

		ReleaseReturnValData(objLocalManager(), &result);
	}
	ea32Destroy(&pKeysToRemoveFromDatabase);


}




static void AddVerificationString(U32 iIndex, char key[32])
{
	char *pEncoded = NULL;
	TransactionReturnVal result = {0};
	
	if (!EncodeAESThenBase64(key, spVerificationString, siVerificationStringLength, &pEncoded))
	{
		StartupFail("Unable to do verification encoding for key %d", iIndex);
	}


	AutoTrans_trAddEncryptionKeyVerificationString(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, iIndex, pEncoded);

	if (result.eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		StartupFail("Unable to add verification encoding for key %d", iIndex);
	}
}



//scan DirForEncryptionKeys for all files named AccountServerKey.n, verify that there's at least one of them,
//and that however many there are, they are contiguous integers, and that each one contains a 64-byte encoding of
//a 32-byte key
static void LoadIndexedKeys(void)
{
	CONST_EARRAY_OF(EncryptionKeyVerificationString) ppVerificationStrings = asgGetEncryptionKeyVerificationStrings();
	char **ppFiles = fileScanDirNoSubdirRecurse(pDirForEncryptionKeys);
	int i;
	char *pName = NULL;
	char *pExt = NULL;
	int iHighestKeyFromDisk;
	int iHighestKeyFromContainer;

	estrStackCreate(&pName);
	estrStackCreate(&pExt);

	for (i = 0; i < eaSize(&ppFiles); i++)
	{
		int iKeyNum;
		estrClear(&pName);
		estrClear(&pExt);
		estrGetDirAndFileNameAndExtension(ppFiles[i], NULL, &pName, &pExt);

		if (stricmp(pName, "ASKey") == 0 && StringToInt_Paranoid(pExt, &iKeyNum))
		{
			if (iKeyNum == 0)
			{
				StartupFail("Found a key file named %s... this is illegal because 0 is not a legal key index", 
					ppFiles[i]);
			}

			AddKeyFromFile(ppFiles[i], iKeyNum);
		}
	}

	fileScanDirFreeNames(ppFiles);

	estrDestroy(&pName);
	estrDestroy(&pExt);

	if (!eaSize(&sppIndexedKeys))
	{
		StartupFail("DirForEncryptionKeys specified %s, but found no files named ASKey.n in that folder",
			pDirForEncryptionKeys);
	}

	eaQSort(sppIndexedKeys, cmpIndexedKey);

	//various cases that can be wrong.... first check that this is a contiguous set of integers
	for (i = 1; i < eaSize(&sppIndexedKeys); i++)
	{
		if (sppIndexedKeys[i]->iIndex == sppIndexedKeys[i-1]->iIndex)
		{
			StartupFail("While loading keys from %s, found two copies of key %d, this is extremely illegal",
				pDirForEncryptionKeys, sppIndexedKeys[i]->iIndex);
		}

		if (sppIndexedKeys[i]->iIndex != sppIndexedKeys[i-1]->iIndex + 1)
		{
			StartupFail("While loading keys from %s, found key %d and key %d but nothing in between... keys must be a contiguous range of integers",
				pDirForEncryptionKeys, sppIndexedKeys[i-1]->iIndex, sppIndexedKeys[i]->iIndex);
		}		
	}

	//for each key loaded from disk, ensure that there's not a verification mismatch (check existence later)
	for (i = 0; i < eaSize(&sppIndexedKeys); i++)
	{
		EncryptionKeyVerificationString *pVerificationString = eaIndexedGetUsingInt(&ppVerificationStrings, sppIndexedKeys[i]->iIndex);
		if (pVerificationString)
		{
			int iDecodedLen;
			void *pDecoded = DecodeBase64ThenAES(sppIndexedKeys[i]->keyData, pVerificationString->encodedStringForComparing, &iDecodedLen);
			if (!pDecoded)
			{
				StartupFail("While loading keys, couldn't decode verification string for key %d, indicating on-disk key corruption", sppIndexedKeys[i]->iIndex);
			}

			if (iDecodedLen != siVerificationStringLength || memcmp(pDecoded, spVerificationString, siVerificationStringLength) != 0)
			{
				StartupFail("While loading keys, verification string check failed for key %d, indicating on-disk key corruption", sppIndexedKeys[i]->iIndex);
			}
		}
	}

	
	iHighestKeyFromDisk = sppIndexedKeys[eaSize(&sppIndexedKeys) - 1]->iIndex;

	if (eaSize(&ppVerificationStrings))
	{
		iHighestKeyFromContainer = ppVerificationStrings[eaSize(&ppVerificationStrings) - 1]->iKeyIndex;
	}
	else
	{
		iHighestKeyFromContainer = 0;
	}



	if (iHighestKeyFromDisk == iHighestKeyFromContainer)
	{
		//if highest key index matches highest asgGetEncryptionKeyVerificationStrings index, then we only need
		//to check the old keys to see if any are now fully obsolete and can be removed from the KeyVerificationStrings
		CheckOldKeys(iHighestKeyFromDisk - 1);
	}
	else
	{
		if (iHighestKeyFromDisk == 1 && iHighestKeyFromContainer == 0)
		{
			GetHumanVerification("It appears that password encryption was not being used for this AS but now is. If this is not the intended case, something is very very wrong");
			AddVerificationString(1, sppIndexedKeys[0]->keyData);
		}
		else
		{
			if (iHighestKeyFromDisk == iHighestKeyFromContainer + 1)
			{
				char *pNewKey = NULL;
				char *pOldKey = NULL;
				if (!AccountEncryption_FindKeyFromIndex(iHighestKeyFromContainer))
				{
					StartupFail("It looks like the AS should be migrating from encryption key %d to encryption key %d. However, %s does not contain ASKey.%d, which is necessary for that migration to take place",
						iHighestKeyFromContainer, iHighestKeyFromDisk, pDirForEncryptionKeys, iHighestKeyFromContainer);
				}


				pNewKey = AccountEncryption_FindKeyFromIndex(iHighestKeyFromDisk);
				pOldKey = AccountEncryption_FindKeyFromIndex(iHighestKeyFromDisk - 1);

				if (!pNewKey || !pOldKey)
				{
					StartupFail("Unable to find both old and new key when potentially migrating from key %d to key %d",
						iHighestKeyFromDisk - 1, iHighestKeyFromDisk);
				}

				if (memcmp(pNewKey, pOldKey, KEY_SIZE) == 0)
				{
					StartupFail("A new encryption key (%d) has been added, but it appears to be identical to key %d, this is presumably pointless",
						iHighestKeyFromDisk, iHighestKeyFromDisk - 1);
				}


				GetHumanVerification("It appears that a new encryption key, %d, has been added. This will cause the AS to reencrypt all passwords with this new key.",
					iHighestKeyFromDisk);
				AddVerificationString(iHighestKeyFromDisk, pNewKey);

				//now verified that (for instance) 5 and 6 both exist on disk, only up to 5 exist in the container, so migration is legal, but
				//still need to check old keys
				CheckOldKeys(iHighestKeyFromContainer - 1);


			}
			else
			{
				StartupFail("Something is wrong with encryption keys. Internally, the AS knows about keys up through %d, but has now found key %d in %s. Keys must increase by 1 each time",
					iHighestKeyFromContainer, iHighestKeyFromDisk, pDirForEncryptionKeys);
			}
		}
	}
}

void AccountEncryption_ForceEncryptionKeyIndex(int iNewIndex)
{

	TransactionReturnVal result = {0};
	CONST_EARRAY_OF(EncryptionKeyVerificationString) ppVerificationStrings = asgGetEncryptionKeyVerificationStrings();
	char filename[CRYPTIC_MAX_PATH];
	int *pHigherIndices = NULL;
	int i;
	int iHighIndex;

	if (iNewIndex == 0 && pDirForEncryptionKeys)
	{
		StartupFail("Asked to restore backups and reset to encryption key 0, but DirForEncryptionKeys is set... this is illegal");
	}

	if (iNewIndex && !pDirForEncryptionKeys)
	{
		StartupFail("Asked to restore backups and reset to encryption key %d, but DirForEncryptionKeys is not set... this is illegal",
			iNewIndex);
	}

	if (iNewIndex)
	{

		sprintf(filename, "%s/ASKey.%d", pDirForEncryptionKeys, iNewIndex);
		if (!fileExists(filename))
		{
			StartupFail("Asked to restore backups and reset to encryption key %d, but %s doesn't seem to exist", iNewIndex, filename);
		}
	}


	FOR_EACH_IN_EARRAY(ppVerificationStrings, EncryptionKeyVerificationString, pKey)
	{
		if (pKey->iKeyIndex > iNewIndex)
		{
			ea32Push(&pHigherIndices, pKey->iKeyIndex);
		}
	}
	FOR_EACH_END;

	if (pDirForEncryptionKeys)
	{
		for (i = 0; i < ea32Size(&pHigherIndices); i++)
		{
			iHighIndex = pHigherIndices[i];
			sprintf(filename, "%s/ASKey.%d", pDirForEncryptionKeys, iHighIndex);
			if (fileExists(filename))
			{
				StartupFail("Asked to restore backups and reset to encryption key %d, but %s exists and has a higher index, this is illegal",
					iNewIndex, filename);
			}
		}
	}

	for (i = 0; i < ea32Size(&pHigherIndices); i++)
	{
		AutoTrans_trRemoveEncryptionKeyVerificationString(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, pHigherIndices[i]);
	}

	ea32Destroy(&pHigherIndices);
}

//at startup, verify that the keys loaded from DirForEncryptionKeys match the encryption verification strings
//stored in the global data as closely as possible, fail in a very visible fashion if they don't, do any
//necessary fixup otherwise
void AccountEncryption_Startup(void)
{
	int iNumPasswordBackupThingsToDo;

	CONST_EARRAY_OF(EncryptionKeyVerificationString) ppVerificationStrings = asgGetEncryptionKeyVerificationStrings();
	if (sbInitted)
	{
		return;
	}
	
	sbInitted = true;

	//on stayup past 1, don't do create/destroy/restore
	if (StayUpCount() > 1)
	{
		sbCreatePasswordBackups = sbDestroyPasswordBackups = sbRestorePasswordBackups = false;
	}

	iNumPasswordBackupThingsToDo = (int)(!!sbCreatePasswordBackups) + (int)(!!sbDestroyPasswordBackups) + (int)(!!sbRestorePasswordBackups);

	if (iNumPasswordBackupThingsToDo > 1)
	{
		StartupFail("AS being asked to do two or more of create/destroy/restore password backups at once... this is not allowed");
	}

	if (!iNumPasswordBackupThingsToDo)
	{
		U32 iBackupCreationTime = asgGetPasswordBackupCreationTime();
		if (iBackupCreationTime)
		{
			char *pTemp = NULL;
			timeSecondsDurationToPrettyEString(timeSecondsSince2000() - iBackupCreationTime, &pTemp);
			WARNING_NETOPS_ALERT("PWORD_BACKUPS_EXIST", "Password backups were created %s ago and still exist. For security reasons, they should not continue to exist forever",
				pTemp);
			estrDestroy(&pTemp);
		}

	}

	if (sbRestorePasswordBackups)
	{
		int iCounter = 0;

		GetHumanVerification("Really restore backup passwords, and revert to password version %d, encryption key %d?",
			siPasswordVersionToResetTo, siEncryptionKeyToResetTo);


		loadstart_printf("Restoring password backups....");
		
		loadstart_printf("Going to set global encryption key and password type");
	
		AccountEncryption_ForceEncryptionKeyIndex(siEncryptionKeyToResetTo);
		AutoTrans_trSetPasswordVersion(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, siPasswordVersionToResetTo);
		loadend_printf("Done....");




		CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
		{
			AccountInfo *account = (AccountInfo *) accountContainer->containerData;

			//for the very first (and only the very first) account, before we have actually done anything,
			//we do a sanity check that it
			//has a backup, and has sensible values for the password version and encryption key
			if (iCounter == 0)
			{
				if (!account->pBackupPasswordInfo)
				{
					GetHumanVerification("Backup password restoring has been requested, but the first account encountered does not have a backup... this is a very bad sign. Really continue?");
				}
				else 
				{
					if (account->pBackupPasswordInfo->ePasswordVersion != siPasswordVersionToResetTo)
					{
						GetHumanVerification("Backup password restoring has been requested, but the first account encountered has a backup with password version %d, not %d. This is a very bad sign. Really continue?",
							account->pBackupPasswordInfo->ePasswordVersion, siPasswordVersionToResetTo);
					}

					if (account->pBackupPasswordInfo->iEncryptionKeyIndex != siEncryptionKeyToResetTo)
					{
						GetHumanVerification("Backup password restoring has been requested, but the first account encountered has a backup with encryption key %d, not %d. This is a very bad sign. Really continue?",
							account->pBackupPasswordInfo->iEncryptionKeyIndex, siEncryptionKeyToResetTo);
					}
				}
			}


			if (++iCounter % 10000 == 0)
			{
				printf("Processed %d accounts...\n", iCounter);
			}

			if (account->pBackupPasswordInfo)
			{
				AutoTrans_trAccountRestorePasswordBackup(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
			}
		}
		CONTAINER_FOREACH_END;

		AutoTrans_trSetPasswordBackupCreationTime(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, timeSecondsSince2000());

		loadend_printf("Done...");

	}

	if (sbDestroyPasswordBackups)
	{
		int iCounter = 0;

		GetHumanVerification("Really destroy all password backups?");


		loadstart_printf("Destroying password backups....");
		

		CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
		{
			AccountInfo *account = (AccountInfo *) accountContainer->containerData;

			if (++iCounter % 10000 == 0)
			{
				printf("Processed %d accounts...\n", iCounter);
			}

			if (account->pBackupPasswordInfo)
			{
				AutoTrans_trAccountDestroyPasswordBackup(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
			}
		}
		CONTAINER_FOREACH_END;

		AutoTrans_trSetPasswordBackupCreationTime(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, 0);


		loadend_printf("Done...");
	}

	if (sbCreatePasswordBackups)
	{
		int iCounter = 0;

		GetHumanVerification("Really create password backups?");

		if (asgGetPasswordBackupCreationTime())
		{
			char *pTemp = NULL;
			timeSecondsDurationToPrettyEString(timeSecondsSince2000() - asgGetPasswordBackupCreationTime(), &pTemp);
			GetHumanVerification("Note that backups were created %s ago and seem to still exist, creating new ones will overwrite those ones. Really continue?",
				pTemp);
			estrDestroy(&pTemp);
		}

		loadstart_printf("Creating password backups....");
		

		CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
		{
			AccountInfo *account = (AccountInfo *) accountContainer->containerData;

			if (++iCounter % 10000 == 0)
			{
				printf("Processed %d accounts...\n", iCounter);
			}

			if (account->passwordInfo)
			{
				AutoTrans_trAccountCreatePasswordBackup(NULL, objServerType(), GLOBALTYPE_ACCOUNT, account->uID);
			}
			else
			{
				if (!account->bPWAutoCreated)
				{
					WARNING_NETOPS_ALERT("CANT_BACKUP_ACCT_PW", "Can't backup the password for account %s(%u), it has no passwordInfo",
						account->accountName, account->uID);
				}
			}
		}
		CONTAINER_FOREACH_END;

		AutoTrans_trSetPasswordBackupCreationTime(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, timeSecondsSince2000());

		loadend_printf("Done...");
	}


	if (!pDirForEncryptionKeys)
	{
	
		if (eaSize(&ppVerificationStrings) != 0)
		{
			StartupFail("AS has been using encryption, has stored encryption strings, but no -DirForEncryptionKeys is specified, this is totally illegal");
		}
	}
	else
	{
		loadstart_printf("Loading encryption keys...");
		LoadIndexedKeys();
		loadend_printf("Done");
	}

	loadstart_printf("Checking password version setting...");
	if (eCurPasswordVersion != asgGetPasswordVersion())
	{
	
		if (eCurPasswordVersion < asgGetPasswordVersion())
		{
			StartupFail("AS was previously using password version %s, is now attempting to use password version %s (due to a code or commandline change). It is illegal to switch to an earlier password version",
				GetPasswordVersionName(asgGetPasswordVersion()), GetPasswordVersionName(eCurPasswordVersion));
		}

		GetHumanVerification("Previously, the AS has been using password version %s. Due to either code or commandline changes, will now switch to %s. This will require re-processing and re-saving all accounts",
			GetPasswordVersionName(asgGetPasswordVersion()), GetPasswordVersionName(eCurPasswordVersion));



		AutoTrans_trSetPasswordVersion(NULL, objServerType(), GLOBALTYPE_ACCOUNTSERVER_GLOBALDATA, 1, eCurPasswordVersion);
	}
	loadend_printf("Done");

	loadstart_printf("Checking for accounts that need password version or encryption key fixup");

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
	{
		AccountInfo *account = (AccountInfo *) accountContainer->containerData;

		if (accountNeedsPasswordOrEncryptionFixup(account))
		{
			ea32Push(&spAccountIDsToFixup, account->uID);
		}
	}
	CONTAINER_FOREACH_END;

	loadend_printf("Done");

	loadstart_printf("Checking for PerfectWorld batches that need password version or encryption key fixup");

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_PWCOMMONACCOUNT, accountContainer);
	{
		PerfectWorldAccountBatch *pBatch = (PerfectWorldAccountBatch *) accountContainer->containerData;
	
		FOR_EACH_IN_EARRAY(pBatch->eaAccounts, PWCommonAccount, pAccount)
		{
			if (AccountIntegration_PWCommonAccountNeedsEncryptionFixup(pAccount))
			{
				ea32Push(&spPerfectWorldBatchIDsToFixup, pBatch->uBatchID);
				break;
			}
		}
		FOR_EACH_END;
		
	}
	CONTAINER_FOREACH_END;

	loadend_printf("Done");



}

static int siAccountFixup_MsecsPerRun = 0;
AUTO_CMD_INT(siAccountFixup_MsecsPerRun, AccountFixup_MsecsPerRun) ACMD_COMMANDLINE;

static int siAccountFixup_ChunkSize = 5;
AUTO_CMD_INT(siAccountFixup_ChunkSize, AccountFixup_ChunkSize) ACMD_COMMANDLINE;

static int siAccountFixup_AccountsPerRun = 10;
AUTO_CMD_INT(siAccountFixup_AccountsPerRun, AccountFixup_AccountsPerRun) ACMD_COMMANDLINE;

static int siAccountFixup_TicksBetweenRuns = 1;
AUTO_CMD_INT(siAccountFixup_TicksBetweenRuns, AccountFixup_TicksBetweenRuns) ACMD_COMMANDLINE;


static U32 siStartingNumFixups = 0;
static U32 siFixupStartTime = 0;
static U32 siNumFixedUp = 0;

static U32 siStartingNumFixups_PW = 0;
static U32 siNumFixedUp_PW = 0;
static U32 siFixupStartTime_PW = 0;

void AccountEncryption_FixupSomeAccounts(void)
{
	static int iTickCounter = 0;
	S64 iStartingTime;
	int iProcessedCount = 0;
	ContainerID iID;
	 PerfectWorldAccountBatch *pBatch;

	static bool sbFirst = true;

	if (sbFirst)
	{
		sbFirst = false;
		siStartingNumFixups = ea32Size(&spAccountIDsToFixup);
		siStartingNumFixups_PW = ea32Size(&spPerfectWorldBatchIDsToFixup);
		siFixupStartTime = timeSecondsSince2000();
	}

	if (!ea32Size(&spAccountIDsToFixup) && !ea32Size(&spPerfectWorldBatchIDsToFixup))
	{
		return;
	}

	if (siAccountFixup_TicksBetweenRuns && iTickCounter > 0)
	{
		iTickCounter--;
			return;
	}
	else
	{
		iTickCounter = siAccountFixup_TicksBetweenRuns;
	}


	iStartingTime = timeGetTime();

	if (ea32Size(&spAccountIDsToFixup))
	{
		while (1)
		{
			int i;

			for (i = 0; i < (siAccountFixup_ChunkSize ? siAccountFixup_ChunkSize : 1); i++)
			{
				AccountInfo *pAccount;
				iID = ea32Pop(&spAccountIDsToFixup);
				pAccount = findAccountByID(iID);
				iProcessedCount++;
				siNumFixedUp++;

				if (pAccount)
				{
					if (accountNeedsPasswordOrEncryptionFixup(pAccount))
					{
						accountDoPasswordOrEncryptionFixup(pAccount);
					}
				}

				if (!ea32Size(&spAccountIDsToFixup))
				{
					return;
				}
			}

			if (siAccountFixup_MsecsPerRun && timeGetTime() - iStartingTime > siAccountFixup_MsecsPerRun)
			{
				return;
			}

			if (siAccountFixup_AccountsPerRun && iProcessedCount >= siAccountFixup_AccountsPerRun)
			{
				return;
			}
		}
	}

	if (!siFixupStartTime_PW)
	{
		siFixupStartTime_PW = timeSecondsSince2000();
	}

	//never do more than one batch per frame, as they're slow
	iID = ea32Pop(&spPerfectWorldBatchIDsToFixup);
	siNumFixedUp_PW++;
	pBatch = PerfectWorldAccountBatch_FindByID(iID);
	
	if (pBatch)
	{
		if (eaSize(&pBatch->eaAccounts))
		{
			//fixing up one account in a batch implicitly fixes up the whole batch
			AccountIntegration_PWCommonAccountDoEncryptionFixup(pBatch->eaAccounts[0]);
			
		}
	}
			
	
	
}

AUTO_STRUCT;
typedef struct FixupProgressForServerMon
{
	char *pStatus; AST(ESTRING)
} FixupProgressForServerMon;


bool GetFixupProgressForServerMon(char *pLocalXPath, UrlArgumentList *pArgList, int iAccessLevel, StructInfoForHttpXpath *pStructInfo, GetHttpFlags eFlags)
{
	FixupProgressForServerMon progress = {0};
	bool bRetVal;

	char *pCrypticStatus = NULL;
	char *pPWStatus = NULL;

	if (siStartingNumFixups == 0)
	{
		estrPrintf(&pCrypticStatus, "No account fixup happening");
	}
	else if (siNumFixedUp == siStartingNumFixups)
	{
		estrPrintf(&pCrypticStatus, "Account fixup complete (%d accounts fixed up)",
			siStartingNumFixups);
	}
	else
	{

		U32 iDuration = timeSecondsSince2000() - siFixupStartTime;
		U32 iNumFixedUp = siNumFixedUp;
		U32 iNumRemaining = siStartingNumFixups - siNumFixedUp;
		U32 iSecsRemaining = 0;
		char *pDurationStr = NULL;
		char *pRemainingStr = NULL;
		float fPerSecond;

		if (iDuration == 0)
		{
			iDuration = 1;
		}

		fPerSecond = (float)iNumFixedUp / (float) iDuration;
		if (fPerSecond)
		{
			iSecsRemaining = iNumRemaining / fPerSecond;
		}

		timeSecondsDurationToPrettyEString(iDuration, &pDurationStr);
		timeSecondsDurationToPrettyEString(iSecsRemaining, &pRemainingStr);

		estrPrintf(&pCrypticStatus, "%d/%d accounts fixed up in %s, %f/sec. Estimated time for completion: %s",
			iNumFixedUp, siStartingNumFixups, pDurationStr, fPerSecond, pRemainingStr);
		
		estrDestroy(&pDurationStr);
		estrDestroy(&pRemainingStr);
	}

	if (siStartingNumFixups_PW == 0)
	{
		estrPrintf(&pPWStatus, "No fixup happening");
	}
	else if (siFixupStartTime_PW == 0)
	{
		estrPrintf(&pPWStatus, "Fixup not yet begun, %d batches will be fixed up", siStartingNumFixups_PW);
	}
	else if (siNumFixedUp_PW == siStartingNumFixups_PW)
	{
		estrPrintf(&pPWStatus, "Fixup complete (%d batches fixed up)",
			siStartingNumFixups_PW);
	}
	else
	{

		U32 iDuration = timeSecondsSince2000() - siFixupStartTime_PW;
		U32 iNumFixedUp = siNumFixedUp_PW;
		U32 iNumRemaining = siStartingNumFixups_PW - siNumFixedUp_PW;
		U32 iSecsRemaining = 0;
		char *pDurationStr = NULL;
		char *pRemainingStr = NULL;
		float fPerSecond;

		if (iDuration == 0)
		{
			iDuration = 1;
		}

		fPerSecond = (float)iNumFixedUp / (float) iDuration;
		if (fPerSecond)
		{
			iSecsRemaining = iNumRemaining / fPerSecond;
		}

		timeSecondsDurationToPrettyEString(iDuration, &pDurationStr);
		timeSecondsDurationToPrettyEString(iSecsRemaining, &pRemainingStr);

		estrPrintf(&pPWStatus, "%d/%d batches fixed up in %s, %f/sec. Estimated time for completion: %s",
			iNumFixedUp, siStartingNumFixups_PW, pDurationStr, fPerSecond, pRemainingStr);
		
		estrDestroy(&pDurationStr);
		estrDestroy(&pRemainingStr);
	}

	


	estrPrintf(&progress.pStatus, "Cryptic account status: %s\n\nPerfectWorld batch status: %s\n", pCrypticStatus, pPWStatus);
	estrDestroy(&pCrypticStatus);
	estrDestroy(&pPWStatus);

	bRetVal = ProcessStructIntoStructInfoForHttp("", pArgList,
		&progress, parse_FixupProgressForServerMon, iAccessLevel, 0, pStructInfo, eFlags);

	StructDeInit(parse_FixupProgressForServerMon, &progress);

	return bRetVal;
}
AUTO_RUN;
void initAccountFixupHTTP(void)
{
	RegisterCustomXPathDomain(".fixup", GetFixupProgressForServerMon, NULL);
}


bool accountNeedsPasswordOrEncryptionFixup(AccountInfo *pAccount)
{
	if (!sbInitted)
	{
		ONCE(AssertOrAlert("AS_STARTUP_ORDER_BAD", "Trying to call accountNeedsPasswordOrEncryptionFixup before calling AccountEncryption_Startup, this is illegal"));
		return true;
	}

	if (pAccount->bPWAutoCreated)
	{
		return false;
	}

	if (!pAccount->passwordInfo)
	{
		return true;
	}

	if (pAccount->passwordInfo->iEncryptionKeyIndex != AccountEncryption_GetCurEncryptionKeyIndex())
	{
		return true;
	}

	if (pAccount->passwordInfo->ePasswordVersion != eCurPasswordVersion)
	{
		return true;
	}

	return false;
}

static bool sbAlwaysFailReencrypting = false;
AUTO_CMD_INT(sbAlwaysFailReencrypting, AlwaysFailReencrypting);


//starts with the password encrypted with key iStartingIndex, unencrypts it, reencrypts it with iStartingIndex+1,
//special case if iStartingIndex == 0
AUTO_TRANS_HELPER;
bool ReEncryptPassword(ATH_ARG NOCONST(AccountInfo) *pAccount, int iStartingIndex, char **ppOutError)
{
	if (sbAlwaysFailReencrypting)
	{
		estrPrintf(ppOutError, "Always fail reencrypting");
		return false;
	}

	if (iStartingIndex == 0)
	{
		char *pTemp;
		char *pKey;

		if (!pAccount->passwordInfo || !pAccount->passwordInfo->password_ForDisk[0])
		{
			estrPrintf(ppOutError, "No password_ForDisk found");
			return false;
		}

		pKey = AccountEncryption_FindKeyFromIndex(1);
		if (!pKey)
		{
			estrPrintf(ppOutError, "Couldn't find key 1");
			return false;
		}

		estrStackCreate(&pTemp);
		EncodeAESThenBase64(pKey, pAccount->passwordInfo->password_ForDisk, (int)strlen(pAccount->passwordInfo->password_ForDisk), &pTemp);
		strcpy(pAccount->passwordInfo->password_ForDisk, pTemp);
		pAccount->passwordInfo->iEncryptionKeyIndex = 1;
		estrDestroy(&pTemp);
	}
	else
	{
		char *pOldKey = AccountEncryption_FindKeyFromIndex(iStartingIndex);
		char *pNewKey = AccountEncryption_FindKeyFromIndex(iStartingIndex + 1);
		char *pDecrypted;
		int iDecryptedSize;
		char *pTemp;

		if (!pAccount->passwordInfo || !pAccount->passwordInfo->password_ForDisk[0])
		{
			estrPrintf(ppOutError, "No password_ForDisk found");
			return false;
		}

		if (!pOldKey)
		{
			estrPrintf(ppOutError, "Couldn't find key %d", iStartingIndex);
			return false;
		}
		if (!pNewKey)
		{
			estrPrintf(ppOutError, "Couldn't find key %d", iStartingIndex + 1);
			return false;
		}

		pDecrypted = DecodeBase64ThenAES(pOldKey, pAccount->passwordInfo->password_ForDisk, &iDecryptedSize);
		if (!pDecrypted)
		{
			estrPrintf(ppOutError, "Couldn't decode encrypted pw with key %d", iStartingIndex);
			return false;
		}

		estrStackCreate(&pTemp);
		EncodeAESThenBase64(pNewKey, pDecrypted, iDecryptedSize, &pTemp);
		strcpy(pAccount->passwordInfo->password_ForDisk, pTemp);
		pAccount->passwordInfo->iEncryptionKeyIndex = iStartingIndex + 1;
		estrDestroy(&pTemp);
		free(pDecrypted);
	}


	return true;
}

AUTO_TRANS_HELPER;
bool EncryptOrCopyPassword_RAMToDisk(ATH_ARG NOCONST(AccountPasswordInfo) *pPasswordInfo)
{
	if (!pPasswordInfo)
	{
		return false;
	}

	if (!pPasswordInfo->password_ForRAM[0])
	{
		return false;
	}

	if (pPasswordInfo->iEncryptionKeyIndex == 0)
	{
		strcpy(pPasswordInfo->password_ForDisk, pPasswordInfo->password_ForRAM);
	}
	else
	{
		char *pKey = AccountEncryption_FindKeyFromIndex(pPasswordInfo->iEncryptionKeyIndex);
		char *pTemp = NULL;

		if (!pKey)
		{
			return false;
		}

		estrStackCreate(&pTemp);
		EncodeAESThenBase64(pKey, pPasswordInfo->password_ForRAM, (int)strlen(pPasswordInfo->password_ForRAM), &pTemp);
		strcpy(pPasswordInfo->password_ForDisk, pTemp);
		estrDestroy(&pTemp);
	}


	return true;





}



	
AUTO_TRANS_HELPER;
bool trhAccountDecryptPassword(ATH_ARG NOCONST(AccountPasswordInfo) *pPasswordInfo)
{
	char *pKey;
	char *pDecoded;
	int iDecodedSize;


	if (!pPasswordInfo || !pPasswordInfo->password_ForDisk[0])
	{
		return false;
	}

	if (pPasswordInfo->iEncryptionKeyIndex == 0)
	{
		strcpy(pPasswordInfo->password_ForRAM, pPasswordInfo->password_ForDisk);
		return true;
	}

	pKey = AccountEncryption_FindKeyFromIndex(pPasswordInfo->iEncryptionKeyIndex);
	if (!pKey)
	{
		return false;
	}

	pDecoded = DecodeBase64ThenAES(pKey, pPasswordInfo->password_ForDisk, &iDecodedSize);

	if (!pDecoded)
	{
		return false;
	}

	if (iDecodedSize >= MAX_PASSWORD)
	{
		return false;
	}

	memcpy(pPasswordInfo->password_ForRAM, pDecoded, iDecodedSize);
	pPasswordInfo->password_ForRAM[iDecodedSize] = 0;

	return true;
}

bool accountDecryptPassword(AccountInfo *pAccount)
{
	return trhAccountDecryptPassword((NOCONST(AccountPasswordInfo)*)(pAccount->passwordInfo));
}

bool accountDecryptBackupPassword(AccountInfo *pAccount)
{
	return trhAccountDecryptPassword((NOCONST(AccountPasswordInfo)*)(pAccount->pBackupPasswordInfo));
}

AUTO_TRANSACTION ATR_LOCKS(pAccount, ".password_obsolete, .Uid, .passwordInfo, .AccountName");
enumTransactionOutcome trAccountDoPasswordOrEncryptionFixup(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	int iCurEncryptionIndex = AccountEncryption_GetCurEncryptionKeyIndex();


	//first step, create PasswordInfo if it doesn't exist already (this will happen either
	//during first fixup with the struct existing, or else during account creation-time fixup
	if (!pAccount->passwordInfo)
	{
		pAccount->passwordInfo = (NOCONST(AccountPasswordInfo)*)StructCreate(parse_AccountPasswordInfo);
		
		switch (eCurPasswordVersion)
		{
		xcase PASSWORDVERSION_HASHED_UNSALTED:
			strcpy(pAccount->passwordInfo->password_ForRAM, pAccount->password_obsolete);

		xcase PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME:
			if (!cryptAddSaltToHash(pAccount->password_obsolete, pAccount->accountName, true, SAFESTR(pAccount->passwordInfo->password_ForRAM)))
			{
				TRANSACTION_RETURN_FAILURE("Unable to do hashing/salting while initializing password in version %s",
					GetPasswordVersionName(eCurPasswordVersion));
			}			
		}

		pAccount->passwordInfo->ePasswordVersion = eCurPasswordVersion;
				
		pAccount->password_obsolete[0] = 0;
		pAccount->passwordInfo->iEncryptionKeyIndex = AccountEncryption_GetCurEncryptionKeyIndex();
	
		EncryptOrCopyPassword_RAMToDisk(pAccount->passwordInfo);
	}

	//we now know that pAccount->passwordInfo exists and is correct, so we start doing our fixups... first we fixup to 
	//the current encryption version
	if (pAccount->passwordInfo->iEncryptionKeyIndex != iCurEncryptionIndex)
	{
		int iSourceIndex, iTargetIndex;
		if (pAccount->passwordInfo->iEncryptionKeyIndex < 0 || pAccount->passwordInfo->iEncryptionKeyIndex > iCurEncryptionIndex)
		{
			 TRANSACTION_RETURN_FAILURE("Account %u has invalid encryption key", pAccount->uID);
		}

		//successively reencrypt password_forDisk, leave password_forRAM alone
		iSourceIndex = pAccount->passwordInfo->iEncryptionKeyIndex;
		iTargetIndex = iSourceIndex + 1;

		while (iTargetIndex <= iCurEncryptionIndex)
		{
			static char *pError = NULL;
			if (!ReEncryptPassword(pAccount, iSourceIndex, &pError))
			{
				 TRANSACTION_RETURN_FAILURE("Account %u, unable to reencrypt password from key %d to key %d. Error: %s", 
					pAccount->uID, iSourceIndex, iTargetIndex, pError);
			}

			iSourceIndex++;
			iTargetIndex++;
		}
	}

	if (pAccount->passwordInfo->ePasswordVersion != eCurPasswordVersion)
	{
		enumPasswordVersion eOldVersion;
		trhAccountDecryptPassword(pAccount->passwordInfo);

		//
		for (eOldVersion = pAccount->passwordInfo->ePasswordVersion; eOldVersion < eCurPasswordVersion; eOldVersion++)
		{
			char temp[MAX_PASSWORD];

			switch (eOldVersion)
			{
			xcase PASSWORDVERSION_HASHED_UNSALTED:
				//passwordInfo->password_ForRAM contains the hashed password, want to replace it with the hashed, then 
				//hashed-with-account-name version
				
				if (!cryptAddSaltToHash(pAccount->passwordInfo->password_ForRAM, pAccount->accountName, true, SAFESTR(temp)))
				{
					if (StringIsAllWhiteSpace(pAccount->passwordInfo->password_ForRAM))
					{
						TRANSACTION_RETURN_FAILURE("Hashing/salting failed because the password is empty");
					}
					else
					{
						TRANSACTION_RETURN_FAILURE("Unable to do hashing/salting while converting from password version %s to password version %s",
							GetPasswordVersionName(eOldVersion), GetPasswordVersionName(eOldVersion + 1));
					}
				}

				strcpy(pAccount->passwordInfo->password_ForRAM, temp);
				EncryptOrCopyPassword_RAMToDisk(pAccount->passwordInfo);



			xdefault:
				TRANSACTION_RETURN_FAILURE("Don't know how to convert from password version %s to password version %s",
					GetPasswordVersionName(eOldVersion), GetPasswordVersionName(eOldVersion + 1));
			}

			pAccount->passwordInfo->ePasswordVersion = eOldVersion + 1;
		}

	}


	return TRANSACTION_OUTCOME_SUCCESS;
}

//at acct creation time, it's not really a container yet, so just call the AUTO_TRANS directly (I know, I know, don't
//try this at home)

bool accountDoCreationTimePasswordFixup(NOCONST(AccountInfo) *pAccount)
{
	char *pSuccessEstr = NULL;
	char *pFailureEstr = NULL;
	enumTransactionOutcome eOutcome = trAccountDoPasswordOrEncryptionFixup(&pSuccessEstr, &pFailureEstr, pAccount);
	if (eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		CRITICAL_NETOPS_ALERT("ACCT_CREATION_TIME_FIXUP_FAIL", "At account creation time for acct %s, password fixup failed because: %s", pAccount->accountName, pFailureEstr);
	}

	estrDestroy(&pSuccessEstr);
	estrDestroy(&pFailureEstr);

	return eOutcome == TRANSACTION_OUTCOME_SUCCESS;
}


bool accountDoPasswordOrEncryptionFixup(AccountInfo *pAccount)
{
	TransactionReturnVal result = {0};

	//may have already gotten fixed up for some reason
	if (!accountNeedsPasswordOrEncryptionFixup(pAccount))
	{
		return true;
	}

	AutoTrans_trAccountDoPasswordOrEncryptionFixup(&result, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID);
	
	//after anythign which might change password_ForRAM, have to set it back to empty in the real container so it
	//will be re-decrypted next time it's needed
	if (pAccount->passwordInfo)
	{
		pAccount->passwordInfo->password_ForRAM[0] = 0;
	}

	if (result.eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		CRITICAL_NETOPS_ALERT("ACCT_PW_FIXUP_FAIL", "Failed to fixup account %u because: %s", pAccount->uID, GetTransactionFailureString(&result));
		ReleaseReturnValData(objLocalManager(), &result);
		return false;
	}

	ReleaseReturnValData(objLocalManager(), &result);
	return true;
}


AUTO_TRANS_HELPER;
bool accountSetAndEncryptPassword(ATH_ARG NOCONST(AccountInfo) *pAccount, const char *pHashedPassword)
{
	if (ISNULL(pAccount) || !pHashedPassword[0])
	{
		return false;
	}

	if (!sbInitted)
	{
		ONCE(AssertOrAlert("AS_STARTUP_ORDER_BAD", "Trying to call accountSetAndEncryptPassword before calling AccountEncryption_Startup, this is illegal"));
		return false;
	}

	pAccount->password_obsolete[0] = 0;
	if (!pAccount->passwordInfo)
	{
		pAccount->passwordInfo = (NOCONST(AccountPasswordInfo)*)StructCreate(parse_AccountPasswordInfo);
	}

	switch (eCurPasswordVersion)
	{
	xcase PASSWORDVERSION_HASHED_UNSALTED:
		strcpy(pAccount->passwordInfo->password_ForRAM, pHashedPassword);
	xcase PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME:
		if (!cryptAddSaltToHash(pHashedPassword, pAccount->accountName, true, SAFESTR(pAccount->passwordInfo->password_ForRAM)))
		{
			return false;
		}

	xdefault:
		CRITICAL_NETOPS_ALERT("CANT_CHANGE_PW", "accountSetAndEncryptPassword being called for unsupported password version");
		return false;
	}


	pAccount->passwordInfo->ePasswordVersion = eCurPasswordVersion;
	pAccount->passwordInfo->iEncryptionKeyIndex = AccountEncryption_GetCurEncryptionKeyIndex();
	EncryptOrCopyPassword_RAMToDisk(pAccount->passwordInfo);

	if (pAccount->pBackupPasswordInfo)
	{
		switch (pAccount->pBackupPasswordInfo->ePasswordVersion)
		{
		xcase PASSWORDVERSION_HASHED_UNSALTED:
			strcpy(pAccount->pBackupPasswordInfo->password_ForRAM, pHashedPassword);
		xcase PASSWORDVERSION_HASHED_SALTED_WITH_ACTNAME:
			if (!cryptAddSaltToHash(pHashedPassword, pAccount->accountName, true, SAFESTR(pAccount->pBackupPasswordInfo->password_ForRAM)))
			{
				return false;
			}

		xdefault:
			CRITICAL_NETOPS_ALERT("CANT_CHANGE_PW", "accountSetAndEncryptPassword being called for unsupported password version in backup PW of account %s",
				pAccount->accountName);
			return false;
		}

		EncryptOrCopyPassword_RAMToDisk(pAccount->pBackupPasswordInfo);
	}


	pAccount->uPasswordChangeTime = timeSecondsSince2000();

	return true;
}

//given a string, encrypts it with the current encryption key, base 64 encodes it, and returns an alloced string (or just
//copies it if there is no encryption)
char *AccountEncryption_EncryptEncodeAndAllocString(const char *pInString)
{
	char *pEncoded = NULL;
	int iCurKeyIndex = AccountEncryption_GetCurEncryptionKeyIndex();
	char *pRetVal;

	if (!sbInitted)
	{
		ONCE(AssertOrAlert("AS_STARTUP_ORDER_BAD", "Trying to call AccountEncryption_EncryptEncodeAndAllocString before calling AccountEncryption_Startup, this is illegal"));
		return NULL;
	}

	if (iCurKeyIndex == 0)
	{
		return strdup(pInString);
	}

	estrStackCreate(&pEncoded);


	if (!EncodeAESThenBase64(AccountEncryption_FindKeyFromIndex(iCurKeyIndex), pInString, (int)strlen(pInString), &pEncoded))
	{
		estrDestroy(&pEncoded);
		return NULL;
	}

	pRetVal = strdup(pEncoded);
	estrDestroy(&pEncoded);
	return pRetVal;
}
//reverse the above process
char *AccountEncryption_DecodeDecryptAndAllocStringWithKeyVersion(const char *pInString, int iKeyIndex)
{
	char *pDecoded;
	int iDecodedSize;
	char *pRetVal;
	char *pKey;

	if (!pInString)
	{
		return NULL;
	}

	if (iKeyIndex == 0)
	{
		return strdup(pInString);
	}

	pKey = AccountEncryption_FindKeyFromIndex(iKeyIndex);
	if (!pKey)
	{
		devassert("AccountEncryption_DecodeDecryptAndAllocStringWithKeyVersion being called with no key");
		return NULL;
	}

	pDecoded = DecodeBase64ThenAES(pKey, pInString, &iDecodedSize);
	if (!pDecoded)
	{
		return NULL;
	}

	pRetVal = malloc(iDecodedSize + 1);
	memcpy(pRetVal, pDecoded, iDecodedSize);
	pRetVal[iDecodedSize] = 0;

	free(pDecoded);

	return pRetVal;
}

//with a specified key
char *AccountEncryption_DecodeDecryptAndAllocString(const char *pInString)
{
	return AccountEncryption_DecodeDecryptAndAllocStringWithKeyVersion(pInString, AccountEncryption_GetCurEncryptionKeyIndex());
}

#undef _getch

#define GEN_KEY_EXIT { if (pContext->eHowCalled == CMD_CONTEXT_HOWCALLED_EARLY_COMMANDLINE) { char c; printf("Press a key to continue\n"); c = _getch(); exit(0); } return; }

AUTO_COMMAND ACMD_EARLYCOMMANDLINE ACMD_ACCESSLEVEL(0);
void GenerateEncryptionKey(char *pFileName, CmdContext *pContext)
{
	FILE *pOutFile;
	char key[KEY_SIZE];
	char *pBase64Encoded = NULL;
	
	printf("\n\n---------------------\nGoing to try to generate a encryption key and put it into %s\n---------------------\n\n", pFileName);

	if (fileExists(pFileName))
	{
		printf("That file already exists... this is too dangerous\n");
		GEN_KEY_EXIT;
	}

	pOutFile = fopen(pFileName, "wt");

	if (!pOutFile)
	{
		printf("Unable to open %s for writing\n", pFileName);
		GEN_KEY_EXIT;
	}

	cryptGetRandomBitsWithMouseEntropyAndAllSortsOfCrazyStuff(key, KEY_SIZE);
	estrBase64Encode(&pBase64Encoded, key, KEY_SIZE);
	fprintf(pOutFile, "%s", pBase64Encoded);
	fclose(pOutFile);
	estrDestroy(&pBase64Encoded);

	printf("Succeeded\n");

	GEN_KEY_EXIT

}

AUTO_TRANSACTION ATR_LOCKS(pAccount, ".pBackupPasswordInfo");
enumTransactionOutcome trAccountDestroyPasswordBackup(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{

	StructDestroyNoConstSafe(parse_AccountPasswordInfo, &pAccount->pBackupPasswordInfo);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION ATR_LOCKS(pAccount, ".pBackupPasswordInfo, .passwordInfo");
enumTransactionOutcome trAccountCreatePasswordBackup(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!pAccount->passwordInfo)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if (pAccount->pBackupPasswordInfo)
	{
		StructResetVoid(parse_AccountPasswordInfo, pAccount->pBackupPasswordInfo);
	}
	else
	{
		pAccount->pBackupPasswordInfo = StructCreateVoid(parse_AccountPasswordInfo);
	}

	StructCopyVoid(parse_AccountPasswordInfo, pAccount->passwordInfo, pAccount->pBackupPasswordInfo, 0, 0, 0);


	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_TRANSACTION ATR_LOCKS(pAccount, ".pBackupPasswordInfo, .passwordInfo");
enumTransactionOutcome trAccountRestorePasswordBackup(ATR_ARGS, NOCONST(AccountInfo) *pAccount)
{
	if (!pAccount->passwordInfo || !pAccount->pBackupPasswordInfo)
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}


	StructCopyVoid(parse_AccountPasswordInfo, pAccount->pBackupPasswordInfo, pAccount->passwordInfo, 0, 0, 0);

	return TRANSACTION_OUTCOME_SUCCESS;
}

/*
#include "rand.h"

void RandomizeMemory(char *pBuf, int iSize)
{
	int i;

	for (i = 0; i < iSize; i++)
	{
		pBuf[i] = randomIntRange(0, 255);
	}
}

AUTO_RUN;
void AES64Test(void)
{
	char key[32] = "ja;sljfkas;dlfjad;lfajsdf;lskfj";

	int iLen;

	for (iLen = 1; iLen < 5000; iLen++)
	{
		char *pData = malloc(iLen);
		char *pEncoded = NULL;
		char *pDecoded;
		int iDecodedLen;
		
		RandomizeMemory(pData, iLen);

		assert(EncodeAESThenBase64(key, pData, iLen, &pEncoded));
		pDecoded = DecodeBase64ThenAES(key, pEncoded, &iDecodedLen);
		assert(pDecoded && iDecodedLen == iLen && memcmp(pDecoded, pData, iDecodedLen) == 0);

		free(pData);
		free(pDecoded);
		estrDestroy(&pEncoded);
	}
}
	*/	






#include "AccountEncryption_c_ast.c"
