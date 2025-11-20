/***************************************************************************
*     Copyright (c) 2010, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#ifndef DBCONTAINERRESTORE_H_
#define DBCONTAINERRESTORE_H_

#include "GlobalTypes.h"

typedef struct AccountRestoreRequest AccountRestoreRequest;
typedef struct ContainerRestoreState ContainerRestoreState;
typedef struct PossibleCharacterChoices PossibleCharacterChoices;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct TransactionRequest TransactionRequest;
typedef struct ContainerRef ContainerRef;
typedef struct CmdContext CmdContext;
typedef struct AccountStub AccountStub;

typedef struct TransactionReturnVal TransactionReturnVal;
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);

typedef enum RestoreType
{
	RESTORETYPE_ENTITYPLAYER = 0,
	RESTORETYPE_ACCOUNTWIDE
} RestoreType;

typedef struct RestoreCallbackData
{
	SlowRemoteCommandID slowCommandID;
	// This TransactionReturnCallback needs to take a ContainerRestoreRequest as the userData
	TransactionReturnCallback cbFunc;
    void *appData;
} RestoreCallbackData;

typedef struct ContainerRestoreRequest
{
	ContainerID sourceID;
	GlobalType eGlobalType;

	int iRefCount;
	bool bKeepCached;

	bool bUseAccountID;
	ContainerID accountID;

	ContainerRestoreState *pRestoreState;

	TransactionRequest *pRequest;

	ContainerRef *pConRef;

	int eRestoreStatus;

	U32 iTimeRequested;

	bool bDone;
	bool bRecursive;
	bool bAlertOnExistingContainer;

	RestoreType eRestoreType;

	// This TransactionReturnCallback needs to take a ContainerRestoreRequest as the userData
	RestoreCallbackData callbackData;
	const char *displayName;
//	AccountRestoreRequest *pAccountRequest;
} ContainerRestoreRequest; 

typedef struct AccountRestoreRequest
{
	bool cached;
	const char *displayName;
	const char *characterName;
	U32 accountID;

	INT_EARRAY offlineCharacterIDs; 

	bool done;
	int eStatus;
	RestoreCallbackData	callbackData;
} AccountRestoreRequest;

enum
{
	CR_SUCCESS = 4,
	CR_TRANSACTION_BUILT = 3,
	CR_LOADINGHOG = 2,
	CR_QUEUED = 1,
	CR_COULDNTOPENHOG = -1,
	CR_UNKNOWNRESTORE = -2,
	CR_ALREADYREQUESTED = -3,
	CR_NOTHINGTODO = -4,
	CR_TRANSACTIONFAILED = -5,
	CR_CONTAINERNOTFOUND = -6,
	CR_CONTAINERINDELETEDCACHE = -7,
	CR_RESTORINGONCLONE = -8,
	CR_NAMECOLLISION = -9,
	CR_QUEUEISFULL = -12,
	CR_CHARACTERISNOTOFFLINE = -13,
	CR_ALREADYRESTOREDFROMOFFLINE = -14,
	CR_CONTAINERACCOUNTMISMATCH = -15,
	CR_CONTAINERNOTOWNEDBYDB = -16,
};

void RestoreContainer_CB(TransactionReturnVal *pReturn, ContainerRestoreRequest *request);
int RestoreContainer(CmdContext *context, GlobalType type, U32 id, ContainerRestoreState *rs);
bool GetOrCreateContainerRestoreRequest(ContainerRef *ref, ContainerRestoreRequest **ppRequest);
void CleanUpContainerRestoreRequest(ContainerRestoreRequest **ppRequest);
int dbContainerRestoreStatus(GlobalType containerType, ContainerID containerID);

bool InitiateAutoRestore(U32 accountID);

// When calling the Ex version, the TransactionReturnCallback needs to take a ContainerRestoreRequest as the userData
#define AutoRestoreEntityPlayer(accountID, containerID) AutoRestoreContainerEx(accountID, GLOBALTYPE_ENTITYPLAYER, containerID, RESTORETYPE_ENTITYPLAYER, NULL)
#define AutoRestoreEntityPlayerEx(accountID, containerID, callbackData) AutoRestoreContainerEx(accountID, GLOBALTYPE_ENTITYPLAYER, containerID, RESTORETYPE_ENTITYPLAYER, callbackData)
bool AutoRestoreContainerEx(ContainerID accountID, GlobalType containerType, ContainerID containerID, RestoreType restoreType, const RestoreCallbackData *callbackData);

bool buildRestoreTransaction(GlobalType type, ContainerID id, ContainerRestoreState *rs, TransactionRequest *request, bool recursive, bool topLevel, bool alertOnExistingContainer);

void initContainerRestoreStashTable();
void initContainerRestoreThread();
void UpdateContainerRestores(TimedCallback* cb, F32 timeSinceLastCallback, UserData userData);

void GetOfflineDependentContainers(const char *file, GlobalType containerType, ContainerID containerID, ContainerRef ***pppRefs);

#define FindOfflineCharactersAndAutoRestore(resultString, callbackData, accountId) FindOfflineCharactersAndAutoRestoreEx(resultString, NULL, callbackData, accountId, NULL)
bool FindOfflineCharactersAndAutoRestoreEx(char **resultString, INT_EARRAY *pOfflineCharacterIDs, const RestoreCallbackData *callbackData, U32 accountId, const char *characterName);

// Initializes an accountRestoreRequest
// Makes a copy of displayName
void InitAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest, const char *displayName);

// Initializes an accountRestoreRequest on the stack
// Does not make a copy of displayName
void InitTemporaryAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest, const char *displayName);

// Creates an AccountRestoreRequest on the heap and stores it in a stash table.
AccountRestoreRequest *GetCachedAccountRestoreRequest(const char *displayName);

// Creates an AccountRestoreRequest on the heap
AccountRestoreRequest *CreateAccountRestoreRequest(const char *displayName);

// Destroy an AccountRestoreRequest
// Will not destroy an AccountRestoreRequest created by GetCachedAccountRestoreRequest
void DestroyAccountRestoreRequest(AccountRestoreRequest *accountRestoreRequest);

int GetTotalRestoredCharacters(void);
U32 GetLastRestoreTime(void);
void SetLastRestoreTime(U32 restoreTime);

#define RestoreContainersForAccountStub(pStub) RestoreContainersForAccountStubEx(NULL, pStub, NULL, NULL, NULL, true)
bool RestoreContainersForAccountStubEx(char **resultString, AccountStub *pStub, const char *pCharacterName, INT_EARRAY *peaOfflineCharacters, const RestoreCallbackData *callbackData, bool ignoreLazyLoad);
void RestoreAccountWideContainersForAccountStub(AccountStub *pStub, bool ignoreLazyLoad);

#endif