#pragma once

typedef struct AccountInfo AccountInfo;
typedef struct Container Container;
typedef enum AccountCreationResult AccountCreationResult;

AST_PREFIX( PERSIST )
AUTO_STRUCT AST_CONTAINER;
typedef struct PWCommonAccount
{
	CONST_STRING_MODIFIABLE pAccountName; AST(KEY)
	CONST_STRING_MODIFIABLE pForumName;
	CONST_STRING_MODIFIABLE pEmail;
	CONST_STRING_MODIFIABLE pPasswordHash_Obsolete; AST(NAME(pPasswordHash))
	char *pPasswordHash_RAM; NO_AST
	char *pPasswordHashFixedSalt_RAM; NO_AST
	CONST_STRING_MODIFIABLE pPasswordHash_Disk;
	CONST_STRING_MODIFIABLE pPasswordHashFixedSalt_Disk;
	CONST_STRING_MODIFIABLE pFixedSalt;
	const bool bPasswordHashIsFromCryptic;
	const int iEncryptionVersion;
	
	const bool bBanned;

	const U32 uLinkedID; // Linked Cryptic AccountID
	const U32 uBatchID; // Reverse lookup to Batch container
} PWCommonAccount;

AUTO_STRUCT AST_CONTAINER;
typedef struct PerfectWorldAccountBatch
{
	const U32 uBatchID; AST(KEY)
	CONST_EARRAY_OF(PWCommonAccount) eaAccounts;

	AST_COMMAND("Apply Transaction","ServerMonTransactionOnEntity Perfectworldaccountbatch $FIELD(UBatchID) $STRING(Transaction String)")
} PerfectWorldAccountBatch;	
AST_PREFIX();

typedef enum PerfectWorldUpdateResult
{
	PWUPDATE_Error = 0,
	PWUPDATE_Updated,
	PWUPDATE_Created,
	PWUPDATE_Useless,
	PWUPDATE_Max
} PerfectWorldUpdateResult;

typedef enum AccountIntegrationResult
{
	ACCOUNT_INTEGRATION_Error = -1,
	ACCOUNT_INTEGRATION_Success = 0,
	ACCOUNT_INTEGRATION_EmailConflict,
	ACCOUNT_INTEGRATION_DisplayNameConflict,
	ACCOUNT_INTEGRATION_DisplayNameInvalid,
	ACCOUNT_INTEGRATION_PWAlreadyLinked,
	ACCOUNT_INTEGRATION_CrypticAlreadyLinked,
	ACCOUNT_INTEGRATION_NotLinked,
	ACCOUNT_INTEGRATION_PWBanned,
} AccountIntegrationResult;

// Returns the corresponding XML-RPC string result for an AccountIntegrationResult
const char *getAccountIntegrationResultString(AccountIntegrationResult eResult);

PWCommonAccount * findPWCommonAccountbyName(const char *pAccountName);
PWCommonAccount * findPWCommonAccountbyForumName(const char *pForumName);
PWCommonAccount * findPWCommonAccountByEmail(const char *pEmail);
PWCommonAccount * findPWCommonAccountByLoginField(const char *pLoginField);

bool confirmPerfectWorldLoginPassword(PWCommonAccount * pAccount, const char *pPassword,
	const char * pPasswordFixedSalt, const char * pFixedSalt, U32 salt, bool bEmailLogin);
void AccountIntegration_ClearPWAutoCreatedFlag(AccountInfo *account);

PerfectWorldAccountBatch *PerfectWorldAccountBatch_FindByID(U32 iID);

AccountIntegrationResult AccountIntegration_LinkAccountToPWCommon(SA_PARAM_NN_VALID AccountInfo *pAccount, SA_PARAM_NN_VALID PWCommonAccount *pCommon);
AccountIntegrationResult AccountIntegration_UnlinkAccountFromPWCommon(SA_PARAM_NN_VALID AccountInfo *pAccount);
AccountIntegrationResult AccountIntegration_CreateAccountFromPWCommon(
	SA_PARAM_NN_VALID PWCommonAccount *pCommon,
	SA_PARAM_OP_STR const char *pOverrideDisplayName,
	SA_PARAM_OP_OP_VALID AccountInfo **ppAccount,
	SA_PARAM_NN_VALID AccountCreationResult *peCreateResult);
PerfectWorldUpdateResult AccountIntegration_CreateOrUpdatePWCommon(
	SA_PARAM_NN_STR const char *pAccountName,
	SA_PARAM_OP_STR const char *pForumName,
	SA_PARAM_NN_STR const char *pEmail,
	SA_PARAM_OP_STR const char *pPasswordHash,
	SA_PARAM_OP_STR const char *pPasswordHashFixedSalt,
	SA_PARAM_OP_STR const char *pFixedSalt,
	bool bBanned);

// Called before DB load
void initializeAccountIntegration(void);

// Calls for resetting DB for unit testing
void AccountIntegration_CreatePWFromStruct(PWCommonAccount *pAccount);
void AccountIntegration_DestroyContainers(void);

// Perform periodic Account Integration processing.
void AccountIntegration_Tick(void);

// Create a conflict ticket for an account.
U32 createConflictTicket(SA_PARAM_NN_VALID PWCommonAccount *pAccount);

// Find a conflict ticket
bool findConflictTicketByID(SA_PARAM_NN_VALID PWCommonAccount *pAccount, U32 uTicketID);

// Cryptic Account conflict tickets
U32 createCrypticConflictTicket(SA_PARAM_NN_VALID AccountInfo *pAccount);
bool findCrypticConflictTicketByID(SA_PARAM_NN_VALID AccountInfo *pAccount, U32 uTicketID);

// Stat functions
int AccountIntegration_GetHourlyUsefulUpdateCount(void);
int AccountIntegration_GetHourlyUselessUpdateCount(void);
int AccountIntegration_GetHourlyFailedUpdateCount(void);
void AccountIntegration_ResetHourlyUpdateCounts(void);

void AccountIntegration_PWCommonAccountDecryptPassword(SA_PARAM_NN_VALID PWCommonAccount *pAccount);
static __forceinline void AccountIntegration_PWCommonAccountDecryptPasswordIfNecessary(SA_PARAM_NN_VALID PWCommonAccount *pAccount)
{
	if ((!pAccount->pPasswordHash_RAM || !pAccount->pPasswordHash_RAM[0]) &&
		(!pAccount->pPasswordHashFixedSalt_RAM || !pAccount->pPasswordHashFixedSalt_RAM[0]))
	{
		AccountIntegration_PWCommonAccountDecryptPassword(pAccount);
	}
}

bool AccountIntegration_PWCommonAccountNeedsEncryptionFixup(SA_PARAM_NN_VALID PWCommonAccount *pAccount);
bool AccountIntegration_PWCommonAccountDoEncryptionFixup(SA_PARAM_NN_VALID PWCommonAccount *pAccount);