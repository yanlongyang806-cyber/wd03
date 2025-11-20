/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

// DO NOT include structures or parse tables for any actual containers (like Entity, team, etc). 
// These will NOT work correctly on the DB, because things are packed differently

#include "dbCharacterChoice.h"
#include "objIndex.h"
#include "ObjectDB.h"
#include "MultiWorkerThread.h"
#include "LoginCommon.h"
#include "AccountStub.h"
#include "timing_profiler.h"
#include "StringCache.h"
#include "StructNet.h"
#include "objTransactions.h"
#include "tokenstore.h"
#include "dbContainerRestore.h"
#include "dbGenericDatabaseThreads.h"
#include "ContinuousBuilderSupport.h"
#include "ThreadSafeMemoryPool.h"
#include "FreeThread.h"

#include "AutoGen/dbRemoteCommands_c_ast.h"
#include "textparser.h"
#include "AutoGen/GlobalTypes_h_ast.h"
#include "AutoGen/LoginCommon_h_ast.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"
#include "autogen/ObjectDB_autogen_SlowFuncs.h"
#include "AutoGen/HeadshotServer_autogen_remoteFuncs.h"
#include "../serverlib/autogen/objTransactions_h_ast.h"

extern ObjectIndex *gAccountID_idx;
extern ObjectIndex *gAccountIDDeleted_idx;

// AUTO_STRUCT TSMPs
TSMP_DEFINE(CharacterIDNameList);
TSMP_DEFINE(CharacterIDName);
TSMP_DEFINE(ContainerList);

PlayerCostume *dbGetPlayerCostume(ParseTable *pParseTable, void *pData, ParseTable **ppCostumeParseTable)
{
	MultiVal mv = {0};
	char *value = NULL;
	int iCostumeNum = 0;
	PlayerCostume *pCostume = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(objPathGetMultiVal(".pSaved.CostumeData.ActiveCostume",  pParseTable, pData, &mv))
	{
		iCostumeNum = MultiValGetInt(&mv, false);
	}
	estrStackCreate(&value);
	estrPrintf(&value, ".pSaved.CostumeData.CostumeSlot[%d].Costume", iCostumeNum);
	if(!objPathGetStruct(value, pParseTable, pData, ppCostumeParseTable, &pCostume))
	{
		pCostume = NULL;
		*ppCostumeParseTable = NULL;
	}
	estrDestroy(&value);

	PERFINFO_AUTO_STOP();

	return pCostume;
}

bool dbPopulateDeletedChoiceLists = false;
AUTO_CMD_INT(dbPopulateDeletedChoiceLists, PopulateDeletedChoiceLists); ACMD_COMMANDLINE;

AUTO_RUN;
void InitDBChoiceList(void)
{
	TSMP_SMART_CREATE(CharacterIDNameList, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_CharacterIDNameList, &TSMP_NAME(CharacterIDNameList));

	TSMP_SMART_CREATE(CharacterIDName, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_CharacterIDName, &TSMP_NAME(CharacterIDName));

	TSMP_SMART_CREATE(ContainerList, 256, TSMP_X64_RECOMMENDED_CHUNK_SIZE);
	ParserSetTPIUsesThreadSafeMemPool(parse_ContainerList, &TSMP_NAME(ContainerList));
}

static CharacterIDNameList *MakeCharacterIDNameListFromArray(ContainerID *eaIDs, bool deleted)
{
	CharacterIDNameList *ids = StructCreate(parse_CharacterIDNameList);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		CharacterIDName *pair;
		Container *con = objGetContainerGeneralEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true, deleted);

		if (con)
		{
			pair = StructCreate(parse_CharacterIDName);
			pair->id = con->header->containerId;
			estrPrintf(&pair->name, "%s", con->header->savedName);
			objUnlockContainer(&con);

			eaPush(&ids->list, pair);
		}
	}
	EARRAY_FOREACH_END;

	return ids;
}

static CharacterIDNameList *DBReturnCharactersByAccountID_internal(U32 accountID, bool bDeleted)
{
	ContainerID *eaIDs = NULL;
	CharacterIDNameList *pList = NULL;

	if (bDeleted)
	{
		eaIDs = GetDeletedContainerIDsFromAccountID(accountID);
	}
	else
	{
		eaIDs = GetContainerIDsFromAccountID(accountID);
	}

	pList = MakeCharacterIDNameListFromArray(eaIDs, bDeleted);
	ea32Destroy(&eaIDs);

	return pList;
}

//This command retrieves character name/id pairs belonging to a given account.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
CharacterIDNameList *DBReturnCharactersByAccountID(U32 accountID)
{
	return DBReturnCharactersByAccountID_internal(accountID, false);
}

//This command retrieves deleted character name/id pairs belonging to a given account.
AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
CharacterIDNameList *DBReturnDeletedCharactersByAccountID(U32 accountID)
{
	return DBReturnCharactersByAccountID_internal(accountID, true);
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(9);
CharacterIDNameList *DBReturnOfflineCharactersByAccountID(U32 accountID)
{
	if (gDatabaseConfig.bEnableOfflining)
	{
		CharacterIDNameList *offlineids = StructCreate(parse_CharacterIDNameList);
		Container *con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountID, true, false, true);

		if(con)
		{
			AccountStub *stub = con->containerData;
			if(stub)
			{
				EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, i);
				{
					if(!stub->eaOfflineCharacters[i]->restored)
					{
						CharacterIDName *pair = StructCreate(parse_CharacterIDName);
						pair->id = stub->eaOfflineCharacters[i]->iContainerID;
						estrCopy2(&pair->name, stub->eaOfflineCharacters[i]->savedName);
						eaPush(&offlineids->list, pair);
					}
				}
				EARRAY_FOREACH_END;
			}

			objUnlockContainer(&con);
		}

		return offlineids;
	}

	return NULL;
}

typedef struct PlayerCostumeV0 PlayerCostumeV0;

PlayerCostumeV0 *dbGetPlayerCostumeV0(ParseTable *pParseTable, void *pData, ParseTable **ppCostumeParseTable)
{
	MultiVal mv = {0};
	char *path = NULL;
	int iCostumeType = -1;
	int iCostumeNum = 0;
	PlayerCostumeV0 *pCostume = NULL;

	PERFINFO_AUTO_START_FUNC();

	if(objPathGetMultiVal(".pSaved.costumes.activeCostumeType", pParseTable, pData, &mv))
	{
		iCostumeType = MultiValGetInt(&mv, false);
	}

	if(iCostumeType == 0)
	{
		if(objPathGetMultiVal(".pSaved.costumes.activePrimaryCostume", pParseTable, pData, &mv))
		{
			iCostumeNum = MultiValGetInt(&mv, false);
		}

		estrStackCreate(&path);
		estrPrintf(&path, ".pSaved.costumes.eaPrimaryCostumes[%d]", iCostumeNum);
		objPathGetStruct(path, pParseTable, pData, ppCostumeParseTable, &pCostume);
		estrDestroy(&path);
	}
	else if(iCostumeType == 1)
	{
		if(objPathGetMultiVal(".pSaved.costumes.activeSecondaryCostume", pParseTable, pData, &mv))
		{
			iCostumeNum = MultiValGetInt(&mv, false);
		}

		estrStackCreate(&path);
		estrPrintf(&path, ".pSaved.costumes.eaSecondaryCostumes[%d]", iCostumeNum);
		objPathGetStruct(path, pParseTable, pData, ppCostumeParseTable, &pCostume);
		estrDestroy(&path);
	}

	PERFINFO_AUTO_STOP();

	return pCostume;
}

//zOMG DO NOT USE THIS!!!
AUTO_COMMAND_REMOTE ACMD_NAME(RequestPlayerCostumeString);
void DBRequestPlayerCostumeString(GlobalType eType, ContainerID iID, GlobalType eWhereToSendItType, ContainerID iWhereToSendItID, int iRequestID)
{
	char *pOutString = NULL;
	Container *pContainer = objGetContainerEx(eType, iID, true, false, true);
	ParseTable *pCostumeParseTable = NULL;
	int iFixupVersion = 0;

	PERFINFO_AUTO_START_FUNC();

	if (pContainer)
	{
		void *pCostume = NULL;

		pCostume = dbGetPlayerCostumeV0(pContainer->containerSchema->classParse, pContainer->containerData, &pCostumeParseTable);

		if(!pCostume)
		{
			iFixupVersion = 5;
			pCostume = dbGetPlayerCostume(pContainer->containerSchema->classParse, pContainer->containerData, &pCostumeParseTable);
		}

		if(pCostume)
		{
			estrStackCreate(&pOutString);
			ParserWriteText(&pOutString, pCostumeParseTable, pCostume, 0, 0, 0);
		}

		objUnlockContainer(&pContainer);
	}

	RemoteCommand_ReturnPlayerCostumeString(eWhereToSendItType, iWhereToSendItID, iRequestID, pOutString ? pOutString : "", iFixupVersion);
	estrDestroy(&pOutString);

	PERFINFO_AUTO_STOP();
}

U32 StringifyCharacterList(char **resultString, const char *accountName)
{
	ContainerID *eaIDs = NULL;
	U32 accountID = 0;

	eaIDs = GetContainerIDsFromDisplayName(accountName);
	estrClear(resultString);

	EARRAY_INT_CONST_FOREACH_BEGIN(eaIDs, i, n);
	{
		Container *con = objGetContainerEx(GLOBALTYPE_ENTITYPLAYER, eaIDs[i], false, false, true);

		if (con)
		{
			accountID = con->header->accountId;

			if (con->header->savedName)
			{
				estrConcatf(resultString, "%d - %s\n", eaIDs[i], con->header->savedName);
			}
			else
			{
				estrConcatf(resultString, "%d\n", eaIDs[i]);
			}

			objUnlockContainer(&con);
		}
	}
	EARRAY_FOREACH_END;

	ea32Destroy(&eaIDs);
	return accountID;
}

void dbAccountRestoreRequest_CB(TransactionReturnVal *returnVal, AccountRestoreRequest *request)
{
	if (RemoteCommandCheck_aslAPCmdGetAccountIDFromDisplayName(returnVal, &request->accountID) == TRANSACTION_OUTCOME_SUCCESS)
	{
		// Check for offline characters
		FindOfflineCharactersAndAutoRestore(NULL, &request->callbackData, request->accountID);
		request->eStatus = 1;
	}
	else
	{
		request->eStatus = -1;
	}

	request->done = true;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(4);
char * ContainerIDsFromAccountNameWithAutoRestore(const char *accountName)
{
	static char *resultString = NULL;
	U32 accountId = 0;
	AccountRestoreRequest* request;
	
	if(IsThisCloneObjectDB())
	{
		estrCopy2(&resultString, "Do not run this on the CloneObjectDB.");
		return resultString;
	}

	request = GetCachedAccountRestoreRequest(accountName);
	request->callbackData.cbFunc = NULL;
	accountId = StringifyCharacterList(&resultString, accountName);

	if(accountId)
	{
		FindOfflineCharactersAndAutoRestore(&resultString, &request->callbackData, accountId);
	}
	else
	{
		// Get the accountid from the AccountProxyServer
		RemoteCommand_aslAPCmdGetAccountIDFromDisplayName(
			objCreateManagedReturnVal(dbAccountRestoreRequest_CB, request),
			GLOBALTYPE_ACCOUNTPROXYSERVER, 0, accountName);

		estrAppend2(&resultString, "Getting account id. Check back later for offline characters.");
	}

	return resultString;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(4);
char * ContainerIDsFromAccountNameStatusCheck(const char *accountName)
{
	static char *resultString = NULL;
	U32 accountId = 0;
	Container *con = NULL;
	AccountStub *stub = NULL;
	AccountRestoreRequest* request;

	if(IsThisCloneObjectDB())
	{
		estrCopy2(&resultString, "Do not run this on the CloneObjectDB.");
		return resultString;
	}

	request = GetCachedAccountRestoreRequest(accountName);
	request->callbackData.cbFunc = NULL;
	accountId = StringifyCharacterList(&resultString, accountName);

	if(!accountId)
		accountId = request->accountID;

	if(accountId)
	{
		con = objGetContainerEx(GLOBALTYPE_ACCOUNTSTUB, accountId, true, false, true);
	}

	if(con)
	{
		stub = con->containerData;
	}

	EARRAY_INT_FOREACH_REVERSE_BEGIN(request->offlineCharacterIDs, i);
	{
		int status = dbContainerRestoreStatus(GLOBALTYPE_ENTITYPLAYER, request->offlineCharacterIDs[i]);
		if (status == CR_SUCCESS)
		{
			ea32RemoveFast(&request->offlineCharacterIDs, i);
		}
		else
		{
			const char *name = NULL;
			if (stub)
			{
				EARRAY_FOREACH_BEGIN(stub->eaOfflineCharacters, j);
				{
					if (!stub->eaOfflineCharacters[j])
					{
						continue;
					}

					if (stub->eaOfflineCharacters[j]->iContainerID == request->offlineCharacterIDs[i] && stub->eaOfflineCharacters[j]->savedName)
					{
						name = stub->eaOfflineCharacters[j]->savedName;
						break;
					}
				}
				EARRAY_FOREACH_END;
			}

			if (name && name[0])
				estrConcatf(&resultString, "%d - %s: RestoreStatus %d\n", request->offlineCharacterIDs[i], name, status);
			else
				estrConcatf(&resultString, "%d: RestoreStatus %d\n", request->offlineCharacterIDs[i], status);
		}
	}
	EARRAY_FOREACH_END;

	if (con)
	{
		objUnlockContainer(&con);
	}

	return resultString;
}

AUTO_COMMAND ACMD_CATEGORY(ObjectDB) ACMD_ACCESSLEVEL(4);
char * ContainerIDsFromAccountName(const char *accountName)
{
	static char *resultString = NULL;
	StringifyCharacterList(&resultString, accountName);
	return resultString;
}

// End of File
