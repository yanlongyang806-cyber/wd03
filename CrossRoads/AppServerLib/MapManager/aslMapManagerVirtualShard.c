#include "aslMapManager.h"
#include "GlobalTypes.h"
#include "VirtualShard_h_ast.h"
#include "TextParser.h"
#include "objContainer.h"
#include "VirtualShard.h"
#include "Alerts.h"
#include "ResourceManager.h"
#include "aslMapManagerVirtualShard.h"
#include "autogen/objectdb_autogen_remotefuncs.h"
#include "autogen/appserverlib_autotransactions_autogen_wrappers.h"

AUTO_TRANSACTION
ATR_LOCKS(pShard, ".Bdisabled");
enumTransactionOutcome virtualshard_tr_SetVirtualShardEnabled(ATR_ARGS, NOCONST(VirtualShard) *pShard, int bEnabled)
{
	pShard->bDisabled = !bEnabled;
	return TRANSACTION_OUTCOME_SUCCESS;
}


AUTO_COMMAND;
int SetVirtualShardEnabled(const char *pcName, bool bEnabled)
{
	VirtualShard *pShard = NULL;
	const char *pcVirtualShard_CopyDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD);

	if(!RefSystem_DoesDictionaryExist(pcVirtualShard_CopyDictName))
	{
		ErrorOrAlert(__FUNCTION__ "_Must_Be_On_Map_Manager", "The SetVirtualShardEnabled command must be invoked on the MapManager.");
		return false;
	}

	FOR_EACH_IN_REFDICT(pcVirtualShard_CopyDictName, VirtualShard, pIterShard)
	{
		if ( stricmp(pIterShard->pName, pcName) == 0 )
		{
			pShard = pIterShard;
			break;
		}
	}
	FOR_EACH_END;

	if (!pShard)
	{
		return false;
	}
	AutoTrans_virtualshard_tr_SetVirtualShardEnabled(NULL, GetAppGlobalType(), GLOBALTYPE_VIRTUALSHARD, pShard->id, bEnabled);
	return true;
}

AUTO_COMMAND;
int EnableUGCVirtualShard(void)
{
	return SetVirtualShardEnabled("UGCShard", 1);
}

AUTO_COMMAND;
int DisableUGCVirtualShard(void)
{
	return SetVirtualShardEnabled("UGCShard", 0);
}

static bool s_bSentVirtualShardDisabledALert = false;
AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void aslMapManager_SetVirtualShardEnabled(const char *pcName, bool bEnabled)
{
	if(0 == stricmp(pcName, "UGCShard"))
	{
		if(!bEnabled && !s_bSentVirtualShardDisabledALert)
		{
			// Alert that the MapManager had its UGCShard disabled automatically. This remote command is called when a project is marked as needs republishing or needs unplayable.
			CRITICAL_NETOPS_ALERT("UGC_VIRTUAL_SHARD_AUTO_DISABLED", "The UGC Virtual Shard was automatically disabled, presumably because projects are being marked as needs republish or needs unplayable.");
			s_bSentVirtualShardDisabledALert = true;
		}
		else if(bEnabled)
			s_bSentVirtualShardDisabledALert = false; // reset flag so we will alert immediately the next time the UGCShard is disabled.
	}

	SetVirtualShardEnabled(pcName, bEnabled);
}

void NewVirtualShard_CB(TransactionReturnVal *pReturn, void *pUserData)
{


}


AUTO_COMMAND;
void CreateVirtualShard(int iIsUGCShard, ACMD_SENTENCE pName)
{
	NOCONST(VirtualShard) *pNewShard = StructCreateNoConst(parse_VirtualShard);

	pNewShard->pName = strdup(pName);
	pNewShard->bUGCShard = iIsUGCShard;
	pNewShard->bNoPVPQueues = true;
	pNewShard->bNoAuctions = true;

	objRequestContainerCreate(objCreateManagedReturnVal(NewVirtualShard_CB, NULL), 
		GLOBALTYPE_VIRTUALSHARD, pNewShard, GetAppGlobalType(), GetAppGlobalID());

	StructDestroyNoConst(parse_VirtualShard, pNewShard);
}


void CreateUGCVirtualShard(void)
{
	NOCONST(VirtualShard) *pNewShard = StructCreateNoConst(parse_VirtualShard);

	pNewShard->pName = strdup("UGCShard");
	pNewShard->bUGCShard = true;
	pNewShard->bNoPVPQueues = true;
	pNewShard->bNoAuctions = true;
	pNewShard->id = 1;

	objRequestContainerVerifyOrCreateAndInit(objCreateManagedReturnVal(NewVirtualShard_CB, NULL), 
		GLOBALTYPE_VIRTUALSHARD, 1, pNewShard, GetAppGlobalType(), GetAppGlobalID());

	StructDestroyNoConst(parse_VirtualShard, pNewShard);
}


void DoesSingleContainerExistCB(TransactionReturnVal *returnVal, void *userData)
{
	enumTransactionOutcome eOutcome;
	int iResult;

	eOutcome = RemoteCommandCheck_DBCheckSingleContainerExists(returnVal, &iResult);

	switch (eOutcome)
	{
	case TRANSACTION_OUTCOME_SUCCESS:
		if (iResult == 0)
		{
			CreateUGCVirtualShard();
		}
		break;
	default:
		AssertOrAlert("UGC_CANT_CHECK_CONTAINER", "Map manager asked object DB whether ugc shard exists, couldn't get answer");
		break;

	}

}

void aslMapManagerVirtualShards_StartNormalOperation(void)
{
	if (gConf.bUserContent)
	{
		RemoteCommand_DBCheckSingleContainerExists( objCreateManagedReturnVal(DoesSingleContainerExistCB, NULL),
				GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_VIRTUALSHARD, 1);
	}

	// Track virtual shards
	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), false, parse_VirtualShard, false, false, NULL);
	resDictSetMaxUnreferencedResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), RES_DICT_KEEP_ALL); // We need to keep them around even if not references
	objSubscribeToOnlineContainers(GLOBALTYPE_VIRTUALSHARD);
}

static U32 s_uLastUGCVirtualShardDisabledCheck = 0;

void aslMapManagerVirtualShards_NormalOperation(void)
{
	if(s_uLastUGCVirtualShardDisabledCheck == 0 || s_uLastUGCVirtualShardDisabledCheck + 12*60*60 < timeSecondsSince2000()) // once per twelve hours
	{
		VirtualShard *pUGCShard = NULL;
		const char *pcVirtualShard_CopyDictName = GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD);

		FOR_EACH_IN_REFDICT(pcVirtualShard_CopyDictName, VirtualShard, pIterShard)
		{
			if(0 == stricmp(pIterShard->pName, "UGCShard"))
			{
				pUGCShard = pIterShard;
				break;
			}
		}
		FOR_EACH_END;

		if(pUGCShard && pUGCShard->bDisabled)
			CRITICAL_NETOPS_ALERT("UGC_VIRTUAL_SHARD_STILL_DISABLED", "The UGC Virtual Shard is still disabled. Should it be enabled, yet?");

		s_uLastUGCVirtualShardDisabledCheck = timeSecondsSince2000();
	}
}
