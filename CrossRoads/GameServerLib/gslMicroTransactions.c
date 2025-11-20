
#include "AutoTransDefs.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "file.h"
#include "GameAccountData\GameAccountData.h"
#include "GameAccountData_h_ast.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "globalTypes.h"
#include "gslMicroTransactions.h"
#include "gslSendToClient.h"
#include "MicroTransactions.h"
#include "MicroTransactions_h_ast.h"
#include "MicroTransactions_Transact.h"
#include "MicroTransactions_Transact_h_ast.h"
#include "microtransactions_common.h"
#include "microtransactions_common_h_ast.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "Reward.h"
#include "ServerLib.h"

#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/TestServer_autogen_RemoteFuncs.h"
#include "AutoGen/GameserverLib_autotransactions_autogen_wrappers.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME("ImportMicroTransactions");
void MicroTransCmd_ImportMicroTransactions(Entity *pEnt, ACMD_SENTENCE pchFile)
{
	static char pcFileName[CRYPTIC_MAX_PATH];
	static char tmpStr[CRYPTIC_MAX_PATH];

	VerifyServerTypeExistsInShard(GLOBALTYPE_TESTSERVER);

	if(!fileLocateWrite("server/TestServer/scripts/tests/AccountServer/Products/ImportProducts.lua", tmpStr) || !fileExists(tmpStr))
	{
		if(pEnt)
		{
			gslSendPrintf(pEnt, "ImportMicroTransactions: Couldn't find import script [%s]", "server/TestServer/scripts/tests/AccountServer/Products/ImportProducts.lua");
		}
		else
		{
			printf("ImportMicroTransactions: Couldn't find import script [%s]", "server/TestServer/scripts/tests/AccountServer/Products/ImportProducts.lua");
		}
		return;
	}

	if(fileLocateWrite(pchFile, pcFileName) && fileExists(pcFileName))
	{
		if(strEndsWith(pcFileName, ".Csv"))
		{
			pcFileName[strlen(pcFileName)-3] = 'c';
		}
		RemoteCommand_TestServer_SetGlobal_Integer(GLOBALTYPE_TESTSERVER, 0, NULL, "AccountServer_Port", -1, 8090);
		RemoteCommand_TestServer_SetGlobal_String(GLOBALTYPE_TESTSERVER, 0, NULL, "Products_ImportFile", -1, pcFileName);
		RemoteCommand_TestServer_SetGlobal_String(GLOBALTYPE_TESTSERVER, 0, NULL, "AccountServer_User", -1, "");
		RemoteCommand_TestServer_SetGlobal_String(GLOBALTYPE_TESTSERVER, 0, NULL, "AccountServer_Password", -1, "");
		RemoteCommand_TestServer_RunScript(GLOBALTYPE_TESTSERVER, 0, "tests/AccountServer/Products/ImportProducts.lua");
	}
	else if(pEnt)
	{
		gslSendPrintf(pEnt, "ImportMicroTransactions: Couldn't find import file [%s]", pchFile);
	}
	else
	{
		printf("ImportMicroTransactions: Couldn't find import file [%s]", pchFile);
	}
}

AUTO_TRANSACTION ATR_LOCKS(pEnt, ".Pplayer.Pmicrotransinfo.Eaonetimepurchases");
enumTransactionOutcome entity_tr_AddOneTimePurchase(ATR_ARGS, NOCONST(Entity) *pEnt, const char *pchKey)
{
	if(NONNULL(pEnt) && 
		NONNULL(pEnt->pPlayer) &&
		NONNULL(pEnt->pPlayer->pMicroTransInfo) &&
		eaFindString(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases, pchKey) < 0)
	{
		eaPush(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases, StructAllocString(pchKey));
		return(TRANSACTION_OUTCOME_SUCCESS);
	}
	return(TRANSACTION_OUTCOME_FAILURE);
}

AUTO_TRANSACTION ATR_LOCKS(pEnt, ".Pplayer.Pmicrotransinfo.Eaonetimepurchases");
enumTransactionOutcome entity_tr_RemoveOneTimePurchase(ATR_ARGS, NOCONST(Entity) *pEnt, const char *pchKey)
{
	if(NONNULL(pEnt) && 
		NONNULL(pEnt->pPlayer) &&
		NONNULL(pEnt->pPlayer->pMicroTransInfo))
	{
		S32 i = eaFindString(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases, pchKey);
		if (i >= 0)
		{
			StructFreeString(eaRemove(&pEnt->pPlayer->pMicroTransInfo->eaOneTimePurchases, i));
			return(TRANSACTION_OUTCOME_SUCCESS);
		}
	}
	return(TRANSACTION_OUTCOME_FAILURE);
}

S32 MicroTrans_IsSpecialKey(const char *pchKey)
{
	return(eaIndexedFindUsingString(&g_MicroTransConfig.eaSpecialKeys, pchKey) >= 0);
}

const char* MicroTrans_SpecialKeyMesg(const char *pchKey)
{
	SpecialKey *pSpecialKey = eaIndexedGetUsingString(&g_MicroTransConfig.eaSpecialKeys, pchKey);
	return pSpecialKey ? REF_STRING_FROM_HANDLE(pSpecialKey->msgDisplay.hMessage) : NULL;
}

bool MicroTrans_GenerateRewardBags(int iPartitionIdx, Entity *pEnt, MicroTransactionDef *pDef, MicroTransactionRewards *pRewards)
{
	if (pEnt && pDef && pRewards)
	{
		bool bHasRewards = false;
		S32 i;
		for (i = 0; i < eaSize(&pDef->eaParts); i++)
		{
			MicroTransactionPart *pPart = pDef->eaParts[i];
			RewardTable *pRewardTable = GET_REF(pPart->hRewardTable);
			if (pRewardTable)
			{
				MicroTransactionPartRewards* pPartRewards = StructCreate(parse_MicroTransactionPartRewards);
				pPartRewards->iPartIndex = i;
				reward_GenerateBagsForMicroTransaction(iPartitionIdx, pEnt, pRewardTable, &pPartRewards->eaBags);
				eaPush(&pRewards->eaRewards, pPartRewards);
				bHasRewards = true;
			}
		}
		return bHasRewards;
	}
	return false;
}

//////////////////////////////////////////
// CSR COMMANDS
//////////////////////////////////////////

AUTO_COMMAND ACMD_NAME("MicroTrans_RemoveOneTimePurchase") ACMD_ACCESSLEVEL(4) ACMD_CATEGORY(csr);
void MicroTrans_cmd_RemoveOneTimePurchase(Entity *pEnt, const char* pchKey)
{
	if (pEnt && pEnt->pPlayer && pEnt->pPlayer->pMicroTransInfo)
	{
		AutoTrans_entity_tr_RemoveOneTimePurchase(NULL, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, entGetContainerID(pEnt), pchKey);
	}
}

//#include "gslMicroTransactions_h_ast.c"
