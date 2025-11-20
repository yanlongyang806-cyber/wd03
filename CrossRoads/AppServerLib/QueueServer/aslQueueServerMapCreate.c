/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/


#include "aslQueue.h"
#include "group.h"
#include "queue_common.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs.h"
#include "StringCache.h"
#include "MapDescription.h"
#include "utilitiesLib.h"

#include "Autogen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

typedef struct OpenMapCBStruct
{
	const char *pchQueueName;	//Pool String
	const char* pchMapName;		//Pool String
	U32 uiInstanceID;
	ContainerID iMapID;
	U32 uPartitionID;
	U32 uiMapRequestTime;
	U32 uiMapCreateRequestID;	
	QueueMember **ppQueueMembers;
} OpenMapCBStruct;

void aslQueue_InformPlayersOfRemoval(QueueInfo *pQueue, QueueInstance *pInstance)
{
	S32 iMemberIdx;
	for(iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMember *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		if(pMember && !aslQueue_MemberActivated(pMember))
		{
			queue_SendResultMessage(pMember->iEntID, 0, pQueue->pchName, QUEUE_FORCED_REMOVAL_MESG);
		}
	}
}

static void ClearInstance_CB(TransactionReturnVal *returnVal, void *userData)
{
	OpenMapCBStruct *pCBStruct = (OpenMapCBStruct*) userData;
	QueueInfo* pQueue;

	if (!pCBStruct->pchQueueName || !pCBStruct->pchQueueName[0])
	{
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pQueue = aslQueue_FindQueueByNameLocal(pCBStruct->pchQueueName)))
	{
		Errorf("No queue named [%s] in ClearInstance transaction callback", pCBStruct->pchQueueName);
		SAFE_FREE(pCBStruct);
		return;
	}

	pQueue->bDirty = false;
}

static void OpenNewMap_CB(TransactionReturnVal *returnVal, void *userData)
{
	OpenMapCBStruct *pCBStruct = (OpenMapCBStruct*) userData;
	QueueInfo* pQueue;
	QueueInstance* pInstance;
	
	if (!pCBStruct->pchQueueName || !pCBStruct->pchQueueName[0])
	{
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pQueue = aslQueue_FindQueueByNameLocal(pCBStruct->pchQueueName)))
	{
		Errorf("No queue named [%s] in OpenNewMap transaction callback", pCBStruct->pchQueueName);
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, pCBStruct->uiInstanceID)))
	{
		Errorf("No instance found by ID [%d] in OpenNewMap transaction callback", pCBStruct->uiInstanceID);
		SAFE_FREE(pCBStruct);
		return;	
	}

	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char pchSuccess[256];
		QueueMap *pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pCBStruct->iMapID);
		U32 uiElapsedTime = timeSecondsSince2000() - pCBStruct->uiMapRequestTime;

		QueueMap *pNewMap = queue_GetNewMap(pInstance, pCBStruct->uiMapCreateRequestID);

		if(pMap && pNewMap)
		{
			eaCopyStructs(&pNewMap->eaVariables, &pMap->eaVariables, parse_WorldVariable);
		}

		sprintf(pchSuccess, "Opened map [%d] completely. Seconds elapsed since map request: [%d]", 
			pCBStruct->iMapID, uiElapsedTime);
		aslQueue_Log(GET_REF(pQueue->hDef), pInstance, pchSuccess);

		pInstance->uiFailedMapLaunchCount = 0;
		pInstance->uiNextMapLaunchTime = 0;
	}
	
	//Destroy the old map being created
	queue_DestroyNewMap(pInstance, pCBStruct->uiMapCreateRequestID);
	SAFE_FREE(pCBStruct);
}

static U32 Queue_GetLaunchDelay(QueueInstance *pInstance)
{
	if(pInstance->uiFailedMapLaunchCount < QUEUE_FAILED_LAUNCH_COUNT_NORMAL)
	{
		return QUEUE_FAILED_LAUNCH_DELAY_NORMAL;
	}

	return QUEUE_FAILED_LAUNCH_DELAY;
}

static void ReturnDestination_CB(TransactionReturnVal *returnVal, void *userData)
{
	OpenMapCBStruct *pCBStruct = (OpenMapCBStruct*) userData;
	QueueInfo* pQueue;
	QueueInstance* pInstance;
	ReturnedGameServerAddress* pReturnedAddress;

	if (!pCBStruct->pchQueueName || !pCBStruct->pchQueueName[0])
	{
		Errorf("No queue name in aslMapCreate callback");
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pQueue = aslQueue_FindQueueByNameLocal(pCBStruct->pchQueueName)))
	{
		Errorf("No queue named %s in aslMapCreate callback", pCBStruct->pchQueueName);
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, pCBStruct->uiInstanceID)))
	{
		Errorf("No queue instance %d in aslMapCreate callback", pCBStruct->uiInstanceID);
		SAFE_FREE(pCBStruct);
		return;
	}

	switch(RemoteCommandCheck_RequestNewOrExistingGameServerAddress(returnVal, &pReturnedAddress))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		Errorf("Getting game server address failed in aslMapCreate");
		SAFE_FREE(pCBStruct);
		return;

	case TRANSACTION_OUTCOME_SUCCESS:
		if (pReturnedAddress->iContainerID != 0)
		{
			S64 iMapKey = queue_GetMapKey(pReturnedAddress->iContainerID, pReturnedAddress->uPartitionID);
			const char* pchMapName = pCBStruct->pchMapName;
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(OpenNewMap_CB, pCBStruct);
			pCBStruct->iMapID = pReturnedAddress->iContainerID;

			//Sets the map key and create time, open for bidness
			AutoTrans_aslQueue_tr_OpenNewMap(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pInstance->uiID, iMapKey, pchMapName);
		}
		else
		{
			char pchFailString[256];
			pInstance->uiFailedMapLaunchCount++;
			pInstance->uiNextMapLaunchTime = timeSecondsSince2000() + Queue_GetLaunchDelay(pInstance);

			sprintf(pchFailString, "MapManager returned a [0] for containerID for time [%d]", pInstance->uiFailedMapLaunchCount);
			aslQueue_Log(GET_REF(pQueue->hDef), pInstance, pchFailString);

			if(pInstance->uiFailedMapLaunchCount >= 10)
			{
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(ClearInstance_CB, pCBStruct);
				
				Errorf("MapManager returned a [0] for containerID for queue [%s][%d] instance [%d] for the 10th time.  That queue has been halted, and everyone is kicked.",
					pQueue->pchName,
					pQueue->iContainerID,
					pInstance->uiID);

				pQueue->bDirty = true;

				aslQueue_InformPlayersOfRemoval(pQueue, pInstance);

				AutoTrans_aslQueue_tr_ClearInstance(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pInstance->uiID);
				
				//No free, freed in ClearInstance_CB
				//Don't clear the new map variable
				return;
			}
			else if(pInstance->uiFailedMapLaunchCount == 5)
			{
				Errorf("MapManager returned a [0] for containerID for queue [%s][%d] instance [%d] for the 5th time.  5 more times (without success) will result in a broken queue.",
					pQueue->pchName,
					pQueue->iContainerID,
					pInstance->uiID);
			}

			queue_DestroyNewMap(pInstance, pCBStruct->uiMapCreateRequestID);
			SAFE_FREE(pCBStruct);
			return;
		}
		break;
	}
}

static void aslQueue_MapTransferChooseAddress(PossibleMapChoice *pChoice, OpenMapCBStruct* pData)
{
	bool bFound = false;
	NewOrExistingGameServerAddressRequesterInfo requesterInfo = {0};

	if (pChoice->bDebugLogin)
	{
		Errorf("Error in aslMapCreate");
		SAFE_FREE(pData);
		return;
	}

	requesterInfo.pcRequestingShardName = GetShardNameFromShardInfoString();
	requesterInfo.eRequestingServerType = GetAppGlobalType();
	requesterInfo.iRequestingServerID = GetAppGlobalID();

	RemoteCommand_RequestNewOrExistingGameServerAddress(
		objCreateManagedReturnVal_TransactionMayTakeALongTime(ReturnDestination_CB, pData),
		GLOBALTYPE_MAPMANAGER, 0, pChoice, NULL, &requesterInfo);
}

static OpenMapCBStruct *s_fd;

static void GetPossibleMapChoices_CB(TransactionReturnVal *returnVal, void *userData)
{
	OpenMapCBStruct *pCBStruct = (OpenMapCBStruct*) userData;
	QueueInfo* pQueue;
	QueueInstance* pInstance;
	PossibleMapChoices* pMapChoices;
	QueueMap *pNewMap;

	s_fd = pCBStruct;

	if (!pCBStruct->pchQueueName || !pCBStruct->pchQueueName[0])
	{
		Errorf("No queue name in aslMapCreate callback");
		SAFE_FREE(pCBStruct);
		return;
	}
	if(!(pQueue = aslQueue_FindQueueByNameLocal(pCBStruct->pchQueueName)))
	{
		Errorf("No queue named %s in aslMapCreate callback", pCBStruct->pchQueueName);
		SAFE_FREE(pCBStruct);
		return;
	}
	
	if(!(pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, pCBStruct->uiInstanceID)))
	{
		Errorf("No queue instance %d in aslMapCreate callback", pCBStruct->uiInstanceID);
		SAFE_FREE(pCBStruct);
		return;
	}

	pNewMap = queue_GetNewMap(pInstance, pCBStruct->uiMapCreateRequestID);

	if (!pNewMap)
	{
		Errorf("Couldn't find launched map for instance %d in aslMapCreate callback", pCBStruct->uiInstanceID);
		SAFE_FREE(pCBStruct);
		return;		
	}

	switch(RemoteCommandCheck_GetPossibleMapChoices(returnVal, &pMapChoices))
	{
	case TRANSACTION_OUTCOME_FAILURE:
		Errorf("Getting map choices failed in aslMapCreate");
		SAFE_FREE(pCBStruct);
		return;
	case TRANSACTION_OUTCOME_SUCCESS:
		{
			PossibleMapChoice* pChoice = eaGet(&pMapChoices->ppChoices, 0);

			if(pChoice)
			{
				aslQueue_Log(GET_REF(pQueue->hDef), pInstance, "Requesting new server address.");
				aslQueue_MapTransferChooseAddress(pChoice, pCBStruct);
			}
			else
			{
				Errorf("Couldn't find the [%s] queue's requested map ID [%d]", pQueue->pchName, pNewMap->uiMapCreateRequestID);
				SAFE_FREE(pCBStruct);
				return;
			}
		}

		break;
	}
}


void aslQueue_MapCreateRequest(SA_PARAM_NN_VALID QueueDef *pDef, 
							   SA_PARAM_NN_VALID QueueInstance *pInstance,
							   const char* pchMapName,
							   U32 uiRequestID,
							   QueueMember **ppMatchMembers)
{
	char pchRequestInfo[256];
	MapSearchInfo choiceInfo = {0};
	OpenMapCBStruct *pCBStruct = calloc(1, sizeof(OpenMapCBStruct));
	QueueMap *pNewMap = queue_GetNewMap(pInstance, uiRequestID);

	choiceInfo.baseMapDescription.eMapType = pDef->MapSettings.eMapType;
	choiceInfo.baseMapDescription.mapDescription = allocAddString(pchMapName);
	choiceInfo.baseMapDescription.ownerID = uiRequestID;
	choiceInfo.baseMapDescription.ownerType = GLOBALTYPE_QUEUESERVER;
	choiceInfo.baseMapDescription.mapVariables = worldVariableArrayToString(pNewMap->eaVariables);
	choiceInfo.eSearchType = MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES;

	sprintf(pchRequestInfo, "Requesting new map creation of ID [%d]", uiRequestID);
	aslQueue_Log(pDef, pInstance, pchRequestInfo);

	pCBStruct->pchQueueName = allocAddString(pDef->pchName);
	pCBStruct->uiInstanceID = pInstance->uiID;
	pCBStruct->uiMapRequestTime = timeSecondsSince2000();
	pCBStruct->pchMapName = allocAddString(pchMapName);
	pCBStruct->uiMapCreateRequestID = uiRequestID;
	if(eaSize(&ppMatchMembers))
	{
		eaCopy(&pCBStruct->ppQueueMembers,&ppMatchMembers);
	}

	RemoteCommand_GetPossibleMapChoices(
		objCreateManagedReturnVal_TransactionMayTakeALongTime(GetPossibleMapChoices_CB, pCBStruct),
		GLOBALTYPE_MAPMANAGER, 0, &choiceInfo, NULL, "MapCreateRequest from Queue Server");
}
