#pragma once

// ONE TIME FIXUP - Zen conversion
// Each of these structs specifies a pair of keys
// For each account, any balance in one key is moved into the other while being multiplied by 1.25
AUTO_STRUCT;
typedef struct ZenKeyConversion
{
	char *pOldKey; // Key from which to convert
	char *pNewKey; // Key into which to convert
	char *pAccountList; // If specified, apply key change only to accounts in the text file at the specified location on disk
} ZenKeyConversion;

// Stores config settings for general Account Server features that you don't feel deserve their own config file
AUTO_STRUCT;
typedef struct AccountServerConfig
{
	// django WebSrv address; Also used for AccountProxy.c:HandleRequestWebSrvGameEvent
	char *pWebSrvAddress;
	int iWebSrvPort;
	// Drupal site URL used for sending One-Time Code emails, only used if WebSrv is not set up
	char *pOneTimeCodeEmailURL;

	U32 uSavedClientMachinesMax; AST(DEFAULT(10)) // Number of saved CrypticClient machine IDs each user can have,
	U32 uSavedBrowserMachinesMax; AST(DEFAULT(10)) // Number of saved WebBrowser machine IDs each user can have, 

	U32 uOneTimeCodeDuration; AST(DEFAULT(7200)) // Number of seconds a One-Time Code is valid for after it was generated, default = 2 hours
	U32 uOneTimeCodeAttempts; AST(DEFAULT(3)) // Number of attempts per one-time code for a machine ID
	U32 uSaveNextMachineDuration; AST(DEFAULT(5)) // Number of days the "Save Next Machine" is valid for

	U32 uMachineLockGracePeriod; // Number of days a newly created account has to log in for the first Cryptic Client and Web Browser login to auto-save the machine ID

	STRING_EARRAY eaCurrencyKeys; AST(ESTRING) // List of key-values that are currencies, for transaction logging

	EARRAY_OF(ZenKeyConversion) eaZenKeyConversions; AST(NAME(ZenKeyConversion)) // ONE TIME FIXUP - List of key conversions for Zen conversion
} AccountServerConfig;

SA_RET_NN_VALID AccountServerConfig *GetAccountServerConfig(void);
void LoadAccountServerConfig(void);

// Accessors
CONST_STRING_EARRAY GetCurrencyKeys(void);
CONST_EARRAY_OF(ZenKeyConversion) GetZenKeyConversions(void);