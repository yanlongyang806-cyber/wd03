#pragma once

#include "transactionsystem.h"
#include "rand.h"
#include "autogen/gameserverlib_autogen_remotefuncs.h"
#include "serverlib.h"
#include "Autogen/controller_autogen_remotefuncs.h"
#include "../../core/controller/pub/controllerpub.h"
#include "textparser.h"

AUTO_COMMAND_REMOTE;
void gslDoMeaninglessThingAsRemoteTransaction(int iServerToRespondTo, char *pTestString)
{
	RemoteCommand_gslDoMeaninglessThingAsRemoteTransaction( GLOBALTYPE_GAMESERVER, iServerToRespondTo, gServerLibState.containerID, pTestString);
}

#define GSL_TRANS_TESTS_TO_START_AT_ONCE 10

#define TEST_STRING_LENGTH 1000

AUTO_COMMAND ACMD_CATEGORY(Test);
void gslTransTest_LotsOfMeaninglessRemotecommands(void)
{
	int iServerNumToTest = gServerLibState.containerID - 1;
	int i;

	char testString[TEST_STRING_LENGTH + 1];

	for (i=0; i < TEST_STRING_LENGTH; i++)
	{
		testString[i] = 'a';
	}

	testString[TEST_STRING_LENGTH] = 0;




	if (iServerNumToTest < 1)
	{
		return;
	}

	for (i=0; i < GSL_TRANS_TESTS_TO_START_AT_ONCE; i++)
	{
		RemoteCommand_gslDoMeaninglessThingAsRemoteTransaction(GLOBALTYPE_GAMESERVER, iServerNumToTest, gServerLibState.containerID, testString);
	}
}



void ContinueTrainOfRandomTransactions(TransactionReturnVal *returnVal, void *userData)
{
	Controller_ServerList *pList = NULL;
	
	if (RemoteCommandCheck_GetServerList(returnVal, &pList) == TRANSACTION_OUTCOME_SUCCESS)
	{
		int iSize = eaSize(&pList->ppServers);
		char randomString[1025];
		int i;
		int iServerIndexToUse;
		assert(iSize);

		for (i=0; i < 1024; i++)
		{
			randomString[i] = '0' + randInt(10);
		}

		randomString[1024] = 0;

		iServerIndexToUse = randInt(100) % iSize;

		RemoteCommand_rcStartChainOfRandomTransactions(GLOBALTYPE_GAMESERVER, pList->ppServers[iServerIndexToUse]->iGlobalID, randomString);

		StructDestroy(parse_Controller_ServerList, pList);
	}

}


AUTO_COMMAND_REMOTE;
void rcStartChainOfRandomTransactions(char *pBigIrrelevantString)
{
	printf("Part of random transaction chain\n");

	RemoteCommand_GetServerList( objCreateManagedReturnVal(ContinueTrainOfRandomTransactions, NULL),
		GLOBALTYPE_CONTROLLER, 0, GLOBALTYPE_GAMESERVER);
}

	
AUTO_COMMAND ACMD_CATEGORY(Test);
void StartChainOfRandomTransactions(void)
{
	rcStartChainOfRandomTransactions("asdfasdfasdfasdf");
}





//this is a nice SUPER-simple auto trans for testing auto trans stuff.
#include "referencesystem.h"
#include "Team.h"
#include "Entity.h"
#include "Player.h"
#include "entity_h_ast.h"
#include "Player_h_ast.h"
#include "autogen\GameServerLib_autotransactions_autogen_wrappers.h"
#include "ActivityCommon.h"
/*
AUTO_TRANS_HELPER;
void atrh_TestEntTrans(ATR_ARGS, ATH_ARG NOCONST(Entity) *pMyEnt, char *pRewardName)
{	
	RewardModifier *pReward = eaIndexedGetUsingString(&pMyEnt->pPlayer->eaRewardMods, pRewardName);
}
*/


#if 0
AUTO_TRANSACTION ATR_LOCKS(ppMyEnts, ".Pplayer.Uirespecconversions");
enumTransactionOutcome atr_TestEntTrans(ATR_ARGS, CONST_EARRAY_OF(NOCONST(Entity)) ppMyEnts, char *pStrArg, 
	int iIntArg, float fFloatArg, ActivityDef *pDef1, ActivityDef *pDef2)
{
	int i;
	//atrh_TestEntTrans(ATR_RECURSE, pMyEnt, pRewardName);
//	int iSize = eaSize(&pMyEnt->pPlayer->eaRewardMods);
	
	for (i = 0; i < eaSize(&ppMyEnts); i++)
	{
		ppMyEnts[i]->pPlayer->uiRespecConversions++;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}
#endif

#include "rewardCommon.h"
#include "rewardcommon_h_ast.h"

AUTO_TRANSACTION ATR_MAKE_APPEND
	ATR_LOCKS(pMyEnt, ".pPlayer.uiRespecConversions");
enumTransactionOutcome atr_SetRespectConversions(ATR_ARGS, NOCONST(Entity) *pMyEnt, int iVal)
{
	if (iVal)
	{
		pMyEnt->pPlayer->uiRespecConversions = iVal;
		return TRANSACTION_OUTCOME_SUCCESS;
	}
	else
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}
}

AUTO_TRANSACTION ATR_MAKE_APPEND
	ATR_LOCKS(pMyEnt, "pPlayer.loginCookie, pPlayer.eaRewardMods[]");
enumTransactionOutcome atr_TestAddRewardModifier(ATR_ARGS, NOCONST(Entity) *pMyEnt, char *pIndex, NON_CONTAINER RewardModifier *pRewardModifier)
{
	pMyEnt->pPlayer->loginCookie++;

	if (eaIndexedGetUsingString(&pMyEnt->pPlayer->eaRewardMods, pIndex))
	{
		printf("Already there, failing\n");
		TRANSACTION_RETURN_LOG_FAILURE("failed");
	}

	if (eaIndexedPushUsingStringIfPossible(&pMyEnt->pPlayer->eaRewardMods, pIndex, StructClone(parse_RewardModifier, pRewardModifier)))
	{
		TRANSACTION_RETURN_LOG_SUCCESS("Woo hoo, it worked!");
	}
	else
	{
		assert(0);
	}
}

void TestEntTransCB(TransactionReturnVal *returnVal, void *userData)
{
	int iBrk = 0;
}

#include "StringCache.h"

AUTO_COMMAND;
void Test_AddRewardModifier(char *pKey)
{
	NOCONST(RewardModifier) *pModifier = (NOCONST(RewardModifier)*)StructCreate(parse_RewardModifier);
	pModifier->pchNumeric = allocAddString(pKey);

	AutoTrans_atr_TestAddRewardModifier(objCreateManagedReturnVal(TestEntTransCB, NULL), 
		GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, 1, pKey, (RewardModifier*)pModifier);

	StructDestroy(parse_RewardModifier, (RewardModifier*)pModifier);
}



AUTO_TRANSACTION
	ATR_LOCKS(pMyEnt, "pPlayer.eaRewardMods[]");
enumTransactionOutcome atr_TestRemoveRewardModifier(ATR_ARGS, NOCONST(Entity) *pMyEnt, char *pIndex)
{
	NOCONST(RewardModifier) *pModifier = eaIndexedRemoveUsingString_FailOnNULL(&pMyEnt->pPlayer->eaRewardMods, pIndex);

	if (!pModifier)
	{
		printf("Not there, failing\n");
		TRANSACTION_RETURN_LOG_FAILURE("failed");
	}

	TRANSACTION_RETURN_SUCCESS("succeeded");
}


AUTO_COMMAND;
void Test_RemoveRewardModifier(char *pKey)
{
	AutoTrans_atr_TestRemoveRewardModifier(objCreateManagedReturnVal(TestEntTransCB, NULL), 
		GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, 1, pKey);
}

void TestMultiATRCB(TransactionReturnVal *returnVal, void *userData)
{
	int iBrk = 0;
}

AUTO_COMMAND;
void Test_MultiATR(char *pKey, int iVal, bool bForceFail)
{
	TransactionRequest *pRequest = objCreateTransactionRequest();
	NOCONST(RewardModifier) *pModifier = (NOCONST(RewardModifier)*)StructCreate(parse_RewardModifier);
	pModifier->pchNumeric = allocAddString(pKey);

	AutoTransAppend_atr_TestAddRewardModifier(pRequest, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, 1, pKey, (RewardModifier*)pModifier);
	
	AutoTransAppend_atr_SetRespectConversions(pRequest, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, 1, iVal);

	if (bForceFail)
	{
		AutoTransAppend_atr_SetRespectConversions(pRequest, GetAppGlobalType(), GLOBALTYPE_ENTITYPLAYER, 1, 0);
	}

	RequestNewTransaction(objLocalManager(), "MultiATR", pRequest->ppBaseTransactions,
		TRANS_TYPE_SEQUENTIAL_ATOMIC,objCreateManagedReturnVal(TestMultiATRCB, NULL), 
		TRANSREQUESTFLAG_DO_LOCAL_STRUCT_FIXUP_FOR_AUTO_TRANS_WITH_APPENDING);

	objDestroyTransactionRequest(pRequest);
}

		