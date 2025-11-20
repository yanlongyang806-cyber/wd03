#include "accountCommon.h"
#include "AccountLog.h"
#include "AccountManagement.h"
#include "accountnet.h"
#include "AccountReporting.h"
#include "AccountServer.h"
#include "AutoGen/AccountServer_autotransactions_autogen_wrappers.h"
#include "AutoTransDefs.h"
#include "BlockEarray.h"
#include "crypt.h"
#include "earray.h"
#include "estring.h"
#include "fileutil.h"
#include "foldercache.h"
#include "GlobalData.h"
#include "GlobalComm.h"
#include "GlobalTypes.h"
#include "logging.h"
#include "mathutil.h"
#include "net/net.h"
#include "objContainer.h"
#include "objContainerIO.h"
#include "objTransactions.h"
#include "Product.h"
#include "ProductKey.h"
#include "ProductKey_c_ast.h"
#include "qSortG.h"
#include "rand.h"
#include "StashTable.h"
#include "StringUtil.h"
#include "textparser.h"
#include "utilitiesLib.h"
#include "utils.h"
#include "wininclude.h"

extern ParseTable parse_GlobalTypeMapping[];
#define TYPE_parse_GlobalTypeMapping GlobalTypeMapping
extern ParseTable parse_AccountPermissionStruct[];
#define TYPE_parse_AccountPermissionStruct AccountPermissionStruct

#define CHARACTER_TABLE_SIZE 28
//#define GENERATE_MOD 60466176 // 36^5
#define GENERATE_MOD 17210368 // 28^5

// Non-persisted look-aside information for a key
AUTO_STRUCT;
typedef struct KeyLookasideInfo
{
	U32 uBatchId;						// Batch ID of a key, must be nonzero
	U32 uKeyId;							// Index of ID in key list
	U32 uAccountId;						// ID of account associated with this key, if any; otherwise zero
										// Note: uAccountId is (U32)-1 if the key has been marked as invalid
	U32 uDistributedAccountId;			// ID of account this key was distributed to, if any; otherwise zero
} KeyLookasideInfo;

/************************************************************************/
/* Legacy product keys                                                  */
/************************************************************************/

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct LegacyProductKey
{
	const int containerID; AST(KEY)
	const int batchID;
	CONST_STRING_MODIFIABLE pKey;
	const U32 uAccountID;
	const U32 uDistributedAccountID;
	const U32 uLockTime; // Seconds since 2000 that this key was locked for distribution (if 0, it is not locked)

	const bool bFromUsed; NO_AST // This is set by findProductKey(..) to indicate which ContainerStore it was from. 
								 // Undefined value when ProductKey retrieved by other methods.
} LegacyProductKey;
AST_PREFIX();

AUTO_STRUCT AST_IGNORE(numProductKeys) AST_IGNORE(pBatchShards) AST_IGNORE(pBatchSpecials) AST_IGNORE(timeValidStart) AST_IGNORE(timeValidEnd);
typedef struct LegacyProductKeyBatch
{
	U32 uID;
	char productPrefix[PRODUCT_KEY_PREFIX_SIZE+1];
	U32 timeCreated;
	U32 timeDownloaded;

	// Batch Keys
	char ** ppBatchKeys; // Only used to transfer keys during generation

	char **ppProducts; AST(ESTRING) // List of products these keys activate

	// Batch identifying info
	char * pBatchName; AST(ESTRING)
	char * pBatchDescription; AST(ESTRING)

	U8 batchInvalidated : 1; 
	U8 batchDistributed : 1; // has the batch been distributed yet?
} LegacyProductKeyBatch;

typedef struct AccountPermissionStruct AccountPermissionStruct;

AUTO_STRUCT AST_IGNORE_STRUCT(ppKeys) AST_IGNORE(emptyKeys) AST_IGNORE_STRUCT(pDefaultPermissions) AST_IGNORE(InternalProductName);
typedef struct LegacyProductKeyGroup
{
	char productPrefix[PRODUCT_KEY_PREFIX_SIZE+1];

	LegacyProductKeyBatch **keyBatches;
} LegacyProductKeyGroup;

AUTO_STRUCT;
typedef struct LegacyProductKeysStruct
{
	LegacyProductKeyGroup **ppKeyGroups;
	U32 uLastBatchID;
} LegacyProductKeysStruct;

/************************************************************************/
/* Static variables                                                     */
/************************************************************************/

// do NOT use 0-O, 1-I, 2-Z, 5-S -- [B-8]?
static char sTableBaseCharacters[CHARACTER_TABLE_SIZE] = {
	//'0','1','2',
	'3','4',
	//'5',
	'6','7','8','9',
	'A','B','C','D','E','F','G','H',
	//'I',
	'J','K','L','M','N',
	//'O',
	'P','Q','R',
	//'S',
	'T','U','V','W','X','Y',
	//'Z'
};

// Legacy product key groups and key batches file.

static LegacyProductKeysStruct sProductKeys;
static char * sKeyFile = "productKeys.db";

// Key data for use in the secondary key-generating Account Server.
static ProductKeyGenerationData keyGenData = {0};

// Product key lookaside table, block EArray
KeyLookasideInfo *pKeyLookaside = NULL;

// Index into product key lookaside table
static StashTable stKeyLookaside = NULL;				// key = key string (fixed, unowned), value = int (index into pKeyLookaside)
void ProductKey_DestroyContainers(void)
{
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_PRODUCT); 
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH);
	objRemoveAllContainersWithType(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP);
	if (stKeyLookaside)
		stashTableClear(stKeyLookaside);
}

// Maximum batch ID so far.
static U32 uMaxBatchId = 0;

// Allow the Account Server to load even if it has migration errors.
static bool gbAllowMigrationErrors = true;
AUTO_CMD_INT(gbAllowMigrationErrors, AllowMigrationErrors) ACMD_CMDLINE;

// Don't delete legacy containers during migration.
static bool gbDisableKeyMigrationContainerRemoval = false;
AUTO_CMD_INT(gbDisableKeyMigrationContainerRemoval, DisableKeyMigrationContainerRemoval) ACMD_CMDLINE;

// Delete legacy key containers in the background.
static bool gbDeferLegacyKeysDelete = false;
AUTO_CMD_INT(gbDeferLegacyKeysDelete, DeferLegacyKeysDelete) ACMD_CMDLINE;

// Rate of deletion of legacy keys.
static unsigned gbDeleteLegacyKeysPerFrame = 1;
AUTO_CMD_INT(gbDeleteLegacyKeysPerFrame, DeleteLegacyKeysPerFrame) ACMD_CMDLINE;

// Number of keys to send per update when creating keys for a batch.
static U32 suNumberKeysPerUpdate = 1000000;
AUTO_CMD_INT(suNumberKeysPerUpdate, KeyBatchSize) ACMD_CMDLINE;

// If true, reset distributed key hints.
static bool bResetDistributedKeyHint = false;
AUTO_CMD_INT(bResetDistributedKeyHint, ResetDistributedKeyHint) ACMD_CMDLINE;

static bool sbReceivedResponse;
static bool sbReceivedVersion;
static U32 suBatchID;
static int siKeysCreated;
static NetComm *sAccountServerComm;

char * getPaddedPrefix_s(char* dest, size_t size, const char * pProductPrefix);
#define getPaddedPrefix(dst,src) getPaddedPrefix_s(dst, ARRAY_SIZE_CHECKED(dst), src)

static ProductKeyGroup *findProductKeyGroup(const char * pPaddedPrefix);

// Get the new container identifier from a completed transaction.
static ContainerID GetNewContainerId(const TransactionReturnVal *pReturnVal)
{
	ContainerID result = 0;

	devassert(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);
	if (pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS && pReturnVal->iNumBaseTransactions == 1
		&& pReturnVal->pBaseReturnVals->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char *end;
		result = strtoul(pReturnVal->pBaseReturnVals->returnString, &end, 10);
		if (*end)
			result = 0;
	}
	return result;
}

// Get the lookaside entry for a key by its name.
static KeyLookasideInfo *GetLookasideByName(SA_PARAM_NN_STR const char *pKeyName)
{
	int index;
	bool success;
	KeyLookasideInfo *info = NULL;
	devassert(pKeyName);
	if (!pKeyName || strlen(pKeyName) != PRODUCT_KEY_SIZE)
		return NULL;
	success = stashFindInt(stKeyLookaside, pKeyName, &index);
	if (!success)
		return NULL;
	return pKeyLookaside + index;
}

// Get the lookaside entry for a key by its name.
static KeyLookasideInfo *GetLookasideByHandle(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	KeyLookasideInfo *info;
	char *name = NULL;

	devassert(pKey);
	estrStackCreate(&name);
	copyProductKeyName(&name, pKey);
	info = GetLookasideByName(name);
	estrDestroy(&name);
	
	return info;
}

// Compare two locked keys.
int keyLockCmp(const LockedKey **pptr1, const LockedKey **pptr2)
{
	return strCmp(&(*pptr1)->keyName, &(*pptr2)->keyName);
}

// Search for a locked key.
static const LockedKey *findKeyLock(SA_PARAM_NN_STR const char *pKey)
{
	CONST_EARRAY_OF(LockedKey) lockedKeys = asgGetLockedKeys();
	NOCONST(LockedKey) searchKey, *pSearchKey = &searchKey;
	int index;

	if (!devassert(pKey))
		return NULL;
	searchKey.keyName = (char *)pKey;
	index = (int)eaBFind(lockedKeys, keyLockCmp, pSearchKey);
	if (index < eaSize(&lockedKeys) && !stricmp(lockedKeys[index]->keyName, pKey))
	{
		devassert(lockedKeys);
		return lockedKeys[index];
	}
	return NULL;
}

// Search for a locked key, by handle..
static const LockedKey *findKeyLockByProductKey(const ProductKey *pKey)
{
	const LockedKey *result;
	char *keyName = NULL;
	estrStackCreate(&keyName);
	copyProductKeyName(&keyName, pKey);
	result = findKeyLock(keyName);
	estrDestroy(&keyName);
	return result;
}

// Return a pointer to a possibly invalid product key.
// WARNING: If only valid product keys are desired, use productKeyIsValid() instead.
bool findProductKey(ProductKey *pKey, const char * pKeyName)
{
	KeyLookasideInfo *info;

	// Allow null and empty strings to return false.
	if (!pKey || !pKeyName)
		return false;

	// Look up key.
	info = GetLookasideByName(pKeyName);
	if (!info)
		return false;

	// Copy key data.
	pKey->uBatchId = info->uBatchId;
	pKey->uKeyId = info->uKeyId;
	return true;
}

// Copy product key name to an EString.
void copyProductKeyName(SA_PRE_NN_NN_STR char **estrKeyName, SA_PARAM_NN_VALID const ProductKey *pKey)
{
	ProductKeyBatch *batch;

	if (!devassert(estrKeyName && pKey))
		return;

	// Clear output string.
	estrClear(estrKeyName);

	// Look up batch.
	batch = getKeyBatchByID(pKey->uBatchId);
	if (!devassert(batch))
		return;
	if (!devassert(pKey->uKeyId * PRODUCT_KEY_SIZE < estrLength(&batch->pKeys)))
		return;

	// Copy name.
	estrConcat(estrKeyName, batch->pKeys + pKey->uKeyId * PRODUCT_KEY_SIZE, PRODUCT_KEY_SIZE);
}

// Find an undistributed product key
bool findUndistributedProductKey(ProductKey *pKey, const char *pPrefix)
{
	char prefix[PRODUCT_KEY_PREFIX_SIZE + 1] = {0};
	ProductKeyGroup *pGroup = NULL;
	ProductKey key = {0};
	bool success = false;
	KeyLookasideInfo *info = NULL;
	const LockedKey *lockedKey = NULL;
	ProductKeyBatch *pBatch = NULL;
	U32 id = 0;

	PERFINFO_AUTO_START_FUNC();

	// Try to find the key group that owns a prefix.
	getPaddedPrefix(prefix, pPrefix);
	pGroup = findProductKeyGroup(pPrefix);

	// If the prefix is not valid, bail.
	if (!pGroup)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Find a distributable key from a batch in the key group.
	EARRAY_INT_CONST_FOREACH_BEGIN(pGroup->keyBatches, i, size);
	{
		U32 batchId = pGroup->keyBatches[i];
		U32 uNextPossiblyAvailable;
		bool batchHasLockedKey = false;

		// Look up batch.
		batchHasLockedKey = false;
		pBatch = getKeyBatchByID(batchId);
		devassert(pBatch);
		if (!pBatch)
			continue;

		// Skip the batch if it has been distributed.
		if (pBatch->batchDistributed)
			continue;

		// Look for undistributed key.
		uNextPossiblyAvailable = pBatch->uNextUndistributedKey;
		for(id = uNextPossiblyAvailable; id * PRODUCT_KEY_SIZE < estrLength(&pBatch->pKeys); ++id)
		{
			int index;
			bool found;

			// Look up the next key.
			found = stashFindInt(stKeyLookaside, pBatch->pKeys + id * PRODUCT_KEY_SIZE, &index);
			if (!devassert(found))
				continue;
			info = pKeyLookaside + index;

			// Check if this key is eligible for distribution.
			key.uBatchId = pBatch->uID;
			key.uKeyId = id;
			if (findKeyLockByProductKey(&key))
			{
				if (!batchHasLockedKey)
				{
					uNextPossiblyAvailable = id;
					batchHasLockedKey = true;
				}
			}
			else
			{
				if (!info->uAccountId && !info->uDistributedAccountId)
				{
					if (uNextPossiblyAvailable != pBatch->uNextUndistributedKey)
						AutoTrans_trSetNextUndistributedKey(astrRequireSuccess(NULL), objServerType(),
							GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, pBatch->uID, uNextPossiblyAvailable);
					success = true;
					goto done;
				}
			}

			// Note that none of the keys seen so far are usable.
			if (!batchHasLockedKey)
				uNextPossiblyAvailable = id + 1;

			// If this is the last key in the batch, record the next key that might be undistributed.
			if ((id + 1) * PRODUCT_KEY_SIZE == estrLength(&pBatch->pKeys))
			{
				if (batchHasLockedKey && uNextPossiblyAvailable != pBatch->uNextUndistributedKey)
					AutoTrans_trSetNextUndistributedKey(astrRequireSuccess(NULL), objServerType(),
						GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, pBatch->uID, uNextPossiblyAvailable);
			}
		}
	}
	EARRAY_FOREACH_END;
done:

	// If no eligible keys were found, return.
	if (!success)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Verify that this is a valid key.
	if (memcmp(pPrefix, pBatch->pKeys + id * PRODUCT_KEY_SIZE, MIN(PRODUCT_KEY_PREFIX_SIZE, strlen(pPrefix))))
	{
		devassert(0);
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Verify that this key is not locked or used.
	key.uBatchId = pBatch->uID;
	key.uKeyId = id;
	lockedKey = findKeyLockByProductKey(&key);
	if (info->uDistributedAccountId || info->uAccountId || lockedKey)
	{
		devassert(0);
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Copy key handle.
	*pKey = key;
	PERFINFO_AUTO_STOP();
	return true;
}

// Set the next new key.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Unextundistributedkey");
enumTransactionOutcome trSetNextUndistributedKey(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, U32 uNextUndistributedKey)
{
	pBatch->uNextUndistributedKey = uNextUndistributedKey;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Make a product key info structure from a product key
SA_RET_OP_VALID ProductKeyInfo * makeProductKeyInfo(SA_PARAM_NN_VALID const ProductKey *pProductKey)
{
	ProductKeyBatch *pBatch = getKeyBatchByID(pProductKey->uBatchId);
	ProductKeyInfo *pInfo = pBatch ? StructCreate(parse_ProductKeyInfo) : NULL;
	KeyLookasideInfo *lookaside = NULL;
	char *name = NULL;
	ProductKeyGroup *group;

	if (!pInfo) return NULL;

	// Look up key.
	lookaside = GetLookasideByHandle(pProductKey);
	if (!lookaside)
		return NULL;

	// Copy key information.
	estrStackCreate(&name);
	copyProductKeyName(&name, pProductKey);
	pInfo->bUsed = !!lookaside->uAccountId;
	pInfo->pKey = strdup(name);
	estrDestroy(&name);

	// Copy products.
	group = keyGroupFromBatch(pBatch);
	if (!devassert(group))
		return NULL;
	EARRAY_CONST_FOREACH_BEGIN(group->ppProducts, j, size);
		const ProductContainer *pProduct = findProductByName(group->ppProducts[j]);

		if (pProduct)
			ea32Push(&pInfo->eaProductIDs, pProduct->uID);
	EARRAY_FOREACH_END;

	return pInfo;
}

// Use -CheckPrefixConsistency to check key group consistency.
static bool gbCheckPrefixConsistency = false;
AUTO_CMD_INT(gbCheckPrefixConsistency, CheckPrefixConsistency) ACMD_CMDLINE;


// Load legacy product key group and batch information.
void loadLegacyProductKeys(void)
{
	static bool bCallbackLoaded = false;
	FILE *file = NULL;
	
	ParserReadTextFile(STACK_SPRINTF("%s%s", dbAccountDataDir(), sKeyFile), parse_LegacyProductKeysStruct, &sProductKeys, 0);

	if (!gbCheckPrefixConsistency) return;

	loadstart_printf("Checking prefix consistency... ");
	file = fopen(STACK_SPRINTF("%s%s", dbAccountDataDir(), "productKeyCheck.csv"), "wt");
	fprintf(file, "\"Prefix\",\"Batch Name\",\"Batch ID\",\"First Problem\"\n");
	EARRAY_CONST_FOREACH_BEGIN(sProductKeys.ppKeyGroups, iCurKeyGroup, iNumKeyGroups);
	{
		const LegacyProductKeyGroup *pGroup = sProductKeys.ppKeyGroups[iCurKeyGroup];
		STRING_EARRAY eaProducts = NULL;
		bool bGotFirst = false;

		if (!devassert(pGroup)) continue;

		EARRAY_CONST_FOREACH_BEGIN(pGroup->keyBatches, iCurBatch, iNumBatches);
		{
			const LegacyProductKeyBatch *pBatch = pGroup->keyBatches[iCurBatch];

			if (!devassert(pBatch)) continue;

			if (!bGotFirst)
			{
				EARRAY_CONST_FOREACH_BEGIN(pBatch->ppProducts, iCurProduct, iNumProducts);
				{
					eaPush(&eaProducts, pBatch->ppProducts[iCurProduct]);
				}
				EARRAY_FOREACH_END;

				bGotFirst = true;
			}
			else
			{
				bool bInvalid = false;
				const char *pProblem = NULL;
				const char *pProductName = NULL;

				EARRAY_CONST_FOREACH_BEGIN(pBatch->ppProducts, iCurProduct, iNumProducts);
				{
					bool bFound = false;

					EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurExistingProduct, iNumExistingProducts);
					{
						if (!stricmp_safe(eaProducts[iCurExistingProduct], pBatch->ppProducts[iCurProduct]))
						{
							bFound = true;
							break;
						}
					}
					EARRAY_FOREACH_END;

					if (!bFound)
					{
						bInvalid = true;
						pProblem = "extra product";
						pProductName = pBatch->ppProducts[iCurProduct];
						break;
					}
				}
				EARRAY_FOREACH_END;

				EARRAY_CONST_FOREACH_BEGIN(eaProducts, iCurExistingProduct, iNumExistingProducts);
				{
					bool bFound = false;

					EARRAY_CONST_FOREACH_BEGIN(pBatch->ppProducts, iCurProduct, iNumProducts);
					{
						if (!stricmp_safe(eaProducts[iCurExistingProduct], pBatch->ppProducts[iCurProduct]))
						{
							bFound = true;
							break;
						}
					}
					EARRAY_FOREACH_END;

					if (!bFound)
					{
						bInvalid = true;
						pProblem = "missing product";
						pProductName = eaProducts[iCurExistingProduct];
						break;
					}
				}
				EARRAY_FOREACH_END;

				if (bInvalid)
				{
					fprintf(file, "\"%s\",\"%s\",\"%d\",\"%s: %s\"\n",
						pGroup->productPrefix, pBatch->pBatchName, pBatch->uID, pProblem, pProductName);
				}
			}
		}
		EARRAY_FOREACH_END;

		eaDestroy(&eaProducts);
		eaProducts = NULL;
	}
	EARRAY_FOREACH_END;
	fclose(file);
	loadend_printf("done.");
}


static void convertToTableBase(U32 uNumber, char *buffer)
{
	int i;
	U32 uTemp = uNumber;

	devassert(uNumber < GENERATE_MOD);

	for (i=0; i<5; i++)
	{
		U32 uDigit = uTemp % CHARACTER_TABLE_SIZE;

		buffer[i] = sTableBaseCharacters[uDigit];

		uTemp = uTemp / CHARACTER_TABLE_SIZE;
	}
}

// Just copies the first 5 characters of the key
char * parseProductPrefix_s(char * dst, size_t size, const char * pKey)
{
	int i;
	if (!(size > 5 && strlen(pKey) >= 5))
	{
		dst[0] = 0;
		return NULL;
	}

	for (i=0; i<5; i++)
	{
		dst[i] = toupper(pKey[i]);
	}
	dst[5] = 0;
	return dst;
}

char * getPaddedPrefix_s(char* dest, size_t size, const char * pProductPrefix)
{
	int i;

	assert (size > 5);
	memset(dest, 0, size);
	// invalid key - too many characters
	if (strlen(pProductPrefix) > 5)
		return dest;
	strcpy_s(dest, size, pProductPrefix);
	string_toupper(dest);

	for (i=(int) strlen(dest); i < 5; i++)
	{
		dest[i] = '0';
	}
	dest[5] = '\0';
	return dest;
}

// Parse comma-separated product list and validate it.
static void GetProductList(char ***pppProducts, const char *pList)
{
	const char *productList;
	char **ppProductsSorted = NULL;
	int i;

	// Add products.
	for (productList = pList; productList && *productList; productList = strchr(productList, ','), productList += !!productList)
	{
		char *product = 0;
		char *comma = strchr(productList, ',');
		char *end = comma ? comma : productList + strlen(productList);
		estrConcat(&product, productList, end - productList);
		if (!findProductByName(product))
		{
			eaClearEString(pppProducts);
			break;
		}
		eaPush(pppProducts, product);
	}

	// Check for duplicates.
	eaCopyEStrings(pppProducts, &ppProductsSorted);
	eaQSort(ppProductsSorted, strCmp);
	for (i = 0; i < eaSize(&ppProductsSorted) - 1; ++i)
	{
		if (!stricmp(ppProductsSorted[i], ppProductsSorted[i + 1]))
		{
			eaClearEString(pppProducts);
			eaDestroyEString(&ppProductsSorted);
			return;
		}
	}
	eaDestroyEString(&ppProductsSorted);
}

void productKeyGroupAddStruct(ProductKeyGroup *pKeyGroup)
{
	objRequestContainerCreateLocal(astrRequireSuccess("addKeyGroupStruct"), GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, pKeyGroup);
}

// Create new key group.
bool createNewKeyGroup(SA_PARAM_NN_VALID const char * pProductPrefix, SA_PARAM_NN_VALID const char * pInternalProductNames)
{
	NOCONST(ProductKeyGroup) *pKeyGroup;

	if (!devassert(pProductPrefix && pInternalProductNames))
		return false;
	if (strlen(pProductPrefix) > 5)
	{
		devassert(0);
		return false;
	}

	// Create key group structure.
	pKeyGroup = StructCreateNoConst(parse_ProductKeyGroup);
	getPaddedPrefix(pKeyGroup->productPrefix, pProductPrefix);
	if (findProductKeyGroup(pKeyGroup->productPrefix))
	{
		StructDestroyNoConst(parse_ProductKeyGroup, pKeyGroup);
		return false;
	}

	// Add products.
	GetProductList(&pKeyGroup->ppProducts, pInternalProductNames);
	if (!eaSize(&pKeyGroup->ppProducts))
	{
		StructDestroyNoConst(parse_ProductKeyGroup, pKeyGroup);
		return false;
	}

	// Create key group container.
	objRequestContainerCreateLocal(astrRequireSuccess("createNewKeyGroup"), GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, pKeyGroup);
	StructDestroyNoConst(parse_ProductKeyGroup, pKeyGroup);

	return true;
}

// Set the list of product names for a key group.
bool setKeyGroupProductNames(U32 uKeyGroup, SA_PARAM_NN_VALID const char *pInternalProductNames)
{
	TransactionReturnVal result;
	bool success;
	AutoTrans_trSetKeyGroupProductNames(&result, objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, uKeyGroup, pInternalProductNames);
	success = result.eOutcome == TRANSACTION_OUTCOME_SUCCESS;
	objReleaseTransactionReturn(&result);
	return success;
}

// Set the list of product names on a key group container.
AUTO_TRANSACTION
ATR_LOCKS(pGroup, ".Ppproducts");
enumTransactionOutcome trSetKeyGroupProductNames(ATR_ARGS, NOCONST(ProductKeyGroup) *pGroup, const char *pInternalProductNames)
{
	char **ppProducts = NULL;

	// Get product list.
	GetProductList(&ppProducts, pInternalProductNames);
	if (!eaSize(&ppProducts))
	{
		eaDestroy(&ppProducts);
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Copy new product list over the old one.
	eaDestroyEString(&pGroup->ppProducts);
	pGroup->ppProducts = ppProducts;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Find a product key group.
ProductKeyGroup * findProductKeyGroup(const char * pPaddedPrefix)
{
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, groupContainer);
	{
		ProductKeyGroup *group = (ProductKeyGroup *)groupContainer->containerData;
		if (!stricmp_safe(group->productPrefix, pPaddedPrefix))
			return group;
	}
	CONTAINER_FOREACH_END;

	return NULL;
}

// Return NULL if a product key is not valid.
bool productKeyIsValidByPtr(SA_PARAM_OP_VALID ProductKey *pKey)
{
	ProductKeyBatch *keyBatch;
	KeyLookasideInfo *info;

	// Look up key.
	if (!pKey) return false;
	info = GetLookasideByHandle(pKey);
	if (!info)
		return false;

	// Make sure the key is not marked as invalid.
	if (info->uAccountId == (U32)-1)
		return false;

	// Ensure that this is a valid batch.
	keyBatch = getKeyBatchByID(pKey->uBatchId);
	if (!keyBatch || keyBatch->batchInvalidated)
		return false;

	return true;
}

// Determine if a product key is locked
bool productKeyIsLocked(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	return !!findKeyLockByProductKey(pKey);
}

// Get the lock time for a product key.
U32 productKeyGetLockTime(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	const LockedKey *lockedKey = findKeyLockByProductKey(pKey);
	if (!lockedKey)
		return 0;
	return lockedKey->uLockTime;
}

// Determine if a product key is distributed
bool productKeyIsDistributed(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	char *name = NULL;
	KeyLookasideInfo *info;

	// Look up key.
	info = GetLookasideByHandle(pKey);
	if (!devassert(info))
		return false;

	// Return true if distributed.
	if (info->uDistributedAccountId) return true;
	return false;
}

// Get the account ID that a key was activated on
U32 getActivatedAccountId(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	char *name = NULL;
	KeyLookasideInfo *info;

	// Look up key.
	info = GetLookasideByHandle(pKey);
	if (!devassert(info))
		return 0;

	return info->uAccountId == -1 ? 0 : info->uAccountId;
}

// Look up a product key and check if it is valid.
bool productKeyIsValid(ProductKey *pKey, const char *pKeyName)
{
	KeyLookasideInfo *info;
	ProductKey key;
	bool valid;

	if (!devassert(pKey && pKeyName))
		return false;

	// Look up key.
	info = GetLookasideByName(pKeyName);
	if (!info)
		return false;
	key.uBatchId = info->uBatchId;
	key.uKeyId = info->uKeyId;
	valid = productKeyIsValidByPtr(&key);
	if (valid)
	{
		pKey->uBatchId = key.uBatchId;
		pKey->uKeyId = key.uKeyId;
	}
	return valid;
}

// Get product key validity.  Warning: Do not use this to check if a product key is valid.  Only use it to check if a product key
// has specifically been marked as invalid.
bool productKeyGetInvalid(SA_PARAM_NN_STR const char *pKeyName)
{
	KeyLookasideInfo *info = GetLookasideByName(pKeyName);
	if (!devassert(info))
		return false;

	return info->uAccountId == (U32)-1;
}

// Mark product key batch validity.
bool productKeySetInvalid(SA_PARAM_NN_STR const char *pKeyName, bool bInvalid)
{
	KeyLookasideInfo *info = GetLookasideByName(pKeyName);

	if (!info)
		return false;

	// Do not allow invalidating keys that have been activated.
	if (info->uAccountId && info->uAccountId != (U32)-1)
		return false;

	// Do nothing if the key is already in the requested state.
	if (bInvalid && info->uAccountId == -1 || !bInvalid && info->uAccountId == 0)
		return true;

	// Mark the key as valid or invalid.
	if (bInvalid)
	{
		AutoTrans_trAddInvalidKey(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
			info->uBatchId, info->uKeyId);
		info->uAccountId = (U32)-1;
	}
	else
	{
		AutoTrans_trRemoveInvalidKey(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
			info->uBatchId, info->uKeyId);
		info->uAccountId = 0;
	}

	return true;
}

// Add a product key to the invalid list.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Pinvalidkeys");
enumTransactionOutcome trAddInvalidKey(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, U32 uKeyIndex)
{
	int index =	(int)eaiBFind(pBatch->pInvalidKeys, uKeyIndex);
	if (pBatch->pInvalidKeys && pBatch->pInvalidKeys[index] == uKeyIndex)
		return TRANSACTION_OUTCOME_FAILURE;
	eaiInsert(&pBatch->pInvalidKeys, uKeyIndex, index);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Remove a product key from the invalid list.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Pinvalidkeys");
enumTransactionOutcome trRemoveInvalidKey(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, U32 uKeyIndex)
{
	int index =	(int)eaiBFind(pBatch->pInvalidKeys, uKeyIndex);
	if (!pBatch->pInvalidKeys || index == eaiSize(&pBatch->pInvalidKeys))
		return TRANSACTION_OUTCOME_FAILURE;
	eaiRemove(&pBatch->pInvalidKeys, index);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Make a product key batch invalid.
void productKeyBatchSetInvalid(const ProductKeyBatch *pKey, bool bInvalid)
{
	AutoTrans_trKeyBatchSetInvalid(astrRequireSuccess("productKeyBatchSetInvalid"), objServerType(),
		GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, pKey->uID, !!bInvalid);
}

// Make product key batches invalid.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Batchinvalidated");
enumTransactionOutcome trKeyBatchSetInvalid(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, int bInvalid)
{
	pBatch->batchInvalidated = !!bInvalid;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Move a key from the new pile to the used pile.  Does no error checking.
static void moveKeyToUsed(SA_PARAM_NN_VALID const ProductKey *pKey, U32 uAccountID)
{
	KeyLookasideInfo *info;
	char *name = NULL;

	if (!devassert(pKey && uAccountID)) return;

	// Look up key.
	info = GetLookasideByHandle(pKey);
	if (!info)
		return;

	// Add to lookaside.
	info->uAccountId = uAccountID;

	// Subtract one key from the used keys.
	AutoTrans_trUseOneKey(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, pKey->uBatchId);
}

// Update the used key count.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Uusedkeys");
enumTransactionOutcome trUseOneKey(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch)
{
	++pBatch->uUsedKeys;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Remove a key from the used key count, if it actually exists.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Pkeys, .Uusedkeys");
enumTransactionOutcome trUnuseOneKey(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, const char *pKey)
{
	const char *i;
	for(i = pBatch->pKeys; i < pBatch->pKeys + estrLength(&pBatch->pKeys); i += PRODUCT_KEY_SIZE)
	{
		if (!strnicmp(i, pKey, PRODUCT_KEY_SIZE))
		{
			devassert(pBatch->uUsedKeys);
			--pBatch->uUsedKeys;
			return TRANSACTION_OUTCOME_SUCCESS;
		}
	}
	return TRANSACTION_OUTCOME_FAILURE;
}

// Add a key to the account's records of which keys it has consumed
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppproductkeys");
enumTransactionOutcome trAccountAddProductKey(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	eaPush(&pAccount->ppProductKeys, strdup(pKey));
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Remove an activated product key from an account.
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppproductkeys");
enumTransactionOutcome trAccountRemoveProductKey(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	// Remove the key from the account.
	int index = eaFindString(&pAccount->ppProductKeys, pKey);
	if (index != -1)
		eaRemove(&pAccount->ppProductKeys, index);
	return index == -1 ? TRANSACTION_OUTCOME_FAILURE : TRANSACTION_OUTCOME_SUCCESS;
}

// Remove an activated product key from an account.
AUTO_COMMAND;
bool AccountRemoveProductKey(U32 uAccountId, const char *pKey)
{
	KeyLookasideInfo *info;
	TransactionReturnVal result;
	bool success;

	// Remove key from the account.
	AutoTrans_trAccountRemoveProductKey(&result, objServerType(), GLOBALTYPE_ACCOUNT, uAccountId, pKey);
	success = result.eOutcome == TRANSACTION_OUTCOME_SUCCESS;
	objReleaseTransactionReturn(&result);

	// Update lookaside and decrement key count, if this is a real key.
	info = GetLookasideByName(pKey);
	if (info)
	{
		info->uAccountId = 0;
		AutoTrans_trUnuseOneKey(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, info->uBatchId, pKey);
		success = true;
	}

	return success;
}

AUTO_STRUCT;
typedef struct ActivateProductLock
{
	EARRAY_OF(AccountProxyProductActivation) eaActivation;
	U32 uLockTime;
} ActivateProductLock;

static ProductKeyResult convertActivateProductResultToKeyResult(ActivateProductResult eResult)
{
	switch (eResult)
	{
	xcase APR_Success:
		return PK_InternalError;
	xcase APR_InvalidParameter:
		return PK_InternalError;
	xcase APR_KeyValueLockFailure:
		return PK_CouldNotLockKeyValue;
	xcase APR_KeyValueCommitFailure:
		return PK_CouldNotCommitKeyValue;
	xcase APR_KeyValueRollbackFailure:
		return PK_CouldNotRollbackKeyValue;
	xcase APR_ProductKeyDistributionLockFailure:
		return PK_CouldNotDistributeKey;
	xcase APR_ProductKeyDistributionCommitFailure:
		return PK_CouldNotDistributeKey;
	xcase APR_ProductKeyDistributionRollbackFailure:
		return PK_CouldNotDistributeKey;
	xcase APR_InternalSubCreationFailure:
		return PK_CouldNotCreateInternalSub;
	xcase APR_InternalSubAlreadyExistsOnAccount:
		return PK_CouldNotCreateInternalSub;
	xcase APR_CouldNotAssociateProduct:
		return PK_CouldNotAssociateProduct;
	xcase APR_ProductAlreadyAssociated:
		return PK_CouldNotAssociateProduct;
	xcase APR_PrerequisiteFailure:
		return PK_PrerequisiteFailure;
	xcase APR_InvalidReferrer:
		return PK_InvalidReferrer;
	xcase APR_Failure:
	default:
		return PK_CouldNotActivateProduct;
	}
	return PK_InternalError;
}

ProductKeyResult activateProductKeyLock(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pReferrer, SA_PARAM_NN_VALID ActivateProductLock **pOutLock)
{
	ProductKey thisKey;
	ProductKeyBatch *keyBatch = NULL;
	bool bFailedProductLocking = false;
	ProductKeyResult eResult = PK_Invalid;
	ActivateProductResult eActivateProductResult = APR_Failure;
	bool bProductKeyIsValid = false;
	KeyLookasideInfo *info = NULL;
	ProductKeyGroup *group = NULL;

	if (!devassert(pAccount && pKey && pOutLock))
		goto error;

	// Look up key.
	bProductKeyIsValid = productKeyIsValid(&thisKey, pKey);
	info = GetLookasideByName(pKey);
	if (!info)
		bProductKeyIsValid = false;

	*pOutLock = StructCreate(parse_ActivateProductLock);
	if (!devassert(*pOutLock))
	{
		eResult = PK_InternalError;
		goto error;
	}

	accountLog(pAccount, "Key activation attempt: %s", pKey);

	// Make sure the key exists
	if (!bProductKeyIsValid)
	{
		accountLog(pAccount, "Key activation failed (invalid key): %s", pKey);
		eResult = PK_KeyInvalid;
		goto error;
	}

	// Get the batch (won't return null because productKeyIsValid returns NULL in that case)
	keyBatch = getKeyBatchByID(thisKey.uBatchId);

	// Make sure the product key is not locked
	if (productKeyIsLocked(&thisKey))
	{
		accountLog(pAccount, "Key activation failed (key locked): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
		eResult = PK_KeyLocked;
		goto error;
	}

	// Make sure the key has not yet been used
	if (info->uAccountId)
	{
		accountLog(pAccount, "Key activation failed (key used): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
		eResult = PK_KeyUsed;
		goto error;
	}

	// Make sure that, if this key was distributed by a buddy, that we aren't the recruiter and the recruit
	if (info->uDistributedAccountId == pAccount->uID)
	{
		EARRAY_CONST_FOREACH_BEGIN(pAccount->eaRecruits, iCurRecruit, iNumRecruits);
		{
			const RecruitContainer *pRecruit = pAccount->eaRecruits[iCurRecruit];

			if (!devassert(pRecruit)) continue;

			if (!stricmp_safe(pRecruit->pProductKey, pKey))
			{
				accountLog(pAccount, "Key activation failed (cannot recruit self): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
				eResult = PK_KeyUsed;
				goto error;
			}
		}
		EARRAY_FOREACH_END;
	}

	// Get key group.
	group = keyGroupFromBatch(keyBatch);
	if (!devassert(group))
		goto error;

	// Lock the products
	EARRAY_CONST_FOREACH_BEGIN(group->ppProducts, i, size);
	{
		const ProductContainer *product = findProductByName(group->ppProducts[i]);
		AccountProxyProductActivation *activation = NULL;

		if (!product)
		{
			AssertOrAlert("ACCOUNTSERVER_ACTIVATION_KEYBATCH_PRODUCT_MISSING", "Key batch %s is configured to activate a product that does not exist: '%s'", keyBatch->pBatchName, group->ppProducts[i]);
			accountLog(pAccount, "Key activation failed (could not find product [product:%s]): %s (batch: [keybatch:%s])", group->ppProducts[i], pKey, keyBatch->pBatchName);
			bFailedProductLocking = true;
			break;
		}

		eActivateProductResult = accountActivateProductLock(pAccount, product, NULL, NULL, NULL, pReferrer, &activation);
		if (eActivateProductResult != APR_Success)
		{
			accountLog(pAccount, "Key activation failed (could not lock product [product:%s]): %s (batch: [keybatch:%s])", product->pName, pKey, keyBatch->pBatchName);
			bFailedProductLocking = true;
			break;
		}

		eaPush(&((*pOutLock)->eaActivation), activation);
	}
	EARRAY_FOREACH_END;

	// Roll back locks if it couldn't lock everything
	if (bFailedProductLocking)
	{
		EARRAY_CONST_FOREACH_BEGIN(group->ppProducts, i, size);
			const ProductContainer *product;

			if (i >= eaSize(&((*pOutLock)->eaActivation)))
				break;

			product = findProductByName(group->ppProducts[i]);

			if (!devassert(product))
				continue;

			if (accountActivateProductRollback(pAccount, product, (*pOutLock)->eaActivation[i], false) != APR_Success)
			{
				AssertOrAlert("ACCOUNTSERVER_ACTIVATION_ROLLBACK_FAILURE", "Failed to roll back product lock for the product %s lock as a part of key activation for the key %s and account %s", group->ppProducts[i], pKey, pAccount->accountName);
			}

			(*pOutLock)->eaActivation[i] = NULL; // Already cleaned up by commit or rollback
		EARRAY_FOREACH_END;

		eResult = convertActivateProductResultToKeyResult(eActivateProductResult);
		goto error;
	}

	devassert((*pOutLock)->eaActivation);
	devassert(eaSize(&((*pOutLock)->eaActivation)) == eaSize(&group->ppProducts));

	(*pOutLock)->uLockTime = timeSecondsSince2000();

	// Lock the key.
	asgAddKeyLock(pKey, (*pOutLock)->uLockTime);

	return PK_Pending;

error:

	// Clean up the activation data
	if (*pOutLock)
	{
		StructDestroy(parse_ActivateProductLock, *pOutLock);
		*pOutLock = NULL;
	}

	if (!devassert(eResult != PK_Invalid)) eResult = PK_InternalError;
	return eResult;
}

ProductKeyResult activateProductKeyFinish(SA_PARAM_NN_VALID AccountInfo *pAccount,
										  SA_PARAM_NN_STR const char *pKey,
										  SA_PARAM_NN_VALID ActivateProductLock *pLock,
										  bool bCommit)
{
	ProductKey thisKey = {0};
	KeyLookasideInfo *info = NULL;
	ProductKeyBatch *keyBatch = NULL;
	ProductKeyGroup *group = NULL;
	U32 uDistributedAccountID = 0;
	bool bValidKey = false;

	if (!devassert(pLock))
	{
		accountLog(pAccount, "Key activation failed (not locked): %s", pKey ? pKey : "(NULL)");
		return PK_KeyNotLocked;
	}

	if (devassert(pKey))
	{
		// Look up key.
		bValidKey = productKeyIsValid(&thisKey, pKey);
		info = GetLookasideByName(pKey);
		if (!info)
			bValidKey = false;
	}

	// Make sure the key exists
	if (!bValidKey)
	{
		accountLog(pAccount, "Key activation failed (invalid key): %s", pKey ? pKey : "(NULL)");
		StructDestroy(parse_ActivateProductLock, pLock);
		return PK_KeyInvalid;
	}

	// Get the batch (won't return null because productKeyIsValid returns NULL in that case)
	keyBatch = getKeyBatchByID(thisKey.uBatchId);

	// Make sure the product key is locked
	if (!productKeyIsLocked(&thisKey))
	{
		accountLog(pAccount, "Key activation failed (key not locked): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
		StructDestroy(parse_ActivateProductLock, pLock);
		return PK_KeyNotLocked;
	}

	// Make sure the lock is the same
	if (productKeyGetLockTime(&thisKey) != pLock->uLockTime)
	{
		accountLog(pAccount, "Key activation failed (key locked but not by this account): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
		StructDestroy(parse_ActivateProductLock, pLock);
		return PK_KeyLocked;
	}

	// Make sure the key has not yet been used
	if (info->uAccountId)
	{
		accountLog(pAccount, "Key activation failed (key used): %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
		StructDestroy(parse_ActivateProductLock, pLock);
		return PK_KeyUsed;
	}

	// Only consume the key if we are committing
	if (bCommit)
	{
		accountLog(pAccount, "Key being consumed: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);

		uDistributedAccountID = info->uDistributedAccountId;

		// Move the key from the new pile to the used pile
		moveKeyToUsed(&thisKey, pAccount->uID);

		// Add the key to the account
		AutoTrans_trAccountAddProductKey(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
		unlockProductKey(pKey);
		accountReportProductKeyActivate(thisKey.uBatchId, 1);
	}
	else
	{
		accountLog(pAccount, "Key not consumed: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);

		unlockProductKey(pKey);
	}

	// Get key group.
	group = keyGroupFromBatch(keyBatch);
	if (!devassert(group))
		return PK_InternalError;

	// Activate the products associated with the key
	EARRAY_CONST_FOREACH_BEGIN(group->ppProducts, i, size);
	{
		const ProductContainer *product = findProductByName(group->ppProducts[i]);

		if (!devassert(product)) continue;
		if (!devassert(pLock->eaActivation[i])) continue;

		if (bCommit)
		{
			ActivateProductResult eActivationResult;
			if ((eActivationResult = accountActivateProductCommit(pAccount, product, pKey, 0, pLock->eaActivation[i], NULL, false) != APR_Success))
			{
				AssertOrAlert("ACCOUNTSERVER_ACTIVATION_COMMIT_FAILURE",
					"Failed to commit product for the product %s lock as a part of key activation for the key %s and account %s: %s",
					group->ppProducts[i], pKey, pAccount->accountName, StaticDefineIntRevLookupNonNull(ActivateProductResultEnum, eActivationResult));
			}

			// Create the recruit/recruiter relationship if needed
			if (uDistributedAccountID && uDistributedAccountID != pAccount->uID)
			{
				AccountInfo *pRecruiterAccount = findAccountByID(uDistributedAccountID);

				if (devassert(pRecruiterAccount))
				{
					accountRecruited(pRecruiterAccount, pAccount, pKey);
				}
			}
		}
		else
		{
			if (accountActivateProductRollback(pAccount, product, pLock->eaActivation[i], false) != APR_Success)
			{
				AssertOrAlert("ACCOUNTSERVER_ACTIVATION_ROLLBACK_FAILURE", "Failed to roll back product lock for the product %s lock as a part of key activation for the key %s and account %s", group->ppProducts[i], pKey, pAccount->accountName);
			}
		}

		pLock->eaActivation[i] = NULL; // Already cleaned up by commit or rollback
	}
	EARRAY_FOREACH_END;

	StructDestroy(parse_ActivateProductLock, pLock);

	if (bCommit)
		accountLog(pAccount, "Key activated: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
	else
		accountLog(pAccount, "Key rolled back: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);

	return PK_Success;
}

// Assign the product key to the specified user
ProductKeyResult activateProductKey(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pReferrer)
{
	ActivateProductLock *lock = NULL;
	ProductKeyResult ret;

	// Lock the key
	ret = activateProductKeyLock(pAccount, pKey, pReferrer, &lock);
	if (ret != PK_Pending)
		return ret;
	
	return activateProductKeyCommit(pAccount, pKey, lock);
}

// Distribute a key to the specified user
AUTO_TRANSACTION
ATR_LOCKS(pAccount, ".Ppdistributedkeys, .Ppdeprecateddistributedproductkeys");
enumTransactionOutcome trAccountDistributeProductKeyCommit(ATR_ARGS, NOCONST(AccountInfo) *pAccount, const char *pKey)
{
	NOCONST(DistributedKeyContainer) *pDistributedKey;

	if (!verify(NONNULL(pAccount))) return TRANSACTION_OUTCOME_FAILURE;
	if (!verify(NONNULL(pKey) && *pKey)) return TRANSACTION_OUTCOME_FAILURE;

	pDistributedKey = StructCreateNoConst(parse_DistributedKeyContainer);

	pDistributedKey->pActivationKey = strdup(pKey);
	pDistributedKey->uDistributedTimeSS2000 = timeSecondsSince2000();

	eaPush(&pAccount->ppDistributedKeys, pDistributedKey);
	eaPush(&pAccount->ppDeprecatedDistributedProductKeys, strdup(pKey));

	return TRANSACTION_OUTCOME_SUCCESS;
}

// Unlock a product key.
ProductKeyResult unlockProductKey(SA_PARAM_NN_STR const char *pKey)
{
	ProductKey thisKey;
	bool success;

	PERFINFO_AUTO_START_FUNC();
	success = productKeyIsValid(&thisKey, pKey);

	if (!success)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyInvalid;
	}

	if (!productKeyIsLocked(&thisKey))
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyNotLocked;
	}

	// Remove lock.
	success = asgRemoveKeyLock(pKey);
	PERFINFO_AUTO_STOP();

	if (!success) return PK_InternalError;

	return PK_Success;
}

// Distribute a key to the specified user
ProductKeyResult distributeProductKeyFinish(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, U32 uLockTime, bool bCommit)
{
	ProductKey thisKey;
	bool success;
	ProductKeyBatch *keyBatch;
	KeyLookasideInfo *info;

	PERFINFO_AUTO_START_FUNC();

	// Make sure the key exists
	success = productKeyIsValid(&thisKey, pKey);
	if (!success)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyInvalid;
	}
	info = GetLookasideByName(pKey);
	if (!info)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyInvalid;
	}

	// Make sure the key has not yet been used
	if (info->uAccountId)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyUsed;
	}

	// Get the batch (won't return null because productKeyIsValid returns NULL in that case)
	keyBatch = getKeyBatchByID(info->uBatchId);

	// Make sure it hasn't already been distributed
	if (keyBatch->batchDistributed || !productKeyIsLocked(&thisKey) || productKeyIsDistributed(&thisKey)
		|| productKeyGetLockTime(&thisKey) != uLockTime)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyDistributed;
	}

	if (bCommit)
	{
		ProductKeyResult unlockResult;

		ADD_MISC_COUNT(1, "Distribute");
		AutoTrans_trAccountDistributeProductKeyCommit(NULL, objServerType(), GLOBALTYPE_ACCOUNT, pAccount->uID, pKey);
		unlockResult = unlockProductKey(pKey);
		devassert(unlockResult == PK_Success);
		devassert(!info->uDistributedAccountId);
		info->uDistributedAccountId = pAccount->uID;

		accountLog(pAccount, "Key distributed: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
	}
	else
	{
		ADD_MISC_COUNT(1, "Rollback");
		unlockProductKey(pKey);
		accountLog(pAccount, "Key distribution cancelled: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
	}

	PERFINFO_AUTO_STOP();
	return PK_Success;
}

// Lock the distribution of a key to a specified user
ProductKeyResult distributeProductKeyLock(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID U32 *pLockTime)
{
	ProductKey thisKey;
	ProductKeyBatch *keyBatch;
	KeyLookasideInfo *info;
	bool success;

	PERFINFO_AUTO_START_FUNC();

	// Make sure the key exists
	success = productKeyIsValid(&thisKey, pKey);
	if (!success)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyInvalid;
	}
	info = GetLookasideByName(pKey);
	if (!info)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyInvalid;
	}

	// Make sure the key has not yet been used
	if (info->uAccountId)
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyUsed;
	}

	// Get the batch (won't return null because productKeyIsValid returns NULL in that case)
	keyBatch = getKeyBatchByID(thisKey.uBatchId);

	// Make sure it hasn't already been distributed
	if (keyBatch->batchDistributed || productKeyIsDistributed(&thisKey) || productKeyIsLocked(&thisKey))
	{
		PERFINFO_AUTO_STOP();
		return PK_KeyDistributed;
	}

	*pLockTime = timeSecondsSince2000();

	// Lock the key.
	asgAddKeyLock(pKey, *pLockTime);
	accountLog(pAccount, "Key distribution locked: %s (batch: [keybatch:%s])", pKey, keyBatch->pBatchName);
	PERFINFO_AUTO_STOP();

	return PK_Success;
}

// Mark a product key batch as distributed or not.
void productKeyBatchSetDistributed(const ProductKeyBatch *pBatch, bool bDistributed)
{
	AutoTrans_trKeyBatchSetDistributed(astrRequireSuccess("productKeyBatchSetDistributed"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
		pBatch->uID, !!bDistributed);
}

// Mark a product key batch as distributed.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Batchdistributed");
enumTransactionOutcome trKeyBatchSetDistributed(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, int bDistributed)
{
	pBatch->batchDistributed = !!bDistributed;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Get the account ID that a key was distributed to.
U32 getDistributedAccountId(SA_PARAM_NN_VALID const ProductKey *pKey)
{
	KeyLookasideInfo *info;
	char *name = NULL;

	// Make sure the key exists
	info = GetLookasideByHandle(pKey);
	if (!devassert(info))
		return 0;

	// Return distributed account ID.
	return info->uDistributedAccountId;
}

#define ACTIVATION_KEY_LEN 10
char * generateActivationKey_s (char *buffer, size_t buffer_size)
{
	U32 randval1, randval2;
	assert (buffer_size >= ACTIVATION_KEY_LEN + 1);

	randval1 = cryptSecureRand() % GENERATE_MOD;
	randval2 = cryptSecureRand() % GENERATE_MOD;

	convertToTableBase(randval1, buffer);
	convertToTableBase(randval2, buffer+5);
	buffer[ACTIVATION_KEY_LEN + 1] = '\0';
	return buffer;
}

static char * generateSingleKey_s (const char *productPrefix, char * buffer, size_t buffer_size)
{
	int i;
	assert(buffer_size >= 26);
	strcpy_s(buffer, PRODUCT_KEY_PREFIX_SIZE+1, productPrefix);
	for	(i=1; i<5; i++)
	{
		U32 uRand = (U32)cryptSecureRand() % GENERATE_MOD;
		convertToTableBase(uRand, buffer+i*5);
	}
	buffer[25] = '\0';
	return buffer;
}

// Data to pass to trAddKeysToBatch().
AUTO_STRUCT;
typedef struct AddKeysToBatchData
{
	STRING_EARRAY ppKeys;			// List of keys to add to the batch
} AddKeysToBatchData;

// Add a new key to a batch.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Pkeys");
enumTransactionOutcome trAddKeysToBatch(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, AddKeysToBatchData *pData)
{
	EARRAY_CONST_FOREACH_BEGIN(pData->ppKeys, i, n);
	{
		estrAppend2(&pBatch->pKeys, pData->ppKeys[i]);
	}
	EARRAY_FOREACH_END;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Get a list of product containers associated with a key.
void findProductsFromKey(SA_PARAM_NN_STR const char *pKey, ProductContainer *** pppProductsOut)
{
	char prefix[PRODUCT_KEY_PREFIX_SIZE+1];
	ProductKeyGroup *pKeyGroup = NULL;
	ProductKey thisKey;
	ProductKeyBatch *keyBatch;
	bool success;
	KeyLookasideInfo *info;

	if (!parseProductPrefix(prefix, pKey))
		return;

	success = findProductKey(&thisKey, pKey);
	pKeyGroup = findProductKeyGroup(prefix);

	info = GetLookasideByName(pKey);
	if (!info)
		success = false;

	if (!success || pKeyGroup == NULL)
		return;

	keyBatch = getKeyBatchByID(thisKey.uBatchId);
	if (keyBatch == NULL)
		return;

	EARRAY_CONST_FOREACH_BEGIN(pKeyGroup->ppProducts, i, s);
		const ProductContainer *pProduct = findProductByName(pKeyGroup->ppProducts[i]);

		if (pProduct)
			eaPush(pppProductsOut, StructClone(parse_ProductContainer, pProduct));
	EARRAY_FOREACH_END;
}

ProductKeyGroup* findKeyGroupFromString(const char * prefixOrKey)
{
	char paddedPrefix[PRODUCT_KEY_PREFIX_SIZE+1];

	// Get key prefix.
	if (!prefixOrKey || !prefixOrKey[0])
		return NULL;
	if (strlen(prefixOrKey) > PRODUCT_KEY_PREFIX_SIZE)
		parseProductPrefix(paddedPrefix, prefixOrKey);
	else
		getPaddedPrefix(paddedPrefix, prefixOrKey);

	// Search for group.
	if (isAccountServerMode(ASM_KeyGenerating))
	{
		EARRAY_CONST_FOREACH_BEGIN(keyGenData.groups, i, n);
		{
			ProductKeyGroup *group = keyGenData.groups[i];
			if (strcmp(group->productPrefix, paddedPrefix) == 0)
				return group;
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, groupContainer);
		{
			ProductKeyGroup *group = (ProductKeyGroup *)groupContainer->containerData;
			if (strcmp(group->productPrefix, paddedPrefix) == 0)
				return group;
		}
		CONTAINER_FOREACH_END;
	}

	return NULL;
}

// Get the key group that holds a batch.
ProductKeyGroup *keyGroupFromBatch(const ProductKeyBatch *batch)
{
	// Search for batch
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, groupContainer);
	{
		ProductKeyGroup *group = (ProductKeyGroup *)groupContainer->containerData;
		EARRAY_INT_CONST_FOREACH_BEGIN(group->keyBatches, i, n);
			if (group->keyBatches[i] == batch->uID)
				return group;
		EARRAY_FOREACH_END;
	}
	CONTAINER_FOREACH_END;

	// Not found
	return NULL;
}

// Batch creation information.
struct productKeysAddBatchData {
	NetLink *link;
	U32 uGroupId;
};

// Handle result of key batch creation.
static void productKeysAddBatch_CB(TransactionReturnVal *returnVal, void *pData)
{
	U32 id = GetNewContainerId(returnVal);
	Packet *response;
	struct productKeysAddBatchData *data = pData;

	/// Save batch ID in key group.
	AutoTrans_trAddBatchToKeyGroup(astrRequireSuccess("productKeysAddBatch_CB"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYGROUP,
		data->uGroupId, id);

	// Update maximum batch id.
	uMaxBatchId = MAX(uMaxBatchId, id);

	// Send response to generating server.
	response = pktCreate(data->link, FROM_ACCOUNTSERVER_PKBATCHCREATED);
	pktSendU32(response, id);
	pktSend(&response);
}

// Returns the assigned ID of the key batch, 0 if it failed
static void productKeysAddBatch(const char *pPrefix, SA_PARAM_NN_VALID NOCONST(ProductKeyBatch) *keyBatch, NetLink *link)
{
	const ProductKeyGroup *keyGroup = findKeyGroupFromString(pPrefix);
	struct productKeysAddBatchData *data;

	// Make sure this is a valid request.
	if (!devassert(keyBatch))
		return;
	if (!keyGroup || strlen(pPrefix) > PRODUCT_KEY_PREFIX_SIZE)
	{
		Packet *response = pktCreate(link, FROM_ACCOUNTSERVER_PKBATCHCREATED);
		pktSendU32(response, 0);
		pktSend(&response);
		return;
	}

	// Save batch creation information.
	data = malloc(sizeof(*data));
	data->link = link;
	data->uGroupId = keyGroup->uID;

	// Create batch.
	devassert(!keyBatch->uID);
	devassert(!keyBatch->timeCreated);
	devassert(!estrLength(&keyBatch->pKeys));
	keyBatch->timeCreated = timeSecondsSince2000();
	keyBatch->uKeySize = PRODUCT_KEY_SIZE;
	objRequestContainerCreateLocal(objCreateManagedReturnVal(productKeysAddBatch_CB, data), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, keyBatch);
}

// Set the time that a batch was downloaded.
AUTO_TRANSACTION
ATR_LOCKS(pGroup, ".Keybatches");
enumTransactionOutcome trAddBatchToKeyGroup(ATR_ARGS, NOCONST(ProductKeyGroup) *pGroup, U32 uId)
{
	eaiPush(&pGroup->keyBatches, uId);
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Handle packets from a key generating Account Server
void HandleMessageFromKeyGen(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	U32 start = timeGetTime();
	const char *action = "";

	switch(cmd)
	{

		// Get key group information.
		xcase TO_ACCOUNTSERVER_GET_PKGROUPS:
		{
			// Send everything = internal product name, product prefix
			char *keyGroupString = NULL;
			ProductKeyGenerationData wrapper = {0};
			Packet *response;

			action = "GetPKGroups";

			StructInit(parse_ProductKeyGenerationData, &wrapper);
			getProductKeyGenerationData(&wrapper);
			ParserWriteText(&keyGroupString, parse_ProductKeyGenerationData, &wrapper, 0, 0, 0);
			StructDeInit(parse_ProductKeyGenerationData, &wrapper);
			response = pktCreate(link, FROM_ACCOUNTSERVER_PKGROUPS);
			pktSendString(response, keyGroupString);
			pktSend(&response);
			estrDestroy(&keyGroupString);
		}

		// Create a key batch.
		xcase TO_ACCOUNTSERVER_CREATE_KEYBATCH:
		{
			char prefix[PRODUCT_KEY_PREFIX_SIZE + 1] = {0};
			char *keyBatch;
			NOCONST(ProductKeyBatch) *batch;

			action = "CreateBatch";

			// Get packet data.
			PKT_CHECK_STR(pak, packetfailed);
			pktGetString(pak, prefix, sizeof(prefix));
			PKT_CHECK_STR(pak, packetfailed);
			keyBatch = pktGetStringTemp(pak);

			batch = StructCreateNoConst(parse_ProductKeyBatch);

			if (ParserReadText(keyBatch, parse_ProductKeyBatch, batch, 0))
				productKeysAddBatch(prefix, batch, link);
			else
				StructDestroyNoConst(parse_ProductKeyBatch, batch);
		}

		// Add keys to a key batch.
		xcase TO_ACCOUNTSERVER_ADDKEYS:
		{
			Packet *response;
			char *keyBatch;
			ProductKeyBatch *batch;

			PKT_CHECK_STR(pak, packetfailed);
			keyBatch = pktGetStringTemp(pak);

			batch = StructCreate(parse_ProductKeyBatch);
			action = "AddKeys";

			if (ParserReadText(keyBatch, parse_ProductKeyBatch, batch, 0))
			{
				int created = productKeysAdd(batch);
				if (created >= 0)
				{
					response = pktCreate(link, FROM_ACCOUNTSERVER_PKCREATED);
					pktSendU32(response, created);
					pktSend(&response);
				}
				else
				{
					response = pktCreate(link, FROM_ACCOUNTSERVER_FAILED);
					pktSendString(response, "Invalid key batch ID.");
					pktSend(&response);
				}
			}
			else
			{
				response = pktCreate(link, FROM_ACCOUNTSERVER_FAILED);
				pktSendString(response, "Bad key batch string received.");
				pktSend(&response);
			}
			StructDestroy(parse_ProductKeyBatch, batch);
		}

		xdefault:
			devassert(0);
	}

	packetfailed:

	// Log the request.
	servLog(LOG_ACCOUNT_SERVER_GENERAL, action, "time %lu", timeGetTime() - start);
}

// Mark a batch as downloaded.
void batchDownloaded(ProductKeyBatch *pKeyBatch)
{
	AutoTrans_trSetBatchDownloaded(astrRequireSuccess("batchDownloaded"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
		pKeyBatch->uID, timeSecondsSince2000());
}

// Set the time that a batch was downloaded.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Timedownloaded");
enumTransactionOutcome trSetBatchDownloaded(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, U32 uDownloaded)
{
	pBatch->timeDownloaded = uDownloaded;
	return TRANSACTION_OUTCOME_SUCCESS;
}

void productKeyBatchAddFromStruct(ProductKeyBatch *pKeyBatch)
{
	ProductKeyBatch *onlyKeys = StructCreate(parse_ProductKeyBatch);
	TransactionReturnVal result;

	CONTAINER_NOCONST(ProductKeyBatch, onlyKeys)->ppBatchKeys = pKeyBatch->ppBatchKeys;
	pKeyBatch->ppBatchKeys = NULL;

	objRequestContainerCreateLocal(&result, GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, pKeyBatch);
	while (result.eOutcome == TRANSACTION_OUTCOME_NONE)
		UpdateObjectTransactionManager();
	CONTAINER_NOCONST(ProductKeyBatch, onlyKeys)->uID = GetNewContainerId(&result);
	ReleaseReturnValData(objLocalManager(), &result);
	assert(onlyKeys->uID);
	productKeysAdd(onlyKeys);
	StructDestroy(parse_ProductKeyBatch, onlyKeys);
}

// Add a block of product keys to a batch.
int productKeysAdd(ProductKeyBatch *addBatch)
{
	ProductKeyBatch * localBatch = getKeyBatchByID(addBatch->uID);
	int count = 0;
	int i, size;
	ProductKey key;
	ProductKeyGroup *group;
	bool success;
	char *newKeys = NULL;
	int *oldIndices = NULL;
	int baseId;
	AddKeysToBatchData data;

	PERFINFO_AUTO_START_FUNC();

	// Make sure the batch exists.
	if (!localBatch)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return -1;
	}

	// Make sure there's a key group for this batch.
	group = keyGroupFromBatch(localBatch);
	if (!group)
	{
		devassert(0);
		return -1;
		PERFINFO_AUTO_STOP_FUNC();
	}

	// Sort keys and remove those that are invalid for some reason.
	eaQSort(addBatch->ppBatchKeys, strCmp);
	size = eaSize(&addBatch->ppBatchKeys);
	if (!size)
	{
		PERFINFO_AUTO_STOP_FUNC();
		return 0;
	}
	for (i = 0; i < eaSize(&addBatch->ppBatchKeys); ++i)
	{
		char *keyString = addBatch->ppBatchKeys[i];
		if (!addBatch->ppBatchKeys[i]																		// Null
			|| i < eaSize(&addBatch->ppBatchKeys) - 1 && !stricmp(keyString, addBatch->ppBatchKeys[i + 1])	// Duplicate
			|| findProductKey(&key, keyString)																// Already exists
			|| strlen(keyString) != PRODUCT_KEY_SIZE														// Incorrect key length
			|| strnicmp(keyString, group->productPrefix, PRODUCT_KEY_PREFIX_SIZE))							// Incorrect prefix
		{
				eaRemove(&addBatch->ppBatchKeys, i);
				--i;
		}
	}

	// Get base ID of new keys.
	devassert(estrLength(&localBatch->pKeys) % PRODUCT_KEY_SIZE == 0);
	baseId = estrLength(&localBatch->pKeys)/PRODUCT_KEY_SIZE;

	// Save the old keys and indices.
	eaiSetCapacity(&oldIndices, baseId);
	for (i = 0; i != baseId; ++i)
	{
		int index;
		success = stashRemoveInt(stKeyLookaside, localBatch->pKeys + i * PRODUCT_KEY_SIZE, &index);
		devassert(success);
		eaiPush(&oldIndices, index);
	}

	// Add key batch to the key list.
	data.ppKeys = addBatch->ppBatchKeys;
	AutoTrans_trAddKeysToBatch(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
		localBatch->uID, &data);
	devassert(estrLength(&localBatch->pKeys) % PRODUCT_KEY_SIZE == 0);

	// Re-add old keys to StashTable.
	for (i = 0; i < baseId; ++i)
	{
		success = stashAddInt(stKeyLookaside, localBatch->pKeys + i * PRODUCT_KEY_SIZE, oldIndices[i], false);
		devassert(success);
	}
	eaiDestroy(&oldIndices);

	// Add new keys to lookaside table.
	for (i = 0; i < eaSize(&addBatch->ppBatchKeys); ++i)
	{
		KeyLookasideInfo *info;

		// Create lookaside entry.
		info = beaPushEmptyStruct(&pKeyLookaside, parse_KeyLookasideInfo);
		info->uBatchId = localBatch->uID;
		info->uKeyId = baseId + i;

		// Add to StashTable.
		success = stashAddInt(stKeyLookaside, localBatch->pKeys + (baseId + i) * PRODUCT_KEY_SIZE, info - pKeyLookaside, false);
		devassert(success);
	}

	return eaSize(&addBatch->ppBatchKeys);
}

ProductKeyBatch *getKeyBatchByID(U32 uID)
{
	if (isAccountServerMode(ASM_KeyGenerating))
	{
		EARRAY_CONST_FOREACH_BEGIN(keyGenData.batches, i, n);
		{
			if (keyGenData.batches[i]->uID == uID)
				return keyGenData.batches[i];
		}
		EARRAY_FOREACH_END;
	}
	else
	{
		Container *con = objGetContainer(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, uID);
		if (con)
			return (ProductKeyBatch*) con->containerData;
	}
	return NULL;
}

SA_RET_OP_VALID const ProductKeyBatch * getKeyBatchByName(SA_PARAM_NN_STR const char *pName)
{
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, batchContainer);
	{
		ProductKeyBatch *batch = (ProductKeyBatch *)batchContainer->containerData;
		if (!strcmpi(batch->pBatchName, pName))
			return batch;
	}
	CONTAINER_FOREACH_END;

	return NULL;
}

// Iterate over key batches.
void iterateKeyBatches(fpKeyBatchCallback fpCallback, void *pUserData)
{
	PERFINFO_AUTO_START_FUNC();

	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, batchContainer);
	{
		ProductKeyBatch *batch = (ProductKeyBatch *)batchContainer->containerData;
		fpCallback(batch, pUserData);
	}
	CONTAINER_FOREACH_END;

	PERFINFO_AUTO_STOP_FUNC();
}

// Get the maximum batch ID that currently exists.
int getMaxBatchId()
{
	return uMaxBatchId;
}

// Get array of product key groups.
CONST_EARRAY_OF(ProductKeyGroup) getProductKeyGroupList(void)
{
	static EARRAY_OF(ProductKeyGroup) groups = NULL;
	eaClear(&groups);
	if (isAccountServerMode(ASM_KeyGenerating))
		eaCopy(&groups, &keyGenData.groups);
	else
	{
		CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, groupContainer);
		{
			ProductKeyGroup *group = (ProductKeyGroup *)groupContainer->containerData;
			eaPush(&groups, group);
		}
		CONTAINER_FOREACH_END;
	}
	return groups;
}

// Get product key generation data.
void getProductKeyGenerationData(ProductKeyGenerationData *pData)
{
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, groupContainer);
	{
		ProductKeyGroup *group = (ProductKeyGroup *)groupContainer->containerData;
		eaPush(&pData->groups, StructClone(parse_ProductKeyGroup, group));
	}
	CONTAINER_FOREACH_END;
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, groupContainer);
	{
		ProductKeyBatch *batch = (ProductKeyBatch *)groupContainer->containerData;
		NOCONST(ProductKeyBatch) *strippedBatch = StructCloneDeConst(parse_ProductKeyBatch, batch);
		devassert(strippedBatch);
		devassert(estrLength(&strippedBatch->pKeys) % PRODUCT_KEY_SIZE == 0);
		strippedBatch->uBatchKeyCount = estrLength(&strippedBatch->pKeys) / PRODUCT_KEY_SIZE;
		estrClear(&strippedBatch->pKeys);
		eaPush(&pData->batches, (ProductKeyBatch *)strippedBatch);
	}
	CONTAINER_FOREACH_END;
}

static void ProductKeyGeneration_HandleMessage(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink)
{
	switch(cmd)
	{
	case FROM_ACCOUNTSERVER_PKGROUPS:
		{
			char *groupString = pktGetStringTemp(pak);

			StructReset(parse_ProductKeyGenerationData, &keyGenData);

			ParserReadText(groupString, parse_ProductKeyGenerationData, &keyGenData, 0);
			sbReceivedResponse = true;
		}
	xcase FROM_ACCOUNTSERVER_PKBATCHCREATED:
		{
			suBatchID = pktGetU32(pak);
			sbReceivedResponse = true;
		}
	xcase FROM_ACCOUNTSERVER_PKCREATED:
		{
			siKeysCreated = (int) pktGetU32(pak);
			sbReceivedResponse = true;
		}
	xcase FROM_ACCOUNTSERVER_FAILED:
		{
			AssertOrAlert("ACCOUNTSERVER_KEYGENERATION_FAILED", "Failed: %s", pktGetStringTemp(pak));
			sbReceivedResponse = true;
		}
	xcase FROM_ACCOUNTSERVER_VERSION:
		{
			char *version = pktGetStringTemp(pak);
			if (strcmp(GetUsefulVersionString(), version))
			{
				AssertOrAlert("ACCOUNTSERVER_VERSION_MISMATCH", "Local version \"%s\" does not match remote version \"%s\"",
					GetUsefulVersionString(), version);
			}
			sbReceivedVersion = true;
		}
	}
}

static __forceinline void waitForResponse(void)
{
	DWORD start_tick = timeGetTime();

	// Make sure we got a version back.
	if (sbReceivedResponse && !sbReceivedVersion)
	{
		AssertOrAlert("ACCOUNTSERVER_VERSION_MISSING", "The Account Server did not respond to a version request; it is probably an old version.");
		return;
	}

	while (!sbReceivedResponse)
	{
		DWORD tick = timeGetTime();
		if (tick-start_tick > 30000) // arbitrary number now
		{
			AssertOrAlert("ACCOUNTSERVER_KEYGENERATION_TIMEDOUT", "Timed out waiting for response from the primary account server.");
			break;
		}
		Sleep(1);
		commMonitor(sAccountServerComm); // since AccountServerOncePerFrame is stalled here
	}
}

// Connected to Account Server
void ProductKeyGeneration_Connected(NetLink *link, void *user_data)
{
	Packet *packet;

	// Send version.
	sbReceivedVersion = false;
	packet = pktCreate(link, TO_ACCOUNTSERVER_VERSION);
	pktSend(&packet);
}

// Open a link to the primary Account Server.
static NetLink *openAccountServerLink()
{
	NetLink *link;

	// Create Account Server Comm if necessary.
	if (!sAccountServerComm)
	{
		sAccountServerComm = commCreate(0,1);
		commSetSendTimeout(sAccountServerComm, 10.0f);
	}

	// Open connection.
	assert(sAccountServerComm);
	link = commConnect(sAccountServerComm, LINKTYPE_UNSPEC, LINK_NO_COMPRESS|LINK_FORCE_FLUSH, getAccountServer(), DEFAULT_ACCOUNTSERVER_UNENCRYPTED_PORT, 
		ProductKeyGeneration_HandleMessage, ProductKeyGeneration_Connected, NULL, 0);
	linkConnectWait(&link,2.f);

	return link;
}

int productKeysCreateBatch(const char * pProductPrefix, int iNumberOfKeys, const char *batchName, const char *batchDescription)
{
	ProductKeyGroup *group;
	NOCONST(ProductKeyBatch) *pKeyBatch = NULL;
	char *pBatchString = NULL;
	Packet *pak;
	NetLink *link;
	U32 keyCount = 0;
	StashTable keyTable;

	if (strlen(pProductPrefix) > PRODUCT_KEY_PREFIX_SIZE)
		return 4;  // Error: Invalid key group prefix.
	group = findKeyGroupFromString(pProductPrefix);
	if (!group)
		return 4;  // Error: Invalid key group prefix.

	pKeyBatch = StructCreateNoConst(parse_ProductKeyBatch);
	estrCopy2(&pKeyBatch->pBatchName, batchName);
	estrCopy2(&pKeyBatch->pBatchDescription, batchDescription);

	ParserWriteText(&pBatchString, parse_ProductKeyBatch, pKeyBatch, 0, 0, 0);
	StructDestroyNoConst(parse_ProductKeyBatch, pKeyBatch);

	link = openAccountServerLink();
	if (!link)
		return 1; // Error: link failure

	loadstart_printf("Creating Key Batch...");
	sbReceivedResponse = false;
	suBatchID = 0;
	pak = pktCreate(link, TO_ACCOUNTSERVER_CREATE_KEYBATCH);
	pktSendString(pak, group->productPrefix);
	pktSendString(pak, pBatchString);
	pktSend(&pak);
	estrDestroy(&pBatchString);

	waitForResponse();

	if (!suBatchID)
	{
		loadend_printf("Failed.");
		linkRemove(&link);
		return 2; // Error: Batch creation failure
	}
	loadend_printf("Done.");

	loadstart_printf("Creating Keys...");
	keyTable = stashTableCreateWithStringKeys(iNumberOfKeys, StashDeepCopyKeys);
	while ((int) keyCount < iNumberOfKeys)
	{
		U32 currentKeys = 0;
		NOCONST(ProductKeyBatch) tempBatch = {0};
		char ** ppKeys = NULL;

		loadstart_printf("Creating Keys %d to %d...", keyCount+1, 
			min(keyCount+suNumberKeysPerUpdate, (U32) iNumberOfKeys));
		tempBatch.uID = suBatchID;
		while (currentKeys < suNumberKeysPerUpdate && (int) (keyCount + currentKeys) < iNumberOfKeys)
		{
			char buffer[PRODUCT_KEY_SIZE+1];
			
			generateSingleKey_s(group->productPrefix, buffer, ARRAY_SIZE_CHECKED(buffer));
			if (!stashFindPointer(keyTable, buffer, NULL))
			{
				eaPush(&tempBatch.ppBatchKeys, estrCreateFromStr(buffer));
				currentKeys++;
			}
		}

		loadstart_printf("Sending keys to account server...");
		sbReceivedResponse = false;
		siKeysCreated = -1;
		pak = pktCreate(link, TO_ACCOUNTSERVER_ADDKEYS);
		ParserWriteText(&pBatchString, parse_ProductKeyBatch, &tempBatch, 0, 0, 0);
		pktSendString(pak, pBatchString);
		pktSend(&pak);
		estrDestroy(&pBatchString);

		waitForResponse();

		if (!sbReceivedResponse)
		{
			stashTableDestroy(keyTable);
			linkRemove(&link);
			loadend_printf("Failed.");
			loadend_printf("Failed.");
			loadend_printf("Failed.");
			return 1; // connection error
		}
		if (siKeysCreated == -1)
		{
			stashTableDestroy(keyTable);
			linkRemove(&link);
			loadend_printf("Failed.");
			loadend_printf("Failed.");
			loadend_printf("Failed.");
			return 3; // key creation failed
		}
		loadend_printf("Done.");
		loadend_printf("Done - Created %d keys.", siKeysCreated);
		keyCount += siKeysCreated;

		eaDestroyEString(&ppKeys);
	}
	loadend_printf("Done - Created %d keys total.", keyCount);

	stashTableDestroy(keyTable);
	linkRemove(&link);

	return 0;
}

// Connect to primary Account Server to begin creating product keys.
void initializeProductKeyCreation(void)
{
	NetLink *link;
	Packet *pak;

	link = openAccountServerLink();
	if (!link)
		return;

	sbReceivedResponse = false;
	pak = pktCreate(link, TO_ACCOUNTSERVER_GET_PKGROUPS);
	pktSend(&pak);
	waitForResponse();

	linkRemove(&link);
}

unsigned int productKeysBatchSize(U32 uBatchID)
{
	ProductKeyBatch *batch = getKeyBatchByID(uBatchID);
	if (!batch)
		return 0;

	if (isAccountServerMode(ASM_KeyGenerating))
	{
		return batch->uBatchKeyCount;
	}
	else
	{
		devassert(estrLength(&batch->pKeys) % PRODUCT_KEY_SIZE == 0);
		return estrLength(&batch->pKeys)/PRODUCT_KEY_SIZE;
	}
}

unsigned int productKeysBatchSizeNew(U32 uBatchID)
{
	return productKeysBatchSize(uBatchID) - productKeysBatchSizeUsed(uBatchID);
}

unsigned int productKeysBatchSizeUsed(U32 uBatchID)
{
	ProductKeyBatch *batch;

	PERFINFO_AUTO_START_FUNC();

	batch = getKeyBatchByID(uBatchID);
	if (!batch)
		return 0;

	PERFINFO_AUTO_STOP_FUNC();

	return batch->uUsedKeys;
}

// eaDestroy result -- DO NOT FREE CONTENTS
EARRAY_OF(ProductKey) productKeysGetBatchKeysNew(U32 uBatchID)
{
	EARRAY_OF(ProductKey) keys = NULL;
	ProductKeyBatch *batch;
	int i;

	PERFINFO_AUTO_START_FUNC();

	eaCreate(&keys);
	batch = getKeyBatchByID(uBatchID);
	if (!batch)
		return 0;

	devassert(estrLength(&batch->pKeys) % PRODUCT_KEY_SIZE == 0);
	for (i = 0; i < (int)estrLength(&batch->pKeys); i += PRODUCT_KEY_SIZE)
	{
		KeyLookasideInfo *info;
		int index;
		ProductKey *key = StructCreate(parse_ProductKey);
		bool success = stashFindInt(stKeyLookaside, batch->pKeys + i, &index);
		if (!devassert(success))
			continue;
		info = pKeyLookaside + index;
		key->uBatchId = info->uBatchId;
		key->uKeyId = info->uKeyId;
		if (!info->uAccountId)
			eaPush(&keys, key);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return keys;
}

// eaDestroy result -- DO NOT FREE CONTENTS
EARRAY_OF(ProductKey) productKeysGetBatchKeysUsed(U32 uBatchID)
{
	EARRAY_OF(ProductKey) keys = NULL;
	ProductKeyBatch *batch;
	int i;

	PERFINFO_AUTO_START_FUNC();

	eaCreate(&keys);
	batch = getKeyBatchByID(uBatchID);
	if (!batch)
		return 0;

	devassert(estrLength(&batch->pKeys) % PRODUCT_KEY_SIZE == 0);
	for (i = 0; i < (int)estrLength(&batch->pKeys); i += PRODUCT_KEY_SIZE)
	{
		KeyLookasideInfo *info;
		int index;
		ProductKey *key = StructCreate(parse_ProductKey);
		bool success = stashFindInt(stKeyLookaside, batch->pKeys + i, &index);
		if (!devassert(success))
			continue;
		info = pKeyLookaside + index;
		key->uBatchId = info->uBatchId;
		key->uKeyId = info->uKeyId;
		if (info->uAccountId)
			eaPush(&keys, key);
	}

	PERFINFO_AUTO_STOP_FUNC();
	return keys;
}

// Dump all unused keys that match a given prefix to a given file
AUTO_COMMAND;
void dumpUnusedProductKeys(SA_PARAM_NN_STR const char *pPrefix, SA_PARAM_NN_STR const char *pFileName)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	FILE *pFile = fopen(pFileName, "wt");
	unsigned int numKeys = 0;
	char pKeyPrefix[PRODUCT_KEY_PREFIX_SIZE + 1];
	ProductKeyGroup *group;

	// Get key group.
	getPaddedPrefix(pKeyPrefix, pPrefix);
	group = findKeyGroupFromString(pKeyPrefix);
	if (!group)
	{
		printf("No key groups match %s\n", pPrefix);
		return;
	}

	if (!pFile)
	{
		printf("Could not open file for writing: '%s'\n", pFileName);
		return;
	}

	printf("Dumping keys to '%s' that have the prefix '%s'...\n", pFileName, pKeyPrefix);

	// Find an undistributed batch in the key group.
	EARRAY_INT_CONST_FOREACH_BEGIN(group->keyBatches, i, size);
	{
		U32 batchId = group->keyBatches[i];
		EARRAY_OF(ProductKey) keys = productKeysGetBatchKeysNew(batchId);
		EARRAY_CONST_FOREACH_BEGIN(keys, j, m);
		{
			char *name = NULL;
			estrStackCreate(&name);
			copyProductKeyName(&name, keys[j]);
			fprintf(pFile, "%s\n", name);
			estrDestroy(&name);
			numKeys++;
		}
		EARRAY_FOREACH_END;
		eaDestroy(&keys);
	}
	EARRAY_FOREACH_END;

	printf("Dumped %d keys to the file.\n", numKeys);

	if (!numKeys) printf("Perhaps you gave an invalid prefix?\n");

	fclose(pFile);
}

AUTO_COMMAND ACMD_I_AM_THE_ERROR_FUNCTION_FOR(dumpUnusedProductKeys);
void dumpUnusedProductKeysError(CmdContext *pContext)
{
	printf("Format: dumpUnusedProductKeys <prefix> \"<file>\"\n");
}

// Register product key schemas
void registerProductKeySchemas(void)
{
	objRegisterNativeSchema(GLOBALTYPE_PRODUCTKEY_USED, parse_LegacyProductKey, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_PRODUCTKEY_NEW, parse_LegacyProductKey, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, parse_ProductKeyGroup, NULL, NULL, NULL, NULL, NULL);
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, parse_ProductKeyBatch, NULL, NULL, NULL, NULL, NULL);
}

// Get the number of days granted by a single activation key, -1 for error
int productKeyDaysGranted(SA_PARAM_NN_STR const char *pKey)
{
	ProductContainer **ppProducts = NULL;
	int numDays = 0;

	findProductsFromKey(pKey, &ppProducts);

	if (!ppProducts) return -1;

	EARRAY_CONST_FOREACH_BEGIN(ppProducts, i, size);
		const ProductContainer *pProduct = ppProducts[i];

		// Make sure the product isn't NULL
		if (!pProduct)
			continue;

		// Don't add the days if it's really for an internal sub
		if (pProduct->pInternalSubGranted && *pProduct->pInternalSubGranted)
			continue;

		numDays += pProduct->uDaysGranted;
	EARRAY_FOREACH_END;

	eaDestroyStruct(&ppProducts, parse_ProductContainer);

	return numDays;
}

static void migrateLegacyProductKeys(void);

// Scan product keys.
void scanProductKeys()
{
	U32 uTotalKeys = 0;
	PERFINFO_AUTO_START_FUNC();

	// Perform migration if necessary.
	migrateLegacyProductKeys();

	loadstart_printf("Scanning product keys... ");

	// Count product keys.
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, batchContainer);
	{
		ProductKeyBatch *batch = (ProductKeyBatch *)batchContainer->containerData;
		int batchSize = estrLength(&batch->pKeys);
		devassert(!(batchSize % PRODUCT_KEY_SIZE));
		uTotalKeys += batchSize/PRODUCT_KEY_SIZE;
		uMaxBatchId = MAX(uMaxBatchId, batch->uID);
	}
	CONTAINER_FOREACH_END;

	// Reserve look-aside table.
	beaSetCapacityStruct(&pKeyLookaside, parse_KeyLookasideInfo, uTotalKeys);
	stKeyLookaside = stashTableCreate(uTotalKeys, StashCaseInsensitive, StashKeyTypeFixedSize, PRODUCT_KEY_SIZE);

	// Scan all containers.
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, batchContainer);
	{
		ProductKeyBatch *batch = (ProductKeyBatch *)batchContainer->containerData;
		int count = estrLength(&batch->pKeys)/PRODUCT_KEY_SIZE;
		int i;

		assert(batch->uKeySize == PRODUCT_KEY_SIZE);
		devassert(estrLength(&batch->pKeys) % PRODUCT_KEY_SIZE == 0);

		for (i = 0; i != count; ++i)
		{
			KeyLookasideInfo *info = beaPushEmptyStruct(&pKeyLookaside, parse_KeyLookasideInfo);
			bool success;
			info->uBatchId = batch->uID;
			info->uKeyId = i;
			success = stashAddInt(stKeyLookaside, batch->pKeys + i * PRODUCT_KEY_SIZE, info - pKeyLookaside, false);
			devassert(success);
		}

		// Scan invalid keys.
		EARRAY_INT_CONST_FOREACH_BEGIN(batch->pInvalidKeys, j, m);
		{
			U32 key = batch->pInvalidKeys[j];
			U32 index;
			bool success;
			KeyLookasideInfo *info;

			if (!devassertmsgf(key < (U32)count, "Batch %lu has an invalid key entry %d for %lu that does not exist.", batch->uID, j, key))
				continue;

			success = stashFindInt(stKeyLookaside, batch->pKeys + key * PRODUCT_KEY_SIZE, &index);
			devassert(success);
			info = pKeyLookaside + index;
			info->uAccountId = (U32)-1;
		}
		EARRAY_FOREACH_END;

		// If -ResetDistributedKeyHint was specified, reset all key hints.
		if (bResetDistributedKeyHint && batch->uNextUndistributedKey)
		{
			AutoTrans_trResetDistributedKeyHint(astrRequireSuccess("trResetDistributedKeyHint"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH,
				batch->uID);
		}
	}
	CONTAINER_FOREACH_END;

	// Scan all accounts for key information.
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
	{
		AccountInfo *account = (AccountInfo *)accountContainer->containerData;

		// Scan activated keys.
		rescan:
		EARRAY_CONST_FOREACH_BEGIN(account->ppProductKeys, i, n);
		{
			KeyLookasideInfo *info = GetLookasideByName(account->ppProductKeys[i]);
			if (!info)
			{
				log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Key %s on account %lu does not exist; removing it.", account->ppProductKeys[i], account->uID);
				AccountRemoveProductKey(account->uID, account->ppProductKeys[i]);
				goto rescan;
			}
			if (info->uAccountId)
				ErrorOrAlert("ACCOUNT_SERVER_DUPLICATE_ACTIVATION", "Key %s activated by both account %lu and account %lu", account->ppProductKeys[i],
					account->uID, info->uAccountId);
			info->uAccountId = account->uID;
		}
		EARRAY_FOREACH_END;

		// Scan distributed keys.
		EARRAY_CONST_FOREACH_BEGIN(account->ppDistributedKeys, i, n);
		{
			KeyLookasideInfo *info = GetLookasideByName(account->ppDistributedKeys[i]->pActivationKey);
			if (!devassert(info))
				continue;
			if (info->uDistributedAccountId)
				ErrorOrAlert("ACCOUNT_SERVER_DUPLICATE_DISTRIBUTION", "Key %s distributed to both account %lu and account %lu",
					account->ppDistributedKeys[i]->pActivationKey, account->uID, info->uDistributedAccountId);
			info->uDistributedAccountId = account->uID;
		}
		EARRAY_FOREACH_END;
	}
	CONTAINER_FOREACH_END;

	loadend_printf("done.");
	PERFINFO_AUTO_STOP_FUNC();
}

// Migrate a list of keys to a batch.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Unextundistributedkey");
enumTransactionOutcome trResetDistributedKeyHint(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch)
{
	pBatch->uNextUndistributedKey = 0;
	return TRANSACTION_OUTCOME_SUCCESS;
}

/************************************************************************/
/* Migration                                                            */
/************************************************************************/

// Crash on a migration error.
static void migrationError(const char *pErrorFormat, ...)
{
	char *pText = NULL;
	estrStackCreate(&pText);
	estrGetVarArgs(&pText, pErrorFormat);
	assertmsg(gbAllowMigrationErrors, pText);
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Migration Error: %s", pText);
	estrDestroy(&pText);
}

// Migrate the product keys themselves.
static void migrateKeys(GlobalType containerType)
{
	ContainerIterator iter = {0};
	Container *currCon = NULL;
	char **keys = NULL;
	U32 *used = NULL;

	// Make lists of keys for all batches.
	objInitContainerIteratorFromType(containerType, &iter);
	for (currCon = objGetNextContainerFromIterator(&iter); currCon; currCon = objGetNextContainerFromIterator(&iter))
	{
		LegacyProductKey *pKey = (LegacyProductKey *)currCon->containerData;
		ProductKeyBatch *pBatch;
		ProductKeyGroup *pGroup;

		// Make sure the key has a batch.
		if (!pKey->batchID)
		{
			migrationError("Key %s lacks a batch.", pKey->pKey);
			continue;
		}

		// Make sure the batch that this key has is the right one.
		pBatch = getKeyBatchByID(pKey->batchID);
		if (!devassert(pBatch))
			continue;
		pGroup = keyGroupFromBatch(pBatch);
		if (!devassert(pGroup))
			continue;
		if (strnicmp(pGroup->productPrefix, pKey->pKey, PRODUCT_KEY_PREFIX_SIZE))
		{
			migrationError("Key %s does not match the prefix of batch %lu.", pKey->pKey, pKey->batchID);
			continue;
		}
		// Add this key to the batch key list.
		if (eaSize(&keys) <= pKey->batchID)
			eaSetSize(&keys, pKey->batchID + 1);
		devassert(keys);
		if (!pKey->pKey || strlen(pKey->pKey) != PRODUCT_KEY_SIZE)
		{
			migrationError("Key %s in batch %lu not acceptable length.", pKey->pKey, pKey->batchID);
			continue;
		}

		// Check if the key is locked.
		if (pKey->uLockTime)
			migrationError("Key %s was locked at %lu.", pKey->pKey, pKey->uLockTime);

		if (pKey->uAccountID && containerType == GLOBALTYPE_PRODUCTKEY_NEW)
			migrationError("Unused key %s claims to have been activated by account %lu.", pKey->pKey, pKey->uAccountID);

		// Make sure the account information on this key matches.
		if (pKey->uAccountID)
		{
			AccountInfo *account = findAccountByID(pKey->uAccountID);
			bool found = false;

			if (account)
			{
				// Verify that key is on account.
				EARRAY_CONST_FOREACH_BEGIN(account->ppProductKeys, i, n);
				{
					if (!stricmp_safe(account->ppProductKeys[i], pKey->pKey))
					{
						found = true;
						break;
					}
				}
				EARRAY_FOREACH_END;
				if (!found)
					migrationError("Key %s claims to be activated by account %lu but is not present in that account's key list.",
						pKey->pKey, pKey->uAccountID);
	
				// Count key as used.
				if (ea32Size(&used) <= pKey->batchID)
					ea32SetSize(&used, pKey->batchID + 1);
				devassert(used);
				++used[pKey->batchID];
			}
			else
				migrationError("Key %s was activated by account %lu which doesn't seem to exist.", pKey->pKey, pKey->uAccountID);
		}

		// Make sure the distribution information on this key matches.
		if (pKey->uDistributedAccountID)
		{
			AccountInfo *account = findAccountByID(pKey->uDistributedAccountID);
			bool found = false;
			EARRAY_CONST_FOREACH_BEGIN(account->ppDistributedKeys, i, n);
			{
				if (!stricmp_safe(account->ppDistributedKeys[i]->pActivationKey, pKey->pKey))
				{
					found = true;
					break;
				}
			}
			EARRAY_FOREACH_END;
			if (!found)
				migrationError("Key %s claims to be distributed to account %lu but is not present in that account's distributed key list.",
				pKey->pKey, pKey->uDistributedAccountID);
		}

		estrAppend2(&keys[pKey->batchID], pKey->pKey);
	}
	objClearContainerIterator(&iter);

	// Add these key lists to each batch.
	EARRAY_CONST_FOREACH_BEGIN(keys, i, n);
	{
		if (estrLength(&keys[i]))
		{
			AutoTrans_trMigrateKeyBatch(astrRequireSuccess(NULL), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, i, keys[i],
				i < ea32Size(&used) ? used[i] : 0);
		}
	}
	EARRAY_FOREACH_END;

	eaDestroy(&keys);
	ea32Destroy(&used);
}

// Migrate from legacy product keys to key batch containers.
static void migrateLegacyProductKeys()
{
	char *legacyProductDb = NULL;

	PERFINFO_AUTO_START_FUNC();

	// Return if no migration needs to be done.
	estrStackCreate(&legacyProductDb);
	estrPrintf(&legacyProductDb, "%s%s", dbAccountDataDir(), sKeyFile);
	if (!fileExists(legacyProductDb))
	{
		estrDestroy(&legacyProductDb);
		PERFINFO_AUTO_STOP_FUNC();
		return;
	}

	// Load legacy product keys database.
	loadLegacyProductKeys();

	// Make sure migration has not been done yet.
	if (objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_KEYGROUP)
		|| objCountTotalContainersWithType(GLOBALTYPE_ACCOUNTSERVER_KEYBATCH))
		migrationError("Migration failure: both legacy and new product key databases present");

	loadstart_printf("Performing one-time migration of product keys...\n");
	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Beginning product key migration.");

	// Run pending transactions.
	UpdateObjectTransactionManager();
	UpdateObjectTransactionManager();

	// Migrate all product key groups and product key batches.
	EARRAY_CONST_FOREACH_BEGIN(sProductKeys.ppKeyGroups, i, n);
	{
		LegacyProductKeyGroup *oldGroup = sProductKeys.ppKeyGroups[i];
		NOCONST(ProductKeyGroup) newGroup = {0};
		const char *productPrefix = NULL;
		char **ppProducts = NULL;
		TransactionReturnVal result;
		ContainerID id;

		// Get prefix and activated products from the batches.
		EARRAY_CONST_FOREACH_BEGIN(oldGroup->keyBatches, j, m);
		{
			LegacyProductKeyBatch *batch = oldGroup->keyBatches[j];
			if (productPrefix)
			{
				char **ppProductsVerify = NULL;
				if (strcmp(productPrefix, batch->productPrefix))
					migrationError("Product group prefix conflict: \"%s\" vs \"%s\"", productPrefix, batch->productPrefix);
				eaCopyEStrings(&batch->ppProducts, &ppProductsVerify);
				eaQSort(ppProductsVerify, strCmp);
				if (!ppProductsVerify)
				{
					migrationError("Batch %s has no products to verify.", batch->pBatchName);
					continue;
				}
				if (eaSize(&batch->ppProducts) != eaSize(&ppProductsVerify))
				{
					migrationError("Size verification failed for batch %s.", batch->pBatchName);
					continue;
				}
				EARRAY_CONST_FOREACH_BEGIN(batch->ppProducts, k, o);
				{
					if (strcmp(batch->ppProducts[k], ppProductsVerify[k]))
						migrationError("Verification for batch %s failed: %s != %s", batch->pBatchName, batch->ppProducts[k], ppProductsVerify[k]);
				}
				EARRAY_FOREACH_END;
				eaDestroyEString(&ppProductsVerify);
			}
			else
			{
				assert(batch->productPrefix);
				productPrefix = batch->productPrefix;
				if (!eaSize(&batch->ppProducts))
					migrationError("Key batch %s does not activate any products.", batch->pBatchName);
				eaCopyEStrings(&batch->ppProducts, &ppProducts);
				eaQSort(ppProducts, strCmp);
			}
		}
		EARRAY_FOREACH_END;

		// Do not migrate empty groups.
		if (!productPrefix || !*productPrefix)
		{
			migrationError("Key group %d (%s) is empty: deleting it.", i, oldGroup->productPrefix);
			continue;
		}

		// Create key group container.
		StructInitNoConst(parse_ProductKeyGroup, &newGroup);
		if (productPrefix)
			strcpy(newGroup.productPrefix, productPrefix);
		else
			newGroup.productPrefix[0] = 0;
		newGroup.ppProducts = ppProducts;
		objRequestContainerCreateLocal(&result, GLOBALTYPE_ACCOUNTSERVER_KEYGROUP, &newGroup);
		while (result.eOutcome == TRANSACTION_OUTCOME_NONE)
			UpdateObjectTransactionManager();
		id = GetNewContainerId(&result);
		ReleaseReturnValData(objLocalManager(), &result);
		assert(id);

		// Migrate all batches for this key group.
		EARRAY_CONST_FOREACH_BEGIN(oldGroup->keyBatches, j, m);
		{
			LegacyProductKeyBatch *oldBatch = oldGroup->keyBatches[j];
			NOCONST(ProductKeyBatch) newBatch = {0};
			ContainerID batchId;

			// Copy key batch data.
			newBatch.uID = oldBatch->uID;
			newBatch.timeCreated = oldBatch->timeCreated;
			newBatch.timeDownloaded = oldBatch->timeDownloaded;
			newBatch.uKeySize = PRODUCT_KEY_SIZE;
			newBatch.pBatchName = oldBatch->pBatchName;
			newBatch.pBatchDescription = oldBatch->pBatchDescription;
			newBatch.batchInvalidated = oldBatch->batchInvalidated;
			newBatch.batchDistributed = oldBatch->batchDistributed;

			// Create key batch container.
			objRequestContainerCreateLocal(&result, GLOBALTYPE_ACCOUNTSERVER_KEYBATCH, &newBatch);
			while (result.eOutcome == TRANSACTION_OUTCOME_NONE)
				UpdateObjectTransactionManager();
			batchId = GetNewContainerId(&result);
			ReleaseReturnValData(objLocalManager(), &result);
			assert(batchId);

			// Save batch ID in group.
			AutoTrans_trAddBatchToKeyGroup(astrRequireSuccess("Migrate add batch"), objServerType(), GLOBALTYPE_ACCOUNTSERVER_KEYGROUP,
				id, batchId);
		}
		EARRAY_FOREACH_END;
	}
	EARRAY_FOREACH_END;

	// Migrate all keys.
	migrateKeys(GLOBALTYPE_PRODUCTKEY_USED);
	migrateKeys(GLOBALTYPE_PRODUCTKEY_NEW);

	// Make sure key information on accounts is consistent with product keys.
	CONTAINER_FOREACH_BEGIN(GLOBALTYPE_ACCOUNT, accountContainer);
	{
		AccountInfo *account = (AccountInfo *) accountContainer->containerData;

		// Scan activated keys.
		EARRAY_CONST_FOREACH_BEGIN(account->ppProductKeys, i, n);
		{
			KeyLookasideInfo *info = GetLookasideByName(account->ppProductKeys[i]);
			if (!info)
			{
				migrationError("Account %lu has a key %s that does not seem to exist.", account->uID, account->ppProductKeys[i]);
				continue;
			}
			if (info->uAccountId != account->uID)
				migrationError("Account %lu has a key %s that is actually associated with account %lu", account->uID, account->ppProductKeys[i],
					info->uAccountId);
		}
		EARRAY_FOREACH_END;

		// Scan distributed keys.
		EARRAY_CONST_FOREACH_BEGIN(account->ppDistributedKeys, i, n);
		{
			DistributedKeyContainer *distributed = account->ppDistributedKeys[i];
			KeyLookasideInfo *info = GetLookasideByName(distributed->pActivationKey);
			if (!info)
			{
				migrationError("Account %lu has a key %s that does not seem to exist.", account->uID, distributed->pActivationKey);
				continue;
			}
			if (info->uDistributedAccountId != account->uID)
				migrationError("Account %lu was distributed a key %s that is actually associated with account %lu", account->uID,
					distributed->pActivationKey, info->uAccountId);
		}
		EARRAY_FOREACH_END;
	}
	CONTAINER_FOREACH_END;

	// Destroy legacy keys.
	if (!gbDisableKeyMigrationContainerRemoval && !gbDeferLegacyKeysDelete)
	{
		objRemoveAllContainersWithType(GLOBALTYPE_PRODUCTKEY_USED);
		objRemoveAllContainersWithType(GLOBALTYPE_PRODUCTKEY_NEW);
	}

	// Write out changes.
	objForceRotateIncrementalHog();

	// Back up old product keys.
	fileRenameToBak(legacyProductDb);

	log_printf(LOG_ACCOUNT_SERVER_GENERAL, "Finished product key migration.");
	loadend_printf("Performing one-time migration of product keys... done.");
	estrDestroy(&legacyProductDb);
	PERFINFO_AUTO_STOP_FUNC();
}

// Migrate a list of keys to a batch.
AUTO_TRANSACTION
ATR_LOCKS(pBatch, ".Uusedkeys, .Pbatchname, .Pkeys");
enumTransactionOutcome trMigrateKeyBatch(ATR_ARGS, NOCONST(ProductKeyBatch) *pBatch, const char *pKeyList, U32 uUsed)
{
	if (!pKeyList || !*pKeyList || strlen(pKeyList) % PRODUCT_KEY_SIZE)
	{
		migrationError("Key string in batch %s not acceptable length.", pBatch->pBatchName);
		return TRANSACTION_OUTCOME_FAILURE;
	}
	estrAppend2(&pBatch->pKeys, pKeyList);
	pBatch->uUsedKeys += uUsed;
	return TRANSACTION_OUTCOME_SUCCESS;
}

// Perform background product key tasks.
void productKeyTick()
{
	static bool usedDone = false, newDone = false;
	if (gbDeferLegacyKeysDelete && !gbDisableKeyMigrationContainerRemoval)
	{
		unsigned i = 0;
		ContainerIterator iter = {0};
		Container *currCon = NULL;

		PERFINFO_AUTO_START_FUNC();
		coarseTimerAddInstance(NULL, __FUNCTION__);

		// Delete used keys first.
		if (!usedDone)
		{
			objInitContainerIteratorFromType(GLOBALTYPE_PRODUCTKEY_USED, &iter);
			currCon = objGetNextContainerFromIterator(&iter);
			while (currCon && i < gbDeleteLegacyKeysPerFrame)
			{
				LegacyProductKey *pKey = (LegacyProductKey *)currCon->containerData;
				objRequestContainerDestroyLocal(astrRequireSuccess("Delete legacy used key"), GLOBALTYPE_PRODUCTKEY_USED, pKey->containerID);
				--i;
				currCon = objGetNextContainerFromIterator(&iter);
			}
			objClearContainerIterator(&iter);
			if (i < gbDeleteLegacyKeysPerFrame)
				usedDone = true;
		}

		// Delete new keys if there are no used keys to delete.
		if (!newDone)
		{
			objInitContainerIteratorFromType(GLOBALTYPE_PRODUCTKEY_NEW, &iter);
			currCon = objGetNextContainerFromIterator(&iter);
			while (currCon && i < gbDeleteLegacyKeysPerFrame)
			{
				LegacyProductKey *pKey = (LegacyProductKey *)currCon->containerData;
				objRequestContainerDestroyLocal(astrRequireSuccess("Delete legacy new key"), GLOBALTYPE_PRODUCTKEY_NEW, pKey->containerID);
				currCon = objGetNextContainerFromIterator(&iter);
				--i;
			}
			objClearContainerIterator(&iter);
			if (i < gbDeleteLegacyKeysPerFrame)
			{
				newDone = true;
				gbDeferLegacyKeysDelete = false;
			}
		}

		coarseTimerStopInstance(NULL, __FUNCTION__);
		PERFINFO_AUTO_STOP_FUNC();
	}
}

#include "ProductKey_h_ast.c"
#include "ProductKey_c_ast.c"
