#pragma once

/************************************************************************/
/* Key size constants                                                   */
/************************************************************************/

// WARNING: There is no support for changing these values.  Any changes will break all AccountDB compatibility.
// If we decide to implement support for different key sizes, all code that uses this constant will need to be
// audited for correctness.
#define PRODUCT_KEY_PREFIX_SIZE 5		// size of the actual string, not including terminator
#define PRODUCT_KEY_SIZE 25				// string length of product keys (does not include null terminator)

/************************************************************************/
/* Headers and forward declarations                                     */
/************************************************************************/

#include "ProductKey_h_ast.h"

typedef struct StashTableImp* StashTable;
typedef struct AccountInfo AccountInfo;
typedef struct AccountPermissionStruct AccountPermissionStruct;
typedef struct LockedKey LockedKey;

/************************************************************************/
/* Product Keys, Product Key Batches, and Product Key Groups            */
/************************************************************************/

// Information about a product key returned by makeProductKeyInfo().
AUTO_STRUCT;
typedef struct ProductKeyInfo
{
	char *pKey;
	U32 *eaProductIDs;
	bool bUsed;
} ProductKeyInfo;

// Product key handle (value semantics)
AUTO_STRUCT;
typedef struct ProductKey
{
	U32 uBatchId;
	U32 uKeyId;
} ProductKey;

// Batch of product keys, owned by a key group.
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct ProductKeyBatch
{
	const U32 uID;									AST(KEY)
	const U32 timeCreated;
	const U32 timeDownloaded;
	const U32 uKeySize;													// Presently, this must be PRODUCT_KEY_SIZE

	// Batch Keys
	CONST_STRING_MODIFIABLE pKeys;					AST(ESTRING)
	UINT_EARRAY pInvalidKeys;											// Sorted list of invalid key indices
	const U32 uUsedKeys;												// Tracks how many keys are used (redundant)
	const U32 uNextUndistributedKey;									// Index of next key that might be undistributed

	// Batch identifying info
	CONST_STRING_MODIFIABLE pBatchName;				AST(ESTRING)
	CONST_STRING_MODIFIABLE pBatchDescription;		AST(ESTRING)

	// Batch Keys
	char **ppBatchKeys;								AST_NOT(PERSIST)	// Only used to transfer keys during generation
	const U32 uBatchKeyCount;						AST_NOT(PERSIST)	// Only used to transfer keys during generation

	// Flags
	const U8 batchInvalidated : 1;
	const U8 batchDistributed : 1;										// has the batch been distributed yet?

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity ProductKeyBatch $FIELD(UID) $STRING(Transaction String)")
} ProductKeyBatch;
AST_PREFIX();

// Group of product key batches, associated with a specific list of products.
AST_PREFIX(PERSIST)
AUTO_STRUCT AST_CONTAINER;
typedef struct ProductKeyGroup
{
	const U32 uID;									AST(KEY)
	const char productPrefix[PRODUCT_KEY_PREFIX_SIZE+1];

	CONST_STRING_EARRAY ppProducts;					AST(ESTRING)		// List of products these keys activate

	CONST_CONTAINERID_EARRAY keyBatches;

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity ProductKeyGroup $FIELD(UID) $STRING(Transaction String)")
} ProductKeyGroup;
AST_PREFIX();

/************************************************************************/
/* Key Generation                                                       */
/************************************************************************/

// Key generation information
AUTO_STRUCT;
typedef struct ProductKeyGenerationData
{
	EARRAY_OF(ProductKeyGroup) groups;
	EARRAY_OF(ProductKeyBatch) batches;
} ProductKeyGenerationData;

/************************************************************************/
/* Product Key Loading and Creation                                     */
/************************************************************************/

// Adds a key group from a pre-populated struct
void productKeyGroupAddStruct(ProductKeyGroup *pKeyGroup);

// Create new key group.
bool createNewKeyGroup(SA_PARAM_NN_VALID const char * pProductPrefix, SA_PARAM_NN_VALID const char * pInternalProductNames);

// Set the list of product names for a key group.
bool setKeyGroupProductNames(U32 uKeyGroup, SA_PARAM_NN_VALID const char *pInternalProductNames);

// Outcome of a product key operation, such as activation.
AUTO_ENUM;
typedef enum ProductKeyResult
{
	PK_Invalid = 0,
	PK_Success,
	PK_PrefixInvalid,
	PK_KeyExists, 
	PK_KeyInvalid,
	PK_KeyUsed,
	PK_KeyBatchInvalid,
	PK_KeyDistributed,
	PK_CouldNotActivateProduct,
	PK_KeyLocked,
	PK_KeyNotLocked,
	PK_Pending,
	PK_InternalError,
	PK_CouldNotLockKeyValue,
	PK_CouldNotCommitKeyValue,
	PK_CouldNotRollbackKeyValue,
	PK_CouldNotDistributeKey,
	PK_CouldNotCreateInternalSub,
	PK_CouldNotAssociateProduct,
	PK_PrerequisiteFailure,
	PK_InvalidReferrer,
} ProductKeyResult;

// Callback for a single batch.
typedef void (*fpKeyBatchCallback)(SA_PARAM_NN_VALID const ProductKeyBatch *pBatch, SA_PARAM_OP_VALID void *pUserData);

/************************************************************************/
/* Key activation                                                       */
/************************************************************************/

// Activate a product key
typedef struct ActivateProductLock ActivateProductLock;
ProductKeyResult activateProductKey(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pReferrer);
ProductKeyResult activateProductKeyLock(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_OP_STR const char *pReferrer, SA_PARAM_NN_VALID ActivateProductLock **pOutLock);
ProductKeyResult activateProductKeyFinish(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID ActivateProductLock *pLock, bool bCommit);
#define activateProductKeyCommit(pAccount, pKey, pLock) activateProductKeyFinish(pAccount, pKey, pLock, true)
#define activateProductKeyRollback(pAccount, pKey, pLock) activateProductKeyFinish(pAccount, pKey, pLock, false)

/************************************************************************/
/* Key distribution                                                     */
/************************************************************************/

// Distribute a key to the specified user
ProductKeyResult distributeProductKeyFinish(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, U32 uLockTime, bool bCommit);
#define distributeProductKeyCommit(uAccountID, pKey, uLockTime) distributeProductKeyFinish(uAccountID, pKey, uLockTime, true)
#define distributeProductKeyRollback(uAccountID, pKey, uLockTime) distributeProductKeyFinish(uAccountID, pKey, uLockTime, false)
ProductKeyResult distributeProductKeyLock(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_STR const char *pKey, SA_PARAM_NN_VALID U32 *pLockTime);

// Mark a product key batch as distributed or not.
void productKeyBatchSetDistributed(const ProductKeyBatch *pBatch, bool bDistributed);

// Get the account ID that a key was distributed to.
U32 getDistributedAccountId(SA_PARAM_NN_VALID const ProductKey *pKey);

/************************************************************************/
/* Invalidation                                                         */
/************************************************************************/

// Look up a product key and check if it is valid.
bool productKeyIsValid(ProductKey *pKey, SA_PARAM_NN_STR const char *pKeyName);

// Get product key validity.  Warning: Do not use this to check if a product key is valid.  Only use it to check if a product key
// has specifically been marked as invalid.
bool productKeyGetInvalid(SA_PARAM_NN_STR const char *pKeyName);

// Mark product key validity.
bool productKeySetInvalid(SA_PARAM_NN_STR const char *pKeyName, bool bInvalid);

// Mark product key batch validity.
void productKeyBatchSetInvalid(const ProductKeyBatch *pKey, bool bInvalid);

/************************************************************************/
/* Locking                                                              */
/************************************************************************/

// Determine if a product key is locked
bool productKeyIsLocked(SA_PARAM_NN_VALID const ProductKey *pKey);

// Get the lock time for a product key.
U32 productKeyGetLockTime(SA_PARAM_NN_VALID const ProductKey *pKey);

// Unlock a product key.
ProductKeyResult unlockProductKey(SA_PARAM_NN_STR const char *pKey);

/************************************************************************/
/* Other                                                                */
/************************************************************************/

// Determine if a product key is distributed
bool productKeyIsDistributed(SA_PARAM_NN_VALID const ProductKey *pKey);

// Get the account ID that a key was activated on
U32 getActivatedAccountId(SA_PARAM_NN_VALID const ProductKey *pKey);

// Return a pointer to a possibly invalid product key.
// WARNING: If only valid product keys are desired, use productKeyIsValid() instead.
bool findProductKey(SA_PRE_NN_FREE SA_POST_NN_VALID ProductKey *pKey, SA_PARAM_NN_STR const char * pKeyName);

// Copy product key name to an EString.
void copyProductKeyName(SA_PRE_NN_NN_STR char **estrKeyName, SA_PARAM_NN_VALID const ProductKey *pKey);

// Find an undistributed product key
bool findUndistributedProductKey(SA_PRE_NN_FREE SA_POST_NN_VALID ProductKey *pKey, SA_PARAM_NN_STR const char * pPrefix);

// Make a product key info structure from a product key
SA_RET_OP_VALID ProductKeyInfo * makeProductKeyInfo(SA_PARAM_NN_VALID const ProductKey *pKey);

// Return NULL if a product key is not valid.
bool productKeyIsValidByPtr(SA_PARAM_OP_VALID ProductKey *pKey);

typedef struct ProductContainer ProductContainer;

// Get the list of products activated by a key.
void findProductsFromKey(SA_PARAM_NN_STR const char *pKey, ProductContainer *** pppProductsOut);

// Get the key group that uses a key prefix.
ProductKeyGroup *findKeyGroupFromString(const char * prefix);

// Get the key group that holds a batch.
ProductKeyGroup *keyGroupFromBatch(const ProductKeyBatch *batch);

ProductKeyBatch *getKeyBatchByID(U32 id);
SA_RET_OP_VALID const ProductKeyBatch *getKeyBatchByName(SA_PARAM_NN_STR const char *pName);

// Iterate over key batches.
void iterateKeyBatches(fpKeyBatchCallback fpCallback, void *pUserData);

// Get the maximum batch ID that currently exists.
int getMaxBatchId(void);

char * parseProductPrefix_s(char * dst, size_t size, const char * pKey);
#define parseProductPrefix(dst,src) parseProductPrefix_s(dst, ARRAY_SIZE_CHECKED(dst), src)

int productKeysCreateBatch(const char * pProductPrefix, int iNumberOfKeys, const char *batchName, const char *batchDescription);

// Handle key generating messages
void HandleMessageFromKeyGen(Packet* pak, int cmd, NetLink* link, AccountLink *accountLink);

// Mark a batch as downloaded.
void batchDownloaded(ProductKeyBatch *pKeyBatch);

// Add a key batch from a pre-populated struct
void productKeyBatchAddFromStruct(ProductKeyBatch *pKeyBatch);

// Add a block of product keys to a batch.
int productKeysAdd(ProductKeyBatch *batch);

// Connect to primary Account Server to begin creating product keys.
void initializeProductKeyCreation(void);

// Get array of product key groups.
CONST_EARRAY_OF(ProductKeyGroup) getProductKeyGroupList(void);

// Get product key generation data.
void getProductKeyGenerationData(ProductKeyGenerationData *pData);

char * generateActivationKey_s (char *buffer, size_t buffer_size);
#define generateActivationKey(buffer) generateActivationKey_s(buffer, ARRAY_SIZE_CHECKED(buffer))

#define PRODUCT_KEY_OK "product_key_ok"
#define PRODUCT_KEY_IN_USE "product_already_active"
#define PRODUCT_KEY_INVALID "invalid_productkey"

// Key batch size functions
unsigned int productKeysBatchSize(U32 uBatchID);
unsigned int productKeysBatchSizeNew(U32 uBatchID);
unsigned int productKeysBatchSizeUsed(U32 uBatchID);

// eaDestroyStruct result
EARRAY_OF(ProductKey) productKeysGetBatchKeysNew(U32 uBatchID);

// eaDestroyStruct result
EARRAY_OF(ProductKey) productKeysGetBatchKeysUsed(U32 uBatchID);

// Get the number of days granted by a single activation key, -1 for error
int productKeyDaysGranted(SA_PARAM_NN_STR const char *pKey);

// Compare two locked keys.
int keyLockCmp(const LockedKey **pptr1, const LockedKey **pptr2);

/************************************************************************/
/* Server Interface                                                     */
/************************************************************************/

// Register product key schemas
void registerProductKeySchemas(void);

// Scan product keys.
void scanProductKeys(void);

// Perform background product key tasks.
void productKeyTick(void);

void ProductKey_DestroyContainers(void);