#pragma once

// Now known as Account Guard
#define MACHINE_LOCKING_IPFILTER_GROUP "AccountGuardWhitelist"

AST_PREFIX( PERSIST );
AUTO_STRUCT AST_CONTAINER;
typedef struct SavedMachine
{
	CONST_STRING_MODIFIABLE pMachineID; AST(KEY)
	CONST_STRING_MODIFIABLE pMachineName;
	const U32 uLastSeenTime;
	CONST_STRING_MODIFIABLE ip; AST(ESTRING)
} SavedMachine;
AST_PREFIX();

// One-to-One mapping with LoginProtocols
AUTO_ENUM;
typedef enum MachineType
{
	MachineType_CrypticClient = 1, // Cryptic Launcher(s), GameClient - all instances on a single computer share the same MachineID
	MachineType_WebBrowser, // Websites, any validation through XML-RPC
	MachineType_All, // also the count
} MachineType;

typedef struct AccountInfo AccountInfo;

bool machineIDIsValid(const char *pMachineID);
bool machineLockingIsIPWhitelisted(U32 uIp);

bool accountMachineLockingIsEnabled(AccountInfo *account);
// Check if the machine ID is saved and update the last access; do not update if NULL is passed in for the ip
bool accountIsMachineIDSaved(AccountInfo *account, const char *pMachineID, MachineType eType, const char *ip);
bool accountMachineSaveNextClient(AccountInfo *account, U32 time);
bool accountMachineSaveNextBrowser(AccountInfo *account, U32 time);
bool accountMachineSaveNextMachineByType(AccountInfo *account, U32 time, MachineType eType);

void accountAddSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType, const char *pMachineName, const char *ip);
void accountRenameSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType, const char *pMachineName);
void accountClearSavedMachines(AccountInfo *account, MachineType eType);
void accountRemoveSavedMachine(AccountInfo *account, const char *pMachineID, MachineType eType);
void accountMachineLockingEnable(AccountInfo *account, bool bEnable);
// Enable the flag that tells the account to automatically save and allow the first machine ID that logs in
void accountMachineLockingSaveNext(AccountInfo *account, bool bEnable, MachineType eType, bool bProcessingAutosave);
void accountMachineProcessAutosave(AccountInfo *account, const char *pMachineID, const char *pMachineName, MachineType eType, const char *ip);

bool accountMachineGenerateOneTimeCode(AccountInfo *account, const char *pMachineID, const char *ip);
void accountMachineRemoveOneTimeCode(AccountInfo *account, const char *pMachineID);
bool accountMachineValidateOneTimeCode(AccountInfo *account, const char *pMachineID, MachineType eType, 
	const char *pOneTimeCode, const char *pMachineName, const char *ip);


void oneTimeCodeTick(void);