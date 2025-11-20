#include "AccountStub.h"
#include "AccountStub_h_ast.h"

#include "AutoTransDefs.h"
#include "objSchema.h"

#include "objTransactions.h"
#include "objIndex.h"

AUTO_RUN_LATE;
int RegisterAccountStub(void)
{
	//Register the game account data
	objRegisterNativeSchema(GLOBALTYPE_ACCOUNTSTUB, parse_AccountStub, NULL, NULL, NULL, NULL, NULL);

	return 1;
}

void AddClearOfflineEntityPlayersInAccountStubToRequest(TransactionRequest *request, U32 iAccountID)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"ClearOfflineEntityPlayersInAccountStub containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_ACCOUNTSTUB),
		iAccountID);
}

void BuildTempCharacterStubFromHeader(NOCONST(CharacterStub) *characterStub, const ObjectIndexHeader *header)
{
	characterStub->iContainerID = header->containerId;
	characterStub->createdTime = header->createdTime;
	characterStub->level = header->level;
	characterStub->fixupVersion = header->fixupVersion;
	characterStub->lastPlayedTime = header->lastPlayedTime;
	characterStub->virtualShardId = header->virtualShardId;
	if(header->pubAccountName)
		strcpy(characterStub->pubAccountName, header->pubAccountName);
	if(header->privAccountName)
		strcpy(characterStub->privAccountName, header->privAccountName);
	if(header->savedName)
		strcpy(characterStub->savedName, header->savedName);
	if(header->extraData1)
		strcpy(characterStub->extraData1, header->extraData1);
	if(header->extraData2)
		strcpy(characterStub->extraData2, header->extraData2);
	if(header->extraData3)
		strcpy(characterStub->extraData3, header->extraData3);
	if(header->extraData4)
		strcpy(characterStub->extraData4, header->extraData4);
	if(header->extraData5)
		strcpy(characterStub->extraData5, header->extraData5);
}

void AddOfflineEntityPlayerToAccountStubRequest(TransactionRequest *request, U32 iAccountID, const ObjectIndexHeader *header)
{
	NOCONST(CharacterStub) characterStub = {0};
	char *estr = NULL;
	char *estrOut = NULL;
	BuildTempCharacterStubFromHeader(&characterStub, header);
	ParserWriteText(&estr, parse_CharacterStub, &characterStub, 0, 0, 0);
	estrSuperEscapeString(&estrOut, estr);
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"AddOfflineEntityPlayerToAccountStub %d \"%s\"",
		iAccountID, estrOut);
	estrDestroy(&estr);
	estrDestroy(&estrOut);
}

void AddMarkEntityPlayerRestoredInAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerID)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"MarkEntityPlayerRestoredInAccountStub %d %d",
		iAccountID, iContainerID);
}

void AddRemoveOfflineEntityPlayerFromAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerID)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"RemoveOfflineEntityPlayerFromAccountStub %d %d",
		iAccountID, iContainerID);
}

void AddOfflineAccountWideContainerToAccountStubRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"AddOfflineAccountWideContainerToAccountStub %d %d",
		iAccountID, iContainerType);
}

void AddMarkAccountWideContainerRestoredInAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"MarkAccountWideContainerRestoredInAccountStub %d %d",
		iAccountID, iContainerType);
}

void AddRemoveOfflineAccountWideContainerFromAccountStubToRequest(TransactionRequest *request, U32 iAccountID, U32 iContainerType)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"RemoveOfflineAccountWideContainerFromAccountStub %d %d",
		iAccountID, iContainerType);
}

// This creates the single container if it doesn't already exist.
void AddEnsureAccountStubExistsToRequest(TransactionRequest *request, U32 iAccountID)
{
	objAddToTransactionRequestf(request, GLOBALTYPE_OBJECTDB, objServerID(), NULL, 
		"VerifyContainer containerIDVar %s %d",
		GlobalTypeToName(GLOBALTYPE_ACCOUNTSTUB),
		iAccountID);
}

// This creates the single container if it doesn't already exist.
void EnsureAccountStubExists(U32 iAccountID, TransactionReturnCallback func, void *userData)
{
	if( iAccountID > 0 )
	{
		TransactionRequest *request = objCreateTransactionRequest();
		AddEnsureAccountStubExistsToRequest(request, iAccountID);
		if(func)
		{
			objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
				objCreateManagedReturnVal(func, userData), "EnsureGameContainerExists", request);
		}
		else
		{
			objRequestTransaction_Flagged(TRANS_TYPE_SEQUENTIAL_ATOMIC,
				NULL, "EnsureGameContainerExists", request);
		}
		objDestroyTransactionRequest(request);
	}
}

int CompareCharacterStubs(const CharacterStub *pArrayObj, const CharacterStub *pUserInput)
{
	if(pArrayObj == NULL || pUserInput == NULL)
	{
		return pArrayObj == pUserInput;
	}
	else
	{
		return pArrayObj->iContainerID == pUserInput->iContainerID;
	}
}

int GetCharacterStubIndex(AccountStub *accountStub, ContainerID containerID)
{
	NOCONST(CharacterStub) characterStub = {0};
	if(!accountStub)
		return -1;

	characterStub.iContainerID = containerID;

	return eaFindCmp(&accountStub->eaOfflineCharacters, &characterStub, CompareCharacterStubs);
}

// returns true if stub->eaOfflineCharacters[index]->restored changed.
bool PushOfflineEntityPlayerToAccountStub(NOCONST(AccountStub) *stub, const char *characterStubString)
{
	NOCONST(CharacterStub) *characterStub = StructCreateNoConst(parse_CharacterStub);
	bool retval = false;
	int index;
	char *estrUnescaped = NULL;
	assert(stub);

	estrSuperUnescapeString(&estrUnescaped, characterStubString);
	ParserReadText(estrUnescaped, parse_CharacterStub, characterStub, 0);

	estrDestroy(&estrUnescaped);

	if((index = eaFindCmp(&stub->eaOfflineCharacters, characterStub, CompareCharacterStubs)) == -1)
	{
		retval = true;
		characterStub->restored = false;
		eaPush(&stub->eaOfflineCharacters, characterStub);
	}
	else
	{
		if(stub->eaOfflineCharacters[index]->restored)
			retval = true;
		StructCopyNoConst(parse_CharacterStub, characterStub, stub->eaOfflineCharacters[index], 0, 0, 0);
		stub->eaOfflineCharacters[index]->restored = false;
		StructDestroyNoConst(parse_CharacterStub, characterStub);
	}		
	return retval;
}

void RemoveOfflineEntityPlayerFromAccountStub(NOCONST(AccountStub) *stub, U32 containerID)
{
	int index;
	NOCONST(CharacterStub) *characterStub;
	assert(stub);

	characterStub = StructCreateNoConst(parse_CharacterStub);
	characterStub->iContainerID = containerID;
	if((index = eaFindCmp(&stub->eaOfflineCharacters, characterStub, CompareCharacterStubs)) != -1)
	{
		eaRemove(&stub->eaOfflineCharacters, index);
	}

	StructDestroyNoConst(parse_CharacterStub, characterStub);
}

// returns true if stub->eaOfflineCharacters[index]->restored changed.
bool MarkEntityPlayerRestoredInAccountStub(NOCONST(AccountStub) *stub, U32 containerID)
{
	bool retval = false;
	int index;
	NOCONST(CharacterStub) *characterStub;
	assert(stub);

	characterStub = StructCreateNoConst(parse_CharacterStub);
	characterStub->iContainerID = containerID;
	if((index = eaFindCmp(&stub->eaOfflineCharacters, characterStub, CompareCharacterStubs)) != -1)
	{
		if(!stub->eaOfflineCharacters[index]->restored)
			retval = true;

		stub->eaOfflineCharacters[index]->restored = true;
	}

	StructDestroyNoConst(parse_CharacterStub, characterStub);
	return retval;
}

// returns true if stub->eaOfflineCharacters[index]->restored changed.
bool PushOfflineAccountWideContainerToAccountStub(NOCONST(AccountStub) *stub, U32 containerType)
{
	bool retval = false;
	NOCONST(AccountWideContainerStub) *containerStub;
	assert(stub);

	containerStub = eaIndexedGetUsingInt(&stub->eaOfflineAccountWideContainers, containerType);
	if(containerStub)
	{
		if(containerStub->restored)
			retval = true;

		containerStub->restored = false;
	}
	else
	{
		retval = true;
		containerStub = StructCreateNoConst(parse_AccountWideContainerStub);
		containerStub->containerType = containerType;
		if(!stub->eaOfflineAccountWideContainers)
			eaIndexedEnableNoConst(&stub->eaOfflineAccountWideContainers, parse_AccountWideContainerStub);
		eaIndexedAdd(&stub->eaOfflineAccountWideContainers, containerStub);		
	}

	return retval;
}

void RemoveOfflineAccountWideContainerFromAccountStub(NOCONST(AccountStub) *stub, U32 containerType)
{
	NOCONST(AccountWideContainerStub) *containerStub;
	assert(stub);

	containerStub = eaIndexedRemoveUsingInt(&stub->eaOfflineAccountWideContainers, containerType);
	if(containerStub)
		StructDestroyNoConst(parse_AccountWideContainerStub, containerStub);
}

// returns true if stub->eaOfflineCharacters[index]->restored changed.
bool MarkAccountWideContainerRestoredInAccountStub(NOCONST(AccountStub) *stub, U32 containerType)
{
	bool retval = false;
	NOCONST(AccountWideContainerStub) *containerStub;
	assert(stub);

	containerStub = eaIndexedGetUsingInt(&stub->eaOfflineAccountWideContainers, containerType);
	if(containerStub)
	{
		if(!containerStub->restored)
			retval = true;

		containerStub->restored = true;
	}

	return retval;
}

#include "AccountStub_h_ast.c"

