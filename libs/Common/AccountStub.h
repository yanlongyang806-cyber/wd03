/***************************************************************************



***************************************************************************/

#ifndef ACCOUNTSTUB_H
#define ACCOUNTSTUB_H

#pragma once
GCC_SYSTEM

#include "GlobalTypeEnum.h"

typedef struct TransactionRequest TransactionRequest;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct ObjectIndexHeader ObjectIndexHeader;
typedef struct NOCONST(CharacterStub) NOCONST(CharacterStub);
typedef struct NOCONST(AccountStub) NOCONST(AccountStub);
typedef void (*TransactionReturnCallback)(TransactionReturnVal *returnVal, void *userData);

// The CharacterStub should contain all data from the header. 
AUTO_STRUCT AST_CONTAINER;
typedef struct CharacterStub
{
	const ContainerID							iContainerID;					AST(PERSIST SUBSCRIBE KEY)
	const char									savedName[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const bool									restored;						AST(PERSIST SUBSCRIBE)
	const U32									createdTime;					AST(PERSIST SUBSCRIBE)
	const U32									level;							AST(PERSIST SUBSCRIBE)
	const U32									fixupVersion;					AST(PERSIST SUBSCRIBE)
	const U32									lastPlayedTime;					AST(PERSIST SUBSCRIBE)
	const U32									virtualShardId;					AST(PERSIST SUBSCRIBE)

	const char									pubAccountName[MAX_NAME_LEN];	AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									privAccountName[MAX_NAME_LEN];	AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									extraData1[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									extraData2[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									extraData3[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									extraData4[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
	const char									extraData5[MAX_NAME_LEN];		AST(PERSIST SUBSCRIBE USERFLAG(TOK_PUPPET_NO_COPY)) 
} CharacterStub;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountWideContainerStub
{
	const GlobalType							containerType;				AST(PERSIST SUBSCRIBE KEY)
	const bool									restored;					AST(PERSIST SUBSCRIBE)
} AccountWideContainerStub;

AUTO_STRUCT AST_CONTAINER;
typedef struct AccountStub
{
	const ContainerID								iAccountID;			AST(PERSIST SUBSCRIBE KEY)
		// Entity's Account ID

	CONST_EARRAY_OF(CharacterStub)					eaOfflineCharacters; AST(PERSIST SUBSCRIBE)

	CONST_EARRAY_OF(AccountWideContainerStub)		eaOfflineAccountWideContainers; AST(PERSIST SUBSCRIBE)
} AccountStub;

void BuildTempCharacterStubFromHeader(NOCONST(CharacterStub) *characterStub, const ObjectIndexHeader *header);
void AddClearOfflineEntityPlayersInAccountStubToRequest(TransactionRequest *request, U32 iAccountID);
void AddOfflineEntityPlayerToAccountStubRequest(TransactionRequest *request, U32 iAccountID, const ObjectIndexHeader *header);
void AddRemoveOfflineEntityPlayerFromAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerID);
void EnsureAccountStubExists(U32 iAccountID, TransactionReturnCallback func, void *userData);
void AddEnsureAccountStubExistsToRequest(TransactionRequest *request, U32 iAccountID);
void AddMarkEntityPlayerRestoredInAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerID);
void AddOfflineAccountWideContainerToAccountStubRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType);
void AddMarkAccountWideContainerRestoredInAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType);
void AddRemoveOfflineAccountWideContainerFromAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType);

bool PushOfflineEntityPlayerToAccountStub(	NOCONST(AccountStub) *stub, const char *characterStubString);
void RemoveOfflineEntityPlayerFromAccountStub(NOCONST(AccountStub) *stub, U32 containerID);
bool MarkEntityPlayerRestoredInAccountStub(NOCONST(AccountStub) *stub, U32 containerID);

bool PushOfflineAccountWideContainerToAccountStub(NOCONST(AccountStub) *stub, U32 containerType);
void RemoveOfflineAccountWideContainerFromAccountStub(NOCONST(AccountStub) *stub, U32 containerType);
bool MarkAccountWideContainerRestoredInAccountStub(NOCONST(AccountStub) *stub, U32 containerType);

int GetCharacterStubIndex(AccountStub *accountStub, ContainerID containerID);

#endif ACCOUNTSTUB_H