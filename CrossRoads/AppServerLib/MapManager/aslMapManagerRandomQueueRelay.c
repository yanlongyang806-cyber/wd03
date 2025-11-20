
#include "aslMapManagerrandomQueueRelay.h"

#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "EString.h"
#include "stdtypes.h"
#include "StashTable.h"

#include "autogen/GameServerLib_autogen_RemoteFuncs.h"

char *pchRandomActiveQueues = NULL;

AUTO_COMMAND_REMOTE;
void aslMapManger_RelayRandomActiveQueues(const char *pchNewRandomActiveQueues)
{
	if(pchRandomActiveQueues)
	{
		estrDestroy(&pchRandomActiveQueues);
		pchRandomActiveQueues = NULL;
	}

	estrCreate(&pchRandomActiveQueues);

	estrPrintf(&pchRandomActiveQueues,"%s",pchNewRandomActiveQueues);

	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;
			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
				RemoteCommand_gslQueue_UpdateRandomActiveQueues(GLOBALTYPE_GAMESERVER,pServer->iContainerID,pchRandomActiveQueues);
			}
		}
	}
	FOR_EACH_END
}

void aslMapManager_MapInitRandomActiveQueues(U32 iServerID)
{
	if(pchRandomActiveQueues)
		RemoteCommand_gslQueue_UpdateRandomActiveQueues(GLOBALTYPE_GAMESERVER,iServerID,pchRandomActiveQueues);
}