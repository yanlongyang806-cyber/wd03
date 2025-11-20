/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "AppServerLib.h"
#include "aslQueue.h"
#include "AutoTransDefs.h"
#include "Entity.h"
#include "fileutil.h"
#include "logging.h"
#include "objContainer.h"
#include "objTransactions.h"
#include "partition_enums.h"
#include "queue_common.h"
#include "queue_common_structs.h"
#include "rand.h"
#include "serverlib.h"
#include "StringCache.h"
#include "Team.h"
#include "PvPGameCommon.h"
#include "ChoiceTable.h"
#include "GlobalTypes.h"

#include "aslQueue_h_ast.h"
#include "queue_common_h_ast.h"
#include "queue_common_structs_h_ast.h"

#include "queue_smartGroup.h"

#include "autogen/controller_autogen_remotefuncs.h"
#include "autogen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"

#define QUEUE_MAP_OPEN_TIMEOUT 120
#define QUEUE_MAP_LIMBO_TIMEOUT 120
#define QUEUE_MAP_LAUNCH_TIMEOUT 120
#define QUEUE_MAP_LAUNCHED_IS_OPEN 60
#define QUEUE_MAP_ACTIVE_TIMEOUT 60
#define QUEUE_MEMBER_LIMBO_TIMEOUT 60

//Queue Messages
#define QUEUE_OFFER_TIMEOUT_MESG "QueueServer_Offer_Timeout"
#define QUEUE_INVITE_TIMEOUT_MESG "QueueServer_Invite_Timeout"
#define QUEUE_LAUNCH_TIMEOUT_MESG "QueueServer_Launch_Timeout"
#define QUEUE_PLAYERS_NOT_ENOUGH_PLAYERS_MESG "QueueServer_NotEnoughPlayers"
#define QUEUE_OFFER_VALIDATE_TIMEOUT_MESG "QueueServer_OfferValidate_Timeout"
#define QUEUE_MAP_NOT_FOUND_MESG "QueueServer_MapNotFound"
#define QUEUE_BEYOND_MAX_JOIN_TIME_MESG "QueueServer_BeyondJoinTimeLimit"
#define QUEUE_MAP_FULL_MESG "QueueServer_MapFull"
#define QUEUE_TEAM_MEMBER_DECLINED_MESG "QueueServer_TeamMemberDeclined"

S32 s_bQueueServerReady = false;		// Updated in main once-per-frame. Exposed inline via aslQueue_ServerReady

static U32 s_uiRequestID = 1;
CachedTeamStruct **s_eaCachedTeams = NULL;
static char **s_eaPendingQueues = NULL;


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Queue Logging


void aslQueue_Log(QueueDef* pDef, 
				  QueueInstance* pInstance, 
				  const char* pchString)
{
	if (!pDef || !pchString)
		return;

	if (pInstance)
	{
		char pchQueueInfo[512];
		S32 iLevelBand = -1;
		const char* pchMapName = NULL;
		U32 uiPrivateOwnerID = 0;
		if (pInstance->pParams)
		{
			iLevelBand = pInstance->pParams->iLevelBandIndex;
			pchMapName = pInstance->pParams->pchMapName;
			uiPrivateOwnerID = pInstance->pParams->uiOwnerID;
		}
		if (!pchMapName || !pchMapName[0])
			pchMapName = "<Random>";

		sprintf(pchQueueInfo, "Queue[%s] Instance[%d] (Map=%s, LevelBand=%d, PrivateOwner=%d)",
			pDef->pchName, pInstance->uiID, pchMapName, iLevelBand, uiPrivateOwnerID);

		log_printf(LOG_QUEUE, "%s: %s\n", pchQueueInfo, pchString);
	}
	else
	{
		log_printf(LOG_QUEUE, "Queue[%s]: %s", pDef->pchName, pchString);
	}
}

static void aslQueue_LogMapDestroyed(QueueDef* pDef, 
									 ContainerID uMapID, 
									 U32 uPartitionID, 
									 QueueMapState eLastState,
									 const char* pchReason)
{
	char pchLogString[512];

	if (!pchReason)
		pchReason = "<Not provided>";

	sprintf(pchLogString, "Destroying map (MapID = %u, PartitionID = %u, Last State = %s). Reason: %s", 
		uMapID, uPartitionID, StaticDefineIntRevLookup(QueueMapStateEnum, eLastState), pchReason);

	aslQueue_Log(pDef, NULL, pchLogString);
}

static void aslQueue_LogMapLaunch(QueueDef* pDef, QueueInstance* pInstance, QueueMatch* pMatch, QueueMap* pMap)
{
	char pchMapLaunch[256];
	int i, iSize = eaSize(&pMatch->eaGroups);

	sprintf(pchMapLaunch, "Launching new map '%s'. ", pMap->pchMapName);

	if (iSize > 0)
	{
		strcatf(pchMapLaunch, "Players: ");
		for (i = 0; i < iSize; i++)
		{
			if (i != 0)
				strcat(pchMapLaunch, ", ");
			strcatf(pchMapLaunch, "Group%d=%d", i+1, pMatch->eaGroups[i]->iGroupSize);
		}
	}
	else
	{
		strcatf(pchMapLaunch, "Group information unavailable.");
	}
	aslQueue_Log(pDef, pInstance, pchMapLaunch);
}

static void aslQueue_LogAbortMatch(QueueInfo *pQueue, 
								   QueueInstance* pInstance, 
								   QueueMap* pMap,
								   S32 iAcceptedCount, 
								   S32 iOfferedCount, 
								   const char* pchAction)
{
	QueueDef* pDef = GET_REF(pQueue->hDef);
	if (pDef)
	{
		char pchString[256];
		U32 uMapID = queue_GetMapIDFromMapKey(pMap->iMapKey);
		U32 uPartitionID = queue_GetPartitionIDFromMapKey(pMap->iMapKey);
		
		sprintf(pchString, "was forced to cancel (%s) [MapID=%d,PartitionID=%d]. (%d/%d) players accepted their offers.", 
			pchAction, uMapID, uPartitionID, iAcceptedCount, iAcceptedCount+iOfferedCount);
		aslQueue_Log(pDef, pInstance, pchString);
	}
}

#define QUEUE_PERIODIC_LOG_TIME 3600
static void aslQueue_LogPeriodic(QueueInfo* pQueue, QueueDef* pDef)
{
	char pchString[256];
	S32 i, iSize = eaSize(&pQueue->eaInstances);
	for (i = 0; i < iSize; i++)
	{
		QueueInstance* pInstance = pQueue->eaInstances[i];
		S32 iQueuedPlayers = eaSize(&pInstance->eaUnorderedMembers);
		S32 iTrackedMaps = eaSize(&pInstance->eaMaps);
		sprintf(pchString, "has [%d] players queued and [%d] tracked maps.", iQueuedPlayers, iTrackedMaps);
		aslQueue_Log(pDef, pInstance, pchString);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  LocalData stuff
//    Copy out all the container data into local data so we don't have to const/deconst maybe? No. Not really. See SVN 92939

typedef struct QueueInfoList
{
	EARRAY_OF(QueueInfo)  eaLocalQueues;
} QueueInfoList;

static QueueInfoList s_LocalData = {0};

// Will create the local data if it doesn't exist yet.
static QueueInfo* aslQueue_CreateLocalData(SA_PARAM_NN_VALID QueueInfo* pQueue)
{
	StructTypeField eFlagsToExclude = 0;
	NOCONST(QueueInfo)* pLocalQueue = eaIndexedGetUsingInt(&s_LocalData.eaLocalQueues, pQueue->iContainerID);
	if (!pLocalQueue)
	{
		pLocalQueue = StructCreateNoConst(parse_QueueInfo);
		pLocalQueue->iContainerID = pQueue->iContainerID;
		eaIndexedEnable(&s_LocalData.eaLocalQueues, parse_QueueInfo);
		eaPush(&s_LocalData.eaLocalQueues, (QueueInfo*)pLocalQueue);
	}
	StructCopyDeConst(parse_QueueInfo, pQueue, pLocalQueue, 0, TOK_PERSIST, eFlagsToExclude);	
	return (QueueInfo*)pLocalQueue;
}

static QueueInfo* aslQueue_CreateLocalDataByName(const char* pchQueueName)
{
	// Get the container version that matches the name if it exists.

	ContainerIterator queueIter;
	QueueInfo* pQueue = NULL;
			
	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		if (stricmp(pQueue->pchName,pchQueueName) == 0)
			break;
	}
	objClearContainerIterator(&queueIter);
	
	if (pQueue)
	{
		return aslQueue_CreateLocalData(pQueue);
	}
	return NULL;
}

// WARNING: This function should only ever be called in aslQueue_Update
static S32 aslQueue_UpdateLocalData(void)
{
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;
	StructTypeField eFlagsToExclude = TOK_NO_TRANSACT;

	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);

	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		NOCONST(QueueInfo)* pLocalQueue = eaIndexedGetUsingInt(&s_LocalData.eaLocalQueues, pQueue->iContainerID);
		if (pLocalQueue)
		{
			StructCopyDeConst(parse_QueueInfo, pQueue, pLocalQueue, 0, TOK_PERSIST, eFlagsToExclude);
		}
	}
	objClearContainerIterator(&queueIter);
	return eaSize(&s_LocalData.eaLocalQueues);
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Find commands

// Gets the queue by container ID, returns NULL if it doesn't exist
QueueInfo* aslQueue_GetQueueLocal(ContainerID iQueueID)
{
	return (QueueInfo*)eaIndexedGetUsingInt(&s_LocalData.eaLocalQueues, iQueueID);
}

// Gets the queue by container ID, returns NULL if it doesn't exist
QueueInfo* aslQueue_GetQueueContainer(ContainerID iQueueID)
{
	Container* pContainer = objGetContainer(GLOBALTYPE_QUEUEINFO, iQueueID);
	if (pContainer) {
		return (QueueInfo*)pContainer->containerData;
	} else {
		return NULL;
	}
}

// Finds a queue with a given name, null for doesn't exist
QueueInfo* aslQueue_FindQueueByNameLocal(SA_PARAM_OP_STR const char* pchQueueName)
{
	S32 i;
	for (i = eaSize(&s_LocalData.eaLocalQueues)-1; i >= 0; i--)
	{
		QueueInfo* pQueue = s_LocalData.eaLocalQueues[i];
		if (stricmp(pQueue->pchName,pchQueueName) == 0)
			break;
	}
	return eaGet(&s_LocalData.eaLocalQueues, i);
}

static QueueInfo *aslQueue_FindQueueByDefLocal(QueueDef *pDef)
{
	int i;

	for(i=0;i<eaSize(&s_LocalData.eaLocalQueues);i++)
	{
		if(GET_REF(s_LocalData.eaLocalQueues[i]->hDef) == pDef)
			return s_LocalData.eaLocalQueues[i];
	}

	return NULL;
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Debug commands that attach to gslQueue_cmd.c


AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
QueueInstanceWrapper * aslQueue_DbgGetInstanceInfo(const char *pchName, U32 uInstanceID, U32 uEntID)
{
	// Get the queue if it is in local data
	QueueInfo* pQueue = aslQueue_FindQueueByNameLocal(pchName);
	if(pQueue)
	{
		// get the queue instance
		QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);
		if(pInstance)
		{
			QueueInstanceWrapper *pQueueWrap = StructCreate(parse_QueueInstanceWrapper);
			pQueueWrap->uEntID = uEntID;
			pQueueWrap->pQueueInstance = StructClone(parse_QueueInstance, pInstance);
			return pQueueWrap;
		}
	}

	return NULL;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
QueueInfoWrapper * aslQueue_DbgGetQueueInfo(const char *pchName, U32 uEntID)
{
	// Get the queue if it is in local data
	QueueInfo* pQueueInfo = aslQueue_FindQueueByNameLocal(pchName);
	if(pQueueInfo)
	{
		QueueInfoWrapper *pQueueWrap = StructCreate(parse_QueueInfoWrapper);
		pQueueWrap->uEntID = uEntID;
		pQueueWrap->pQueueInfo = StructClone(parse_QueueInfo, pQueueInfo);
		return pQueueWrap;
	}
	return NULL;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Send Chat Message

void aslQueue_SendChatMessageToMembersEx(SA_PARAM_NN_VALID QueueInfo* pQueue, 
										 ContainerID uInstanceID,
										 const char* pchMessageKey,
										 ContainerID uSubjectID,
										 ContainerID uIgnoreEntID,
										 PlayerQueueState eState,
										 bool bPrivate)
{
	QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);
	U32 uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);
	if (pInstance && (!bPrivate || uOrigOwnerID > 0))
	{
		S32 iMemberIdx;
		for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			if (uIgnoreEntID && uIgnoreEntID == pMember->iEntID)
			{
				continue;
			}
			if (eState >= 0 && pMember->eState != eState)
			{
				continue;
			}
			// Send to members in valid states only
			if (pMember->eState != PlayerQueueState_InQueue && 
				pMember->eState != PlayerQueueState_Accepted && 
				pMember->eState != PlayerQueueState_Countdown)
			{
				continue;
			}
			if (bPrivate)
			{
				RemoteCommand_gslQueue_SendMessageToPrivateChatChannel(GLOBALTYPE_ENTITYPLAYER,
																	   pMember->iEntID,
																	   pMember->iEntID,
																	   uOrigOwnerID,
																	   uInstanceID,
																	   pchMessageKey,
																	   uSubjectID);
			}
			else
			{
				queue_SendResultMessage(pMember->iEntID, uSubjectID, pQueue->pchName, pchMessageKey);
			}
		}
	}
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Random Queues (turned on if QueueCategories specify a number of active queues. Nobody uses this as of 5Feb2013)

RandomActiveQueues s_RandomActiveQueues = {0};

static S32 CmpS32(const S32 *i, const S32 *j) { return *i - *j; }

static void aslQueue_InitRandomQueueList(RandomActiveQueueList *pList)
{
	QueueDef *pDef;
	RefDictIterator queuedefIter;

	RefSystem_InitRefDictIterator(g_hQueueDefDict, &queuedefIter);
	while (pDef = (QueueDef*)RefSystem_GetNextReferentFromIterator(&queuedefIter))
	{
		if(pList->eCategory == pDef->eCategory)
		{
			QueueDefRef *pNewRef = StructCreate(parse_QueueDefRef);
			
			SET_HANDLE_FROM_REFERENT(g_hQueueDefDict,pDef,pNewRef->hDef);

			eaPush(&pList->ppChoiceDefs,pNewRef);
		}
	}
}

// Called from aslQueueServerInit.c
void aslQueue_LoadRandomQueueLists(void)
{
	int i;

	for(i=0;i<eaSize(&s_QueueCategories.eaData);i++)
	{
		if(s_QueueCategories.eaData[i]->uRandomActiveCount > 0)
		{
			RandomActiveQueueList *pNewList = StructCreate(parse_RandomActiveQueueList);

			eaPush(&s_RandomActiveQueues.ppLists,pNewList);

			pNewList->eCategory = s_QueueCategories.eaData[i]->eCategory;

			aslQueue_InitRandomQueueList(pNewList);
		}
	}

	aslQueue_InitRandomQueueLists();
}


int aslQueue_PickNextRandomQueue(RandomActiveQueueList *pList, int **peaIllegalIndexs)
{
	int iReturn,i;

	if(ea32Size(peaIllegalIndexs) >= eaSize(&pList->ppChoiceDefs))
	{
		return -1;
	}
	else
	{
		ea32QSort((*peaIllegalIndexs),CmpS32);

		iReturn = randomIntRange(0,eaSize(&pList->ppChoiceDefs)-ea32Size(peaIllegalIndexs)-1);

		for(i=0;i<ea32Size(peaIllegalIndexs);i++)
		{
			if(iReturn >= (*peaIllegalIndexs)[i])
				iReturn++;
			else
				break;
		}
	}
	

	return iReturn;
}

ActiveRandomQueueDef *aslQueue_ActivateRandomQueue(RandomActiveQueueList *pList, int iQueueIndex)
{
	if(pList && iQueueIndex < eaSize(&pList->ppChoiceDefs))
	{
		QueueDef *pDef = GET_REF(pList->ppChoiceDefs[iQueueIndex]->hDef);
		ActiveRandomQueueDef *pNewList = StructCreate(parse_ActiveRandomQueueDef);
		QueueCategoryData *pCatData = queue_GetCategoryData(pList->eCategory);

		SET_HANDLE_FROM_REFERENT(g_hQueueDefDict,pDef,pNewList->hDef);

		pNewList->uTimeExpire = timeSecondsSince2000() + pCatData->uRandomActiveTime;
		pNewList->uInstancesRemaining = pCatData->uRandomActiveInstanceMax;

		eaPush(&pList->ppActiveDefs,pNewList);

		return pNewList;
	}

	return NULL;
}

int aslQueue_getRandomQueueIndex(RandomActiveQueueList *pList, QueueDef *pQueueDef)
{
	int i;

	for(i=0;i<eaSize(&pList->ppChoiceDefs);i++)
	{
		if(GET_REF(pList->ppChoiceDefs[i]->hDef) == pQueueDef)
		{
			return i;
		}
	}

	return -1;
}

void aslQueue_RelayActiveRandomQueues(void)
{
	static char *pchRandomActiveQueues = NULL;
	int i;

	if(!pchRandomActiveQueues)
		estrCreate(&pchRandomActiveQueues);
	else
		estrClear(&pchRandomActiveQueues);

	for(i=0;i<eaSize(&s_RandomActiveQueues.ppLists);i++)
	{
		int n;
		RandomActiveQueueList *pList = s_RandomActiveQueues.ppLists[i];

		for(n=0;n<eaSize(&pList->ppActiveDefs);n++)
		{
			estrConcatf(&pchRandomActiveQueues,"%s,",REF_HANDLE_GET_STRING(pList->ppActiveDefs[n]->hDef));
		}
	}

	RemoteCommand_aslMapManger_RelayRandomActiveQueues(GLOBALTYPE_MAPMANAGER,0,pchRandomActiveQueues);
}

void aslQueue_InitRandomQueueLists(void)
{
	int i;

	for(i=0;i<eaSize(&s_RandomActiveQueues.ppLists);i++)
	{
		U32 uCurrentTime = timeSecondsSince2000();
		RandomActiveQueueList *pList = s_RandomActiveQueues.ppLists[i];
		QueueCategoryData *pCatData = queue_GetCategoryData(pList->eCategory);
		int *eaActiveDefs = NULL;
		U32 uStagerTime = pCatData->uRandomActiveTime ? pCatData->uRandomActiveTime / pCatData->uRandomActiveCount : 0;

		if(eaSize(&pList->ppChoiceDefs) <= (int)pCatData->uRandomActiveCount)
		{
			Errorf("Random Queue Category %s does not have enough queues to fill the active count! (%d)",pCatData->pchName,pCatData->uRandomActiveCount);
			continue;
		}

		while(eaSize(&pList->ppActiveDefs) < (int)pCatData->uRandomActiveCount)
		{
			int iRandomNum;
			ActiveRandomQueueDef *pNewQueue = NULL;

			iRandomNum = aslQueue_PickNextRandomQueue(pList,&eaActiveDefs);

			if(iRandomNum == -1)
				break;

			pNewQueue = aslQueue_ActivateRandomQueue(pList,iRandomNum);

			//On init, offset the times to create staggered random queues
			if(uStagerTime)
				pNewQueue->uTimeExpire = uCurrentTime + (uStagerTime * eaSize(&pList->ppActiveDefs));

			ea32Push(&eaActiveDefs,iRandomNum);
		}

		ea32Destroy(&eaActiveDefs);
	}

	aslQueue_RelayActiveRandomQueues();
}




static int aslQueue_GetMemberWaitingCount(QueueDef *pDef)
{
	int i;
	int iReturn = 0;

	QueueInfo *pQueue = aslQueue_FindQueueByDefLocal(pDef);

	if(!pQueue)
		return 0;

	for(i=0;i<eaSize(&pQueue->eaInstances);i++)
	{
		int m;

		for(m=0;m<eaSize(&pQueue->eaInstances[i]->eaUnorderedMembers);m++)
		{
			if(pQueue->eaInstances[i]->eaUnorderedMembers[m]->eState == PlayerQueueState_InQueue)
			{
				iReturn++;
			}
		}
	}

	return iReturn;
}

void aslQueue_RandomQueuesUpdate(void)
{
	int i;
	bool bSendNewList = false;

	for(i=0;i<eaSize(&s_RandomActiveQueues.ppLists);i++)
	{
		RandomActiveQueueList *pList = s_RandomActiveQueues.ppLists[i];
		QueueCategoryData *pData = queue_GetCategoryData(pList->eCategory);
		int n;
		int *eaActiveIndexs = NULL;
		U32 uCurrentTime = timeSecondsSince2000();
		
		for(n=eaSize(&pList->ppActiveDefs)-1;n>=0;n--)
		{
			ActiveRandomQueueDef *pActiveQueue = pList->ppActiveDefs[n];
			ea32Push(&eaActiveIndexs,aslQueue_getRandomQueueIndex(pList,GET_REF(pActiveQueue->hDef)));
			
			if(pList->ppActiveDefs[n]->uTimeExpire < uCurrentTime
				&& aslQueue_GetMemberWaitingCount(GET_REF(pActiveQueue->hDef)) == 0)
			{
				eaRemove(&pList->ppActiveDefs,n);
				StructDestroy(parse_ActiveRandomQueueDef,pActiveQueue);	
				bSendNewList = true;
			}
		}

		while(eaSize(&pList->ppActiveDefs) < (int)pData->uRandomActiveCount)
		{
			int iRandomNum;

			iRandomNum = aslQueue_PickNextRandomQueue(pList,&eaActiveIndexs);

			if(iRandomNum == -1)
				break;

			aslQueue_ActivateRandomQueue(pList,iRandomNum);

			ea32Push(&eaActiveIndexs,iRandomNum);

			bSendNewList = true;
		}
	}

	if(bSendNewList)
		aslQueue_RelayActiveRandomQueues();
}

bool aslQueue_IsRandomQueueActive(QueueDef *pDef)
{
	QueueCategoryData *pCatData = queue_GetCategoryData(pDef->eCategory);
	int i;

	if(!pCatData || pCatData->uRandomActiveCount <= 0)
		return true;

	for(i=0;i<eaSize(&s_RandomActiveQueues.ppLists);i++)
	{
		if(s_RandomActiveQueues.ppLists[i]->eCategory == pDef->eCategory)
		{
			int n;

			for(n=0;n<eaSize(&s_RandomActiveQueues.ppLists[i]->ppActiveDefs);n++)
			{
				if(GET_REF(s_RandomActiveQueues.ppLists[i]->ppActiveDefs[n]->hDef) == pDef)
					return true;
			}

			return false;
		}
	}

	return false;
}

//  End of RandomQueues
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// QueueUpdate List.
//   Manages s_UpdateList which is set for map, instance, or member data.
//
//  Cleared at the start of aslQueue_Update. Filled by function called therein. Processed at the end of aslQueue_Update

static QUpdateList s_QueueUpdateList = {0};

static QUpdateData* aslQueue_GetQueueUpdateData(ContainerID uiQueueID)
{
	QUpdateData* pData = eaIndexedGetUsingInt(&s_QueueUpdateList.eaQueues, uiQueueID);
	if (!pData)
	{
		pData = StructCreate(parse_QUpdateData);
		pData->uiQueueID = uiQueueID;
		eaIndexedEnable(&s_QueueUpdateList.eaQueues, parse_QUpdateData);
		eaPush(&s_QueueUpdateList.eaQueues, pData);
	}
	return pData;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Instance Update

#define QUEUE_INSTANCE_EXPIRE_SECONDS 3600 //time it takes to destroy the instance when there are members but no maps created
#define QUEUE_INSTANCE_TIMEOUT_SECONDS 300 //time it takes to destroy the instance when there are no members
static bool aslQueue_ShouldCleanupInstance(QueueDef* pDef, QueueInstance* pInstance)
{
	if (pInstance==NULL || pInstance->pParams==NULL)
	{
		return true;
	}
	if (pInstance->pParams->uiOwnerID > 0)		// Indication of private ownership
	{
		if (eaSize(&pInstance->eaUnorderedMembers)==0)
		{
			return true;
		}
	}
	else if (!queue_CheckInstanceParamsValid(NULL, pDef, pInstance->pParams))
	{
		return true;
	}
	else if (!pDef->Settings.bAlwaysCreate)		// It looks like STO runs mostly AlwaysCreate. NW does not.
	{
		U32 uiNow = timeSecondsSince2000();
		if (eaSize(&pInstance->eaUnorderedMembers)==0)
		{
			if (pInstance->uiTimeoutStartTime == 0)
			{
				pInstance->uiTimeoutStartTime = uiNow;
			}
			else if (uiNow > pInstance->uiTimeoutStartTime + QUEUE_INSTANCE_TIMEOUT_SECONDS)
			{
				return true;
			}
		}
		else 
		{
			if (pInstance->uiTimeoutStartTime > 0)
			{
				pInstance->uiTimeoutStartTime = 0;
			}

			if (eaSize(&pInstance->eaNewMaps) < 1 && eaSize(&pInstance->eaMaps) == 0)
			{
				if (pInstance->uiExpireStartTime == 0)
				{
					pInstance->uiExpireStartTime = uiNow;
				}
				else if (uiNow > pInstance->uiExpireStartTime + QUEUE_INSTANCE_EXPIRE_SECONDS)
				{
					return true;
				}
			}
			else
			{
				pInstance->uiExpireStartTime = 0;
			}
		}
	}
	return false;
}


static bool aslQueue_HandleInstanceUpdate(QueueInfo* pQueue, SA_PARAM_NN_VALID QInstanceUpdate* pUpdate)
{
	if (aslQueue_UpdateInstance(pQueue, pUpdate))
	{
		QUpdateData* pData = aslQueue_GetQueueUpdateData(pQueue->iContainerID);
		QGeneralUpdate* pQueueUpdate = StructCreate(parse_QGeneralUpdate);
		pQueueUpdate->pInstanceUpdate = pUpdate;
		eaPush(&pData->eaList, pQueueUpdate);
		return true;
	}
	StructDestroy(parse_QInstanceUpdate, pUpdate);
	return false;
}

static bool aslQueue_InstanceShouldBeOvertime(QueueDef* pQueueDef, QueueInstance* pInstance)
{
	S32 bOvertime = false;
	U32 iCurrentTime = timeSecondsSince2000();
	U32 uMaxTimeToWait = queue_GetMaxTimeToWait(pQueueDef, pInstance);

	// OldestMemberTstamp is set up in aslQueue_MemberStateUpdateInstance during member processing. It is reset to 0 before checking members.
	// So will be zero if there are no members.
	// Track the oldest member in the queue. If we go over our limit, set overTime mode to ease queue number restrictions.
			
	if (uMaxTimeToWait &&
		pInstance->iOldestMemberTstamp > 0 && 
		iCurrentTime > pInstance->iOldestMemberTstamp + uMaxTimeToWait)
	{
		bOvertime = true;
	}
	return(bOvertime);
}


static void aslQueue_InstanceUpdate(QueueInfo* pLocalQueueInfo, QueueDef* pDef)
{
	S32 iInstanceIdx;
	for (iInstanceIdx = eaSize(&pLocalQueueInfo->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
	{
		QueueInstance* pInstance = pLocalQueueInfo->eaInstances[iInstanceIdx];
		QInstanceUpdate* pUpdate = NULL;
		bool bOvertime = aslQueue_InstanceShouldBeOvertime(pDef, pInstance);
		
		if (aslQueue_ShouldCleanupInstance(pDef, pInstance))
		{
			pUpdate = StructCreate(parse_QInstanceUpdate);
			pUpdate->uiInstanceID = pInstance->uiID;
			pUpdate->bRemove = true;
			// We don't care about NewMap or Overtime because we are shutting down
		}
		else if (pInstance->bNewMap && eaSize(&pInstance->eaNewMaps) < 1)
		{
			// If the new map is done loading, clear the bNewMap flag
			pUpdate = StructCreate(parse_QInstanceUpdate);
			pUpdate->uiInstanceID = pInstance->uiID;
			pUpdate->bNewMap = false;
			pUpdate->bOvertime = bOvertime;
		}
		else if ((pInstance->bOvertime && !bOvertime) || (!pInstance->bOvertime && bOvertime))
		{
			pUpdate = StructCreate(parse_QInstanceUpdate);
			pUpdate->uiInstanceID = pInstance->uiID;
			pUpdate->bOvertime = bOvertime;
			pUpdate->bNewMap = pInstance->bNewMap;
		}

		if (pUpdate)
		{
			aslQueue_HandleInstanceUpdate(pLocalQueueInfo, pUpdate);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Member State Change


QMemberUpdate* aslQueue_AddMemberStateChangeEx(SA_PARAM_OP_VALID QMemberUpdate*** peaUpdates,
											   SA_PARAM_NN_VALID QueueInstance* pInstance, 
											   SA_PARAM_NN_VALID QueueMember* pMember, 
											   PlayerQueueState eState,
											   U32 uTeamID,
											   bool bUpdateTeam,
											   S32 iGroupIndex,
											   bool bUpdateGroup,
											   S64 iMapKey,
											   bool bUpdateMap)
{
	QMemberUpdate* pUpdate = StructCreate(parse_QMemberUpdate);
	pUpdate->uiInstanceID = pInstance->uiID;
	pUpdate->uiMemberID = pMember->iEntID;
	pUpdate->eState = eState;
	pUpdate->eLastState = pMember->eState;
	if (bUpdateTeam) {
		pUpdate->uiTeamID = uTeamID;
	} else {
		pUpdate->uiTeamID = pMember->iTeamID;
	}
	pUpdate->bUpdateTeam = bUpdateTeam;
	if (bUpdateGroup) {
		pUpdate->iGroupIndex = iGroupIndex;
	} else {
		pUpdate->iGroupIndex = pMember->iGroupIndex;
	}
	pUpdate->bUpdateGroup = bUpdateGroup;
	if (bUpdateMap) {
		pUpdate->iMapKey = iMapKey;
	} else {
		pUpdate->iMapKey = pMember->iMapKey;
	}
	pUpdate->bUpdateMap = bUpdateMap;
	if (peaUpdates)
	{
		eaPush(peaUpdates, pUpdate);
	}
	return pUpdate;
}

QMemberUpdate* aslQueue_AddMemberStateChange(SA_PARAM_OP_VALID QMemberUpdate*** peaUpdates,
											 SA_PARAM_NN_VALID QueueInstance* pInstance, 
											 SA_PARAM_NN_VALID QueueMember* pMember, 
											 PlayerQueueState eState)
{
	return aslQueue_AddMemberStateChangeEx(peaUpdates, pInstance, pMember, eState, 0, false, -1, false, 0, false);
}

// Well, this is a sad function
static QMemberUpdate* aslQueue_MemberChangeStateEx(SA_PARAM_NN_VALID QueueInfo* pQueue, 
												   SA_PARAM_NN_VALID QueueInstance* pInstance, 
												   SA_PARAM_NN_VALID QueueMember* pMember, 
												   PlayerQueueState eState,
												   U32 uTeamID, bool bUpdateTeam,
												   S32 iGroupIndex, bool bUpdateGroup,
												   S64 iMapKey, bool bUpdateMap)
{
	QGeneralUpdate* pQueueUpdate;
	QUpdateData* pData = aslQueue_GetQueueUpdateData(pQueue->iContainerID);
	QMemberUpdate* pUpdate = aslQueue_AddMemberStateChangeEx(NULL, 
															 pInstance, 
															 pMember, 
															 eState, 
															 uTeamID, 
															 bUpdateTeam,
															 iGroupIndex,
															 bUpdateGroup,
															 iMapKey,
															 bUpdateMap);
	if (!aslQueue_UpdateMember(pQueue, pUpdate))
	{
		StructDestroy(parse_QMemberUpdate, pUpdate);
		return NULL;
	}
	pQueueUpdate = StructCreate(parse_QGeneralUpdate);
	pQueueUpdate->pMemberUpdate = pUpdate;
	eaPush(&pData->eaList, pQueueUpdate);
	return pUpdate;
}

static bool aslQueue_MemberChangeState(SA_PARAM_NN_VALID QueueInfo* pQueue, 
									   SA_PARAM_NN_VALID QueueInstance* pInstance, 
									   SA_PARAM_NN_VALID QueueMember* pMember, 
									   PlayerQueueState eState,
									   const char* pchMessageKey,
									   bool bWarning)
{
	QMemberUpdate* pUpdate;
	pUpdate = aslQueue_MemberChangeStateEx(pQueue, pInstance, pMember, eState, 0, false, -1, false, 0, false);

	if (pUpdate && pchMessageKey)
	{
		pUpdate->pchMessageKey = StructAllocString(pchMessageKey);
		pUpdate->bWarning = bWarning;
	}
	return !!pUpdate;
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Map update



static U32 aslQueue_MapGetAverageWaitTime(SA_PARAM_NN_VALID QueueInstance* pInstance, 
										  SA_PARAM_NN_VALID QueueMatch* pMatch,
										  QueueMapState eState,
										  U32* puMapWait)
{
	if (!SAFE_MEMBER2(pInstance, pParams, uiOwnerID) &&
		(eState == kQueueMapState_Launched || eState == kQueueMapState_LaunchCountdown))
	{
		S32 iHistoryIdx;
		U32 uAvgWait = 0;
		for (iHistoryIdx = ea32Size(&pInstance->piHistoricalWaits)-1; iHistoryIdx >= 0; iHistoryIdx--)
		{
			uAvgWait += pInstance->piHistoricalWaits[iHistoryIdx];
		}
		(*puMapWait) = queue_Match_GetAverageWaitTime(pInstance, pMatch);
		return (uAvgWait+(*puMapWait)) / (ea32Size(&pInstance->piHistoricalWaits)+1);
	}
	return 0;
}


static bool aslQueue_MapUpdate(SA_PARAM_NN_VALID QueueInfo* pQueue, 
										 SA_PARAM_NN_VALID QueueInstance* pInstance, 
										 SA_PARAM_NN_VALID QueueMap* pMap,
										 SA_PARAM_OP_VALID QueueMatch* pMatch,
										 QueueMapState eState,
										 const char* pchReason)
{
	QGeneralUpdate* pQueueUpdate;
	QUpdateData* pData = aslQueue_GetQueueUpdateData(pQueue->iContainerID);
	QMapUpdate* pUpdate = StructCreate(parse_QMapUpdate);

	
	pUpdate->uiInstanceID = pInstance->uiID;
	pUpdate->iMapKey = pMap->iMapKey;
	pUpdate->eLastState = pMap->eMapState;
	pUpdate->eState = eState;
	pUpdate->pMatch = StructClone(parse_QueueMatch, pMatch);
	

	pUpdate->pchReason = StructAllocString(pchReason);
	
	if (pMatch)
	{
		pUpdate->uAverageWaitTime = aslQueue_MapGetAverageWaitTime(pInstance, 
																   pMatch, 
																   eState, 
																   &pUpdate->uWaitTime);
	}
	// Update the map in local data through the _trh_
	//   Should only fail if there is no instance or no map.
	if (!aslQueue_UpdateMap(pQueue, pUpdate))
	{
		StructDestroy(parse_QMapUpdate, pUpdate);
		return false;
	}

	// Queue up the actual update for later transacting.
	pQueueUpdate = StructCreate(parse_QGeneralUpdate);
	pQueueUpdate->pMapUpdate = pUpdate;
	eaPush(&pData->eaList, pQueueUpdate);

	return true;
}


//  Keep track of the last time an offer changed so we can time out on no activity
static void aslQueue_MapUpdateOfferValidationTime(QueueMap* pMap, QueueMember*** peaReadyMembers)
{
	U32 uCurrentTime = timeSecondsSince2000();
	S32 iMemberIdx;
	
	if (!pMap->uOfferValidationStartTime)
	{
		ea32Clear(&pMap->eaOfferValidationMemberIDs);
		pMap->uOfferValidationStartTime = uCurrentTime;
	}
	else if (pMap->uOfferValidationStartTime + g_QueueConfig.uCheckOffersResponseTimeout < uCurrentTime)
	{
		for (iMemberIdx = eaSize(peaReadyMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMember* pMember = (*peaReadyMembers)[iMemberIdx];
			if (ea32Find(&pMap->eaOfferValidationMemberIDs, pMember->iEntID) < 0)
			{
				break;	
			}
		}
		// Don't allow a member to requeue multiple times to delay the matching process
		if (iMemberIdx < 0)
		{
			return;
		}
		pMap->uOfferValidationStartTime = uCurrentTime - g_QueueConfig.uCheckOffersResponseTimeout;
	}

	for (iMemberIdx = eaSize(peaReadyMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		ea32PushUnique(&pMap->eaOfferValidationMemberIDs, (*peaReadyMembers)[iMemberIdx]->iEntID);
	}
}

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  AutoBalance.   None of this should be called unless pDef->MapRules.bDisableAutoBalance is turned off. It defaults to on.
//    As of 11Feb2013 there are just a handful of maps in STO that use this.


static int s_iAutoBalanceSlop = 0;
AUTO_CMD_INT(s_iAutoBalanceSlop, queue_SetAutoBalanceSlop);


static bool aslQueue_AutoBalance_TransferMember(SA_PARAM_NN_VALID QueueInstance* pInstance,
												SA_PARAM_NN_VALID QueueMatch* pMatch, 
												SA_PARAM_NN_VALID QueueGroup* pSrcGroup,
												SA_PARAM_OP_VALID QueueGroup* pDstGroup,
												SA_PARAM_NN_VALID QueueMatchMember* pMember,
												SA_PARAM_NN_VALID QueueDef* pDef)
{
	bool bFailed = false;
	//  Failure if the GroupIndex can't be found in the match, or there is no uEntID
	if (pDstGroup && !queue_Match_AddMember(pMatch, pDstGroup->iGroupIndex, pMember->uEntID, pMember->uTeamID))
	{
		bFailed = true;
	}
	//  Failure if the Entity is not in the group
	if (!queue_Match_RemoveMemberFromGroup(pMatch, pSrcGroup->iGroupIndex, pMember->uEntID))
	{
		bFailed = true;
	}
	if (bFailed)
	{
		char pchError[512];
		sprintf(pchError, "auto-balance failed to transfer member (%d) from group (%d) to group (%d) in queue (%s)",
			pMember->uEntID, pSrcGroup->iGroupIndex, SAFE_MEMBER(pDstGroup, iGroupIndex), pDef->pchName);
		aslQueue_Log(pDef, pInstance, pchError);
		return false;
	}
	return true;
}

static S32 aslQueue_AutoBalance_TransferTeam(SA_PARAM_NN_VALID QueueInstance* pInstance,
											 SA_PARAM_NN_VALID QueueMatch* pMatch, 
											 SA_PARAM_NN_VALID QueueGroup* pSrcGroup,
											 SA_PARAM_OP_VALID QueueGroup* pDstGroup,
											 SA_PARAM_NN_VALID QueueMember* pTeamMember,
											 SA_PARAM_NN_VALID QueueDef* pDef,
											 S32 iGoalSize,
											 S32 iStartIndex)
{
	S32 iFindemberIdx, iTeamTransferCount = 0;
	S32 iTeamSize = MAX(pTeamMember->iTeamSize, 1);
	
	//Can't move any team members if there are team members in map
	if (ea32Find(&pSrcGroup->puiInMapTeamIDs, pTeamMember->iTeamID) >= 0)
	{
		return 0;
	}
	if (pDstGroup && iGoalSize >= 0 && iTeamSize + pDstGroup->iGroupSize > iGoalSize)
	{
		// If moving the team will exceed the goal size, don't do it.
		return 0;
	}
	if (iStartIndex < 0)
	{
		iStartIndex = eaSize(&pSrcGroup->eaMembers)-1;
	}
	for (iFindemberIdx = iStartIndex; iFindemberIdx >= 0; iFindemberIdx--)
	{
		QueueMatchMember* pMember = pSrcGroup->eaMembers[iFindemberIdx];
		
		if (pMember->uTeamID != pTeamMember->iTeamID)
		{
			continue;
		}
		if (!aslQueue_AutoBalance_TransferMember(pInstance,pMatch,pSrcGroup,pDstGroup,pMember,pDef))
		{
			break;
		}
		iTeamTransferCount++;
	}
	return iTeamTransferCount;
}


static bool aslQueue_AutoBalance_FindGroupForMemberUsingSwap(QueueInstance *pInstance,
												 QueueMatch *pMatch,
												 QueueDef *pDef,
												 S32 iTeamSize,
												 S32 iSrcGroupIndex,
												 bool bOvertime)
{
	QueueGroup* pSrcGroup = pMatch->eaGroups[iSrcGroupIndex];
	S32 iGroupIdx, iMemberIdx, iSrcMax = queue_GetGroupMaxSize(pSrcGroup->pGroupDef, bOvertime);

	for (iGroupIdx = 0; iGroupIdx < eaSize(&pMatch->eaGroups); iGroupIdx++)
	{
		QueueGroup* pDstGroup = pMatch->eaGroups[iGroupIdx];
		S32 iDstMax = queue_GetGroupMaxSize(pDstGroup->pGroupDef, bOvertime);
		if (iGroupIdx == iSrcGroupIndex || pDstGroup->iGroupSize > iDstMax)		// What if the groupsize is equal to the max?
		{
			continue;
		}

		for (iMemberIdx = eaSize(&pSrcGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMatchMember* pMatchMember = pSrcGroup->eaMembers[iMemberIdx];
			QueueMember* pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMatchMember->uEntID);			
			if (!pMember)
			{
				continue;
			}
			if (pMember->iTeamID && pMember->iTeamSize > 1 && !pDef->Settings.bSplitTeams)
			{
				aslQueue_AutoBalance_TransferTeam(pInstance, pMatch, pSrcGroup, pDstGroup, pMember, pDef, iDstMax, iMemberIdx);
			}
			else
			{
				if (pDstGroup->iGroupSize >= iDstMax)
				{
					continue;
				}
				aslQueue_AutoBalance_TransferMember(pInstance, pMatch, pSrcGroup, pDstGroup, pMatchMember, pDef); 
			}

			if (iSrcMax - pSrcGroup->iGroupSize >= iTeamSize)
			{
				return true;
			}
		}
	}
	return false;
}


static bool aslQueue_AutoBalance_TransferGroup(SA_PARAM_NN_VALID QueueInstance* pInstance,
											   SA_PARAM_NN_VALID QueueMatch* pMatch,
											   SA_PARAM_NN_VALID QueueGroup* pSrcGroup,
											   SA_PARAM_OP_VALID QueueGroup* pDstGroup,
											   SA_PARAM_NN_VALID QueueDef* pDef,
											   S32 iGoalSize,
											   bool bFailOnFirstRemovalError,
											   bool* pbTransferFailure)
{
	S32 iMemberIdx;
	bool bFailed = false;
	for (iMemberIdx = eaSize(&pSrcGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMatchMember* pMember = pSrcGroup->eaMembers[iMemberIdx];
		QueueMember* pQueueMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMember->uEntID);		
		S32 iTeamSize = MAX(SAFE_MEMBER(pQueueMember, iTeamSize), 1);

		if (!pQueueMember)
		{
			continue;
		}
		if (pMember->uTeamID && !pDef->Settings.bSplitTeams && iTeamSize > 1)
		{
			S32 iTeamTransferCount;
			iTeamTransferCount = aslQueue_AutoBalance_TransferTeam(pInstance,
																   pMatch,
																   pSrcGroup,
																   pDstGroup,
																   pQueueMember,
																   pDef,
																   iGoalSize,
																   iMemberIdx);
			if ((bFailOnFirstRemovalError || iTeamTransferCount) && iTeamTransferCount != iTeamSize)
			{
				bFailed = true;
				break;
			}
			if (iTeamTransferCount)
			{
				iMemberIdx -= (iTeamTransferCount-1);
			}
		}
		else
		{
			if (!aslQueue_AutoBalance_TransferMember(pInstance,pMatch,pSrcGroup,pDstGroup,pMember,pDef))
			{
				if (bFailOnFirstRemovalError)
				{
					break;
				}
			}
		}
		if (pDstGroup && pDstGroup->iGroupSize == iGoalSize)
		{
			return true;
		}
		if (pSrcGroup->iGroupSize == iGoalSize)
		{
			if (!pDstGroup)
			{
				return true;
			}
			break;
		}
	}
	if (bFailed && pbTransferFailure)
	{
		(*pbTransferFailure) = true;
	}
	return false;
}

static bool aslQueue_AutoBalance_TransferMembers(SA_PARAM_NN_VALID QueueInstance* pInstance,
												 SA_PARAM_NN_VALID QueueMatch* pMatch, 
												 SA_PARAM_NN_VALID QueueGroup* pDstGroup,
												 SA_PARAM_NN_VALID QueueDef* pDef,
												 S32 iGoalSize,
												 bool* pbTransferFailure)
{
	S32 iGroupIdx;
	bool bBalanced, bFailed = false;
	for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
		if (pGroup == pDstGroup || pGroup->iGroupSize <= iGoalSize)
		{
			continue;
		}
		bBalanced = aslQueue_AutoBalance_TransferGroup(pInstance,
													   pMatch,
													   pGroup,
													   pDstGroup,
													   pDef,
													   iGoalSize,
													   !pDstGroup,
													   &bFailed);
		if (bFailed)
		{
			break;
		}
		if (bBalanced)
		{
			return true;
		}
	}
	if (bFailed && pbTransferFailure)
	{
		(*pbTransferFailure) = true;
	}
	return false;
}

static bool aslQueue_AutoBalance_GetBestMemberToRemove(SA_PARAM_NN_VALID QueueInstance* pInstance,
													   SA_PARAM_NN_VALID QueueMatch* pMatch, 
													   bool bLazyBalance,
													   S32* piGroupIndex,
													   S32* piMemberIndex)
{
	S32 iGroupIdx, iMemberIdx;
	S32 iSmallestTeamSize = TEAM_MAX_SIZE;
	U32 uNewestTimestamp = 0;
	for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
		for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMatchMember* pMatchMember = pGroup->eaMembers[iMemberIdx];
			QueueMember* pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMatchMember->uEntID);			
			S32 iTeamSize = MAX(pMember->iTeamSize, 1);
			bool bBest = false;

			if (!pMember)
			{
				continue;
			}
			if (bLazyBalance)
			{
				if (iTeamSize < iSmallestTeamSize || 
					(iTeamSize == iSmallestTeamSize && pMember->iQueueEnteredTime > uNewestTimestamp))
				{
					bBest = true;
				}
			}
			else if (pMember->iQueueEnteredTime > uNewestTimestamp)
			{
				bBest = true;
			}

			if (bBest)
			{
				uNewestTimestamp = pMember->iQueueEnteredTime;
				iSmallestTeamSize = iTeamSize;
				(*piGroupIndex) = iGroupIdx;
				(*piMemberIndex) = iMemberIdx;
			}
		}
	}
	return (uNewestTimestamp > 0);
}

// Returns true if the added members make the match more balanced
static bool aslQueue_AutoBalance_IsMatchMoreBalanced(QueueMatch* pMatch)
{
	S32 iGroupIdxA, iGroupIdxB; 
	for (iGroupIdxA = eaSize(&pMatch->eaGroups)-1; iGroupIdxA >= 0; iGroupIdxA--)
	{
		for (iGroupIdxB = iGroupIdxA-1; iGroupIdxB >= 0; iGroupIdxB--)
		{
			QueueGroup* pGroupA = pMatch->eaGroups[iGroupIdxA];
			QueueGroup* pGroupB = pMatch->eaGroups[iGroupIdxB];
			S32 iGroupASizeNew = pGroupA->iGroupSize;
			S32 iGroupBSizeNew = pGroupB->iGroupSize;
			S32 iGroupASizeOld = MAX(iGroupASizeNew - eaSize(&pGroupA->eaMembers), 0);
			S32 iGroupBSizeOld = MAX(iGroupBSizeNew - eaSize(&pGroupB->eaMembers), 0);
			S32 iNewMin = MIN(iGroupASizeNew, iGroupBSizeNew);
			S32 iNewMax = MAX(iGroupASizeNew, iGroupBSizeNew);
			S32 iOldMin = MIN(iGroupASizeOld, iGroupBSizeOld);
			S32 iOldMax = MAX(iGroupASizeOld, iGroupBSizeOld);

			if (!iNewMax || !iOldMax || iNewMin/(F32)iNewMax < iOldMin/(F32)iOldMax)
			{
				return false;
			}
		}
	}
	return true;
}

//Simple auto-balancing for queue matches
static bool aslQueue_AutoBalanceMatch(SA_PARAM_NN_VALID QueueInstance* pInstance,
									  SA_PARAM_NN_VALID QueueMatch* pMatch, 
									  SA_PARAM_NN_VALID QueueDef* pDef,
									  bool bLazyBalance)
{
	S32 iGroupIdx, iMemberIdx, iGroupCount = eaSize(&pMatch->eaGroups);
	S32 iBalanceSize = -1;
	S32 iRetryCount = 1000;
	bool bSameAffiliationReq = true;

	// Find the group size to balance against and figure out if we need same allegiance. (Balance size is smallest group?)
	//   WOLF[11Feb13]  This is highly suspicious. Perhaps we only call this in certain circumstances, but it seems weird
	//   to balance down to the smallest group. 2 groups of five and a group of 1 or 2 will result in the fives being demolished.
	//   And if there are teams in those large groups, we're going to not be able to do anything anyway.
	for (iGroupIdx = 0; iGroupIdx < iGroupCount; iGroupIdx++)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
		if (iBalanceSize >= 0)
		{
			iBalanceSize = MIN(iBalanceSize, pGroup->iGroupSize);
		}
		else
		{
			iBalanceSize = pGroup->iGroupSize;
		}
		if (iGroupIdx > 0)
		{
			QueueGroup* pLastGroup = pMatch->eaGroups[iGroupIdx-1];
			if (pLastGroup->pGroupDef->pchAffiliation != pGroup->pGroupDef->pchAffiliation)
			{
				bSameAffiliationReq = false;
			}
		}
	}

	// Actual balancing
	if (bSameAffiliationReq)
	{
		bool bRetry = false;
		while (--iRetryCount > 0)
		{
			U32 uMin = queue_QueueGetMinPlayersEx(pDef, pInstance->bOvertime, true, queue_InstanceIsPrivate(pInstance));

			//Remove players until the total number of players is evenly divisible by the number of groups
			while (pMatch->iMatchSize > uMin && (bRetry || (pMatch->iMatchSize % iGroupCount) != 0))
			{
				bRetry = false;
				if (aslQueue_AutoBalance_GetBestMemberToRemove(pInstance, pMatch, bLazyBalance, &iGroupIdx, &iMemberIdx))
				{
					QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
					QueueMatchMember* pMatchMember = pGroup->eaMembers[iMemberIdx];
					QueueMember* pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMatchMember->uEntID);					
					devassert(pMember);
					if (pMember->iTeamID && pMember->iTeamSize > 1 && !pDef->Settings.bSplitTeams)
					{
						// Remove all members on this player's team
						if(aslQueue_AutoBalance_TransferTeam(pInstance, pMatch, pGroup, NULL, pMember, pDef, -1, -1) < 1)
						{
							// didn't remove anyone
							return false;
						}
					}
					else
					{
						// Remove the individual member
						if(!aslQueue_AutoBalance_TransferMember(pInstance, pMatch, pGroup, NULL, pMatchMember, pDef))
						{
							// didn't remove anyone
							return false;
						}
					}
				}
				else
				{
					return false;
				}
			}
			// Transfer members from group to group to get even member counts in each
			if (!bRetry && pMatch->iMatchSize >= uMin && (pMatch->iMatchSize % iGroupCount) == 0)
			{
				S32 iGoalSize = pMatch->iMatchSize / iGroupCount;
				for (iGroupIdx = iGroupCount-1; iGroupIdx >= 0; iGroupIdx--)
				{
					QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
					if (pGroup->iGroupSize < iGoalSize)
					{
						bool bTransferFailure = false;
						if (!aslQueue_AutoBalance_TransferMembers(pInstance,
																  pMatch,
																  pGroup,
																  pDef,
																  iGoalSize,
																  &bTransferFailure))
						{
							if (bTransferFailure)
							{
								return false;
							}
							break;
						}
					}
				}
				if (iGroupIdx < 0)
				{
					return true; // Successfully transferred enough members to balance the teams
				}
				else
				{
					bRetry = true;
				}
			}
			else
			{
				break;
			}
		}
	}
	else
	{
		while (--iRetryCount > 0)
		{
			S32 iGoalSize = iBalanceSize;
			// Remove players from each group down to the balance size - if possible
			for (iGroupIdx = iGroupCount-1; iGroupIdx >= 0; iGroupIdx--)
			{
				QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
				if (pGroup->iGroupSize > iGoalSize)
				{
					if (!aslQueue_AutoBalance_TransferGroup(pInstance, 
															pMatch, 
															pGroup, 
															NULL, 
															pDef,
															iBalanceSize,
															!bLazyBalance,
															NULL))
					{
						iBalanceSize = MIN(iBalanceSize, pGroup->iGroupSize);
						break;
					}
				}
			}
			if (iGroupIdx < 0)
			{
				return true; // Successfully removed enough members to balance the teams
			}
			if (iGoalSize == iBalanceSize)
			{
				break;
			}
		}
	}

	if (bLazyBalance)
	{
		return aslQueue_AutoBalance_IsMatchMoreBalanced(pMatch);
	}
	if (s_iAutoBalanceSlop > 0) // For debugging
	{
		S32 iMin = INT_MAX, iMax = -1;
		// Check to see if the groups are within an acceptable tolerance
		for (iGroupIdx = iGroupCount-1; iGroupIdx >= 0; iGroupIdx--)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];

			iMin = MIN(iMin, pGroup->iGroupSize);
			iMax = MAX(iMax, pGroup->iGroupSize);
		}
		if (iMax - iMin <= s_iAutoBalanceSlop)
		{
			return true;
		}
	}
	// Auto-balancing failed
	return false;
}

//Simple auto-balancing for queue matches. Test if balanced
static bool aslQueue_AutoBalanceMatchIsBalanced(SA_PARAM_NN_VALID QueueMatch* pMatch, 
											  SA_PARAM_NN_VALID QueueDef* pDef)
{
	S32 iGroupIdx, iGroupCount = eaSize(&pMatch->eaGroups);

	// Automatically succeed if the match only requires one player (or no players)
	if (queue_QueueGetMaxPlayers(pDef, false) <= 1)
	{
		return true;
	}
	// Find the group size to balance against
	for (iGroupIdx = 1; iGroupIdx < iGroupCount; iGroupIdx++)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
		if (pGroup->iGroupSize != pMatch->eaGroups[0]->iGroupSize)
		{
			return(false);
		}
	}
	return true; // Teams are already balanced
}



void aslQueue_AutoBalance_RemoveUngroupedMembers(QueueInfo *pQueue, 
								 QueueInstance* pInstance, 
								 QueueMap* pMap, 
								 QueueDef* pQueueDef,
									 QueueMatch* pMatch,
									bool bAllowInQueueUnmatched)

{
	// If the match was auto-balanced, make sure to remove members that are no longer part of any groups
	if (!pQueueDef->MapRules.bDisableAutoBalance)
	{
		S32 iMemberIdx, iMemberCount = eaSize(&pInstance->eaUnorderedMembers);
		QueueMember** eaUnmatchedMembers = NULL;
		const char* pchMsgKey = QUEUE_OFFER_VALIDATE_TIMEOUT_MESG;

		for (iMemberIdx = 0; iMemberIdx < iMemberCount; iMemberIdx++)
		{
			QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			if (pMember->iMapKey != pMap->iMapKey)
			{
				continue;
			}
			if (pMember->eState == PlayerQueueState_Accepted)
			{
				if (queue_Match_FindMember(pMatch, pMember->iEntID, NULL) >= 0)
				{
					continue;
				}
				if (!bAllowInQueueUnmatched)
				{
					eaPush(&eaUnmatchedMembers, pMember);
					continue;
				}
			}
			else if (pMember->eState != PlayerQueueState_Offered)
			{
				continue;
			}

			aslQueue_MemberChangeState(pQueue, pInstance, pMember, PlayerQueueState_InQueue, pchMsgKey, true);
		}
		if (eaSize(&eaUnmatchedMembers) > 0)
		{
			aslQueue_MapUpdateOfferValidationTime(pMap, &eaUnmatchedMembers);
		}
		eaDestroy(&eaUnmatchedMembers);
	}
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  MatchMember state and updating utilities


//Once the members have been matched and the best match chosen (coming soon?)
// then remove the members from the list of ready members
static void aslQueue_MatchMembersRemoveFromReadyList(QueueMatch *pMatch, QueueMember ***peaReadyMembers)
{
	S32 iGroupIdx, iNumGroups = eaSize(&pMatch->eaGroups);
	S32 iMemberIdx;

	for (iGroupIdx=0; iGroupIdx<iNumGroups; iGroupIdx++)
	{
		QueueGroup *pGroup = eaGet(&pMatch->eaGroups, iGroupIdx);
		S32 iGroupMemberIdx;
		//Have to preserve order of the members
		for (iGroupMemberIdx = 0; iGroupMemberIdx < eaSize(&pGroup->eaMembers); iGroupMemberIdx++)
		{
			for (iMemberIdx = eaSize(peaReadyMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				if ((*peaReadyMembers)[iMemberIdx]->iEntID == pGroup->eaMembers[iGroupMemberIdx]->uEntID)
				{
					eaRemove(peaReadyMembers, iMemberIdx);
				}
			}
		}
	}
}

static void aslQueue_MatchMembersSetToCountdown(QueueInfo* pQueue,
										  QueueInstance* pInstance, 
												QueueMatch* pMatch)
{
	S32 iGroupIdx, iMemberIdx;
	for (iGroupIdx = eaSize(&pMatch->eaGroups)-1; iGroupIdx >= 0; iGroupIdx--)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
		for (iMemberIdx = eaSize(&pGroup->eaMembers)-1; iMemberIdx >= 0; iMemberIdx--)
		{
			QueueMatchMember* pMatchMember = pGroup->eaMembers[iMemberIdx];
			QueueMember* pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pMatchMember->uEntID);			
			if (pMember)
			{
				aslQueue_MemberChangeState(pQueue, pInstance, pMember, PlayerQueueState_Countdown, NULL, false);
			}
		}
	}
}

// We are offering an instance join to a match member. The member state
//   may change to Accepted, Offered, or Countdown based on various criteria.
static void aslQueue_MatchMembersMakeOffer(QueueInfo* pQueue,
										QueueInstance* pInstance,
										S64 iMapKey,
										QueueMatch* pMatch,										   
										   QueueMapState eMapState)
{
	PlayerQueueState eState = PlayerQueueState_None;
	S32 iGroupIndex, iMemberIdx;
	S32 iNumGroups = pMatch ? eaSize(&pMatch->eaGroups) : 0;

	bool bAutoAcceptOffers = (pQueue->bAutoFill || pInstance->bAutoFill);
	
	// do not bAutoAcceptOffers if the queueDef wants to check offers before map launch
	QueueDef *pDef = GET_REF(pQueue->hDef);
	if (pDef && pDef->MapSettings.bCheckOffersBeforeMapLaunch)
	{
		 bAutoAcceptOffers = false;
	}

	for (iGroupIndex = 0; iGroupIndex < iNumGroups; iGroupIndex++)
	{
		QueueGroup* pGroup = pMatch->eaGroups[iGroupIndex];
		
		for (iMemberIdx = 0; iMemberIdx < eaSize(&pGroup->eaMembers); iMemberIdx++)
		{
			U32 uEntID = pGroup->eaMembers[iMemberIdx]->uEntID;
			QueueMember *pMember = queue_FindPlayerInInstance(pInstance, uEntID);
				
			// For each member of the group, determine which player state to set based on the map state
			if (pMember)
			{
				if (eMapState == kQueueMapState_LaunchCountdown ||

					// uiOwnerID indicates a pricate owner. We want to be countdown for private maps?
					(SAFE_MEMBER(pInstance->pParams, uiOwnerID) && 
					 (eMapState == kQueueMapState_Launched || 
					  eMapState == kQueueMapState_Active))
					)
				{
					eState = PlayerQueueState_Countdown;
				}
				else if ((pMember->eJoinFlags & kQueueJoinFlags_AutoAcceptOffers) || bAutoAcceptOffers)
				{
					eState = PlayerQueueState_Accepted;
				}
				else
				{
					eState = PlayerQueueState_Offered;
				}
				// Update the member's state, group, and map ID
				aslQueue_MemberChangeStateEx(pQueue, 
											 pInstance, 
											 pMember, 
											 eState, 
											 0, 
											 false,
											 pGroup->iGroupIndex, 
											 true,
											 iMapKey, 
											 true);
			}
		}
	}
}




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Placement/Matchmaking. Shared calls


static S32 aslQueue_AddTeamToGroup(QueueMatch *pMatch, 
								   QueueGroup *pGroup, 
								   QueueMember *pMember, 
								   SA_PARAM_NN_VALID QueueMember ***peaReadyMembers)
{
	S32 iMemberIdx, iNumMembers = eaSize(peaReadyMembers);
	S32 iMembersAdded = 0;
	for (iMemberIdx = iNumMembers-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMember *pTeamMember = eaGet(peaReadyMembers, iMemberIdx);
		if (pTeamMember->iTeamID == pMember->iTeamID)
		{
			queue_Match_AddMemberToGroup(pMatch, pGroup, pTeamMember->iEntID, pTeamMember->iTeamID, pTeamMember->iGroupRole, pTeamMember->iGroupClass);
			
			//If it's not me, remove them so they don't get re-matched.  Me doesn't get removed. We just skip over it in the member loop in AttemptMatch
			if (pMember->iEntID != pTeamMember->iEntID)
			{
				eaRemove(peaReadyMembers, iMemberIdx);
			}
			iMembersAdded++;
		}
	}
	return iMembersAdded;
}


////////////////////////////////////////////////
//
// Role/Class Smart grouping.
//    Stubbed into game-specific files.
//

/// This function is solely to determine if it makes sense to add pMember (and its team) to the existing pGroup from a class/role balance perspective.
//  It returns either true or false if it thinks it's a good idea or not.

static bool aslQueue_IsSmartGroupToAddTo(QueueDef* pQueueDef, QueueGroup* pGroup, int iGroupMaxSize, QueueMember* pMember, int iTeamSize,
														SA_PARAM_NN_VALID QueueMember ***peaReadyMembers)
{
	if (g_QueueConfig.bRoleClassSmartGroups)
	{
		if (gConf.bQueueSmartGroupNNO)
		{
			return(NNO_aslQueue_IsSmartGroupToAddTo(pQueueDef, pGroup, iGroupMaxSize, pMember, iTeamSize, peaReadyMembers));
		}
		else
		{
			// Other games
			return(true);
		}
	}
	else
	{
		// No smart grouping
		return(true);
	}
}

//////////////////////////////////////////////////////
	
static S32 aslQueue_FindGroupForMember(QueueInstance *pInstance,
									   QueueMatch *pMatch,
									   QueueMember *pMember,
									   QueueDef *pDef,
									   S32 iTeamSize,
									   bool bOvertime,
									   SA_PARAM_NN_VALID QueueMember ***peaReadyMembers)
{
	S32 iGroupIdx;
	S32 iChosenGroup = -1;
	S32 iChosenGroupSize = INT_MAX;
	S32 iChosenLimboCount = -1;
	S32 iNumGroups = eaSize(&pMatch->eaGroups);
	
	if (pMember->iTeamID)
	{
		// If this player has a team, check to see if there any teammates already in-map
		//   (should only be for split teams, because otherwise we place the whole team at once)
		for (iGroupIdx = 0; iGroupIdx < iNumGroups; iGroupIdx++)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];

			if (ea32Find(&pGroup->puiInMapTeamIDs, pMember->iTeamID) >= 0)
			{
				// If the player's team is on the map, try to match them in the same group
				S32 iMax = queue_GetGroupMaxSize(pGroup->pGroupDef, bOvertime);
				if (pGroup->iGroupSize + iTeamSize <= iMax)
				{
					iChosenGroup = iGroupIdx;
				}
				break;
			}
		}
	}
	if (iChosenGroup == -1)
	{
		// We now know how many offers have been made in each group.
		// Find a group that has fewer than its minimum number of players
		for (iGroupIdx = 0; iGroupIdx < iNumGroups; iGroupIdx++)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			QueueGroupDef* pGroupDef = pGroup->pGroupDef;
			S32 iMin = queue_GetGroupMinSizeEx(pGroupDef, bOvertime, false, queue_InstanceIsPrivate(pInstance));
			S32 iMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);

			if (pGroupDef->pchAffiliation && stricmp(pMember->pchAffiliation,pGroupDef->pchAffiliation)!=0)
				continue;

			if ((pGroup->iGroupSize < iChosenGroupSize || 
				(pGroup->iGroupSize == iChosenGroupSize && pGroup->iLimboCount > iChosenLimboCount)) && 
				pGroup->iGroupSize + iTeamSize <= iMin)
			{
				if (aslQueue_IsSmartGroupToAddTo(pDef, pGroup, iMax, pMember, iTeamSize, peaReadyMembers))
				{
					iChosenGroup = iGroupIdx;
					iChosenGroupSize = pGroup->iGroupSize;
					iChosenLimboCount = pGroup->iLimboCount;
				}
			}
		}
	}

	if (iChosenGroup == -1)
	{
		// All groups have at least their minimum number.  Find a group that is below its maximum
		for (iGroupIdx = 0; iGroupIdx < iNumGroups; iGroupIdx++)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			QueueGroupDef* pGroupDef = pGroup->pGroupDef;
			S32 iMax = queue_GetGroupMaxSize(pGroupDef, bOvertime);
			
			if (pGroupDef->pchAffiliation && stricmp(pMember->pchAffiliation,pGroupDef->pchAffiliation)!=0)
				continue;

			if ((pGroup->iGroupSize < iChosenGroupSize || 
				(pGroup->iGroupSize == iChosenGroupSize && pGroup->iLimboCount > iChosenLimboCount)) &&
				pGroup->iGroupSize + iTeamSize <= iMax)
			{
				if (aslQueue_IsSmartGroupToAddTo(pDef, pGroup, iMax, pMember, iTeamSize, peaReadyMembers))
				{
					iChosenGroup = iGroupIdx;
					iChosenGroupSize = pGroup->iGroupSize;
					iChosenLimboCount = pGroup->iLimboCount;
				}
			}
		}
	}

	if (iChosenGroup == -1 && !pDef->MapRules.bDisableAutoBalance)
	{
		// If auto-balance is on, move teams/individuals around between groups (if the allegiances match) to try to make room
		bool bSameAffiliation = true;
		for (iGroupIdx = 1; iGroupIdx < iNumGroups; iGroupIdx++)
		{
			QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
			QueueGroup* pLastGroup = pMatch->eaGroups[iGroupIdx-1];
			if (pLastGroup->pGroupDef->pchAffiliation != pGroup->pGroupDef->pchAffiliation)
			{
				bSameAffiliation = false;
				break;
			}
		}
		if (bSameAffiliation && 
			(U32)iTeamSize + pMatch->iMatchSize <= queue_QueueGetMaxPlayers(pDef, bOvertime) )
		{
			// If still no group is found and the allegiance reqs are the same across groups, try swapping
			for (iGroupIdx = 0; iGroupIdx < iNumGroups; iGroupIdx++)
			{
				QueueGroup* pGroup = pMatch->eaGroups[iGroupIdx];
				if (aslQueue_AutoBalance_FindGroupForMemberUsingSwap(pInstance, 
														 pMatch, 
														 pDef, 
														 iTeamSize,
														 iGroupIdx,
														 bOvertime))
				{
					iChosenGroup = iGroupIdx;
					break;
				}
			}
		}
	}

	//If I found a group to place this member/member team on
	if (iChosenGroup >= 0)
	{
		if (iTeamSize > 1)
		{
			//Place the team into the group
			QueueGroup *pGroup = pMatch->eaGroups[iChosenGroup];
			S32 iActualSize = aslQueue_AddTeamToGroup(pMatch, pGroup, pMember, peaReadyMembers);

			if (iTeamSize != iActualSize)
			{
				log_printf(LOG_QUEUE, 
					"Queue Error: The expected team size [%d] didn't match actual team size [%d].\n",
					iTeamSize, iActualSize);
			}
		}
		else
		{
			//Place the member into the group
			queue_Match_AddMemberToGroup(pMatch, pMatch->eaGroups[iChosenGroup], pMember->iEntID, pMember->iTeamID, pMember->iGroupRole, pMember->iGroupClass);
			
		}
		return iTeamSize;
	} 
	else
	{
		return 0;
	}
}

// We are trying to fill pMatch. It may already have people in it.
static S32 aslQueue_MatchMember(QueueInstance *pInstance,
								QueueMatch *pMatch,
								QueueDef *pDef,
								QueueMember *pMember,
								S32 iInputMemberIdx,
								bool bOvertime,
								SA_PARAM_NN_VALID QueueMember ***peaPotentialMembers)
{
	S32 iTeamSize = 1;
	if (pMember->iTeamID)
	{
		iTeamSize = MAX(pMember->iTeamSize, 1);
	}

	if (!pDef->Settings.bSplitTeams || iTeamSize == 1)
	{
		return aslQueue_FindGroupForMember(pInstance, pMatch, pMember, pDef, iTeamSize, bOvertime, peaPotentialMembers);
	}
	else
	{
		// SplitTeams with a team being placed
		S32 iMemberIdx, iNumMembers = eaSize(peaPotentialMembers);
		S32 iMembersMatched = 0;
		for (iMemberIdx = iNumMembers-1; iMemberIdx >= iInputMemberIdx; iMemberIdx--)
		{
			QueueMember *pTeamMember = eaGet(peaPotentialMembers, iMemberIdx);
			if (pTeamMember->iTeamID == pMember->iTeamID)
			{
				iMembersMatched++;
				if (!aslQueue_FindGroupForMember(pInstance, pMatch, pTeamMember, pDef, 1, bOvertime, peaPotentialMembers))
				{
					log_printf(LOG_QUEUE, "Queue Error [%s], Failure to find a group for [%d].\n",
						pDef->pchName,
						pTeamMember->iEntID);
					iMembersMatched = 0;
					break;
				}
				else if (pMember != pTeamMember)
				{
					eaRemove(peaPotentialMembers, iMemberIdx);
				}
			}

			if (iMembersMatched >= iTeamSize)
				break;
		}

		//Return the number I matched
		return iMembersMatched;
	}
}


//   This is the core of current matchmaking
//Attempts to make a match, return the number of members matched
S32 aslQueue_AttemptMatch(SA_PARAM_NN_VALID QueueInfo *pQueue,
						  SA_PARAM_NN_VALID QueueInstance *pInstance,
						  SA_PARAM_NN_VALID QueueDef *pQueueDef,
						  SA_PARAM_OP_VALID QueueMap *pQueueMap,
						  SA_PARAM_NN_VALID QueueMember ***peaReadyMembers,
						  SA_PARAM_NN_VALID QueueMatch *pQueueMatch,
						  S32 iStartIdx)
{
	S32 iMemberIdx;
	S32 iMembersMatched = 0;
	bool bOvertime = pInstance->bOvertime;

	//Start by making a copy of the members array
	QueueMember **eaCopyMembers = NULL;
	eaCopy(&eaCopyMembers, peaReadyMembers);

	//Reset the validity
	pQueueMatch->bAllGroupsValid = true;

	iMembersMatched = 0;

	//The size needs to be re-evaluated each iteration because the aslQueue_MatchMember can remove members from the array
	for (iMemberIdx = iStartIdx; iMemberIdx < eaSize(&eaCopyMembers); iMemberIdx++)
	{
		QueueMember *pMember = eaGet(&eaCopyMembers, iMemberIdx);
		S32 iTeamSize = MAX(pMember->iTeamSize, 1);
		S32 iCurrentMembersMatched;
		bool bIsNewMap = pQueueMap==NULL || (pQueueMap->eMapState == kQueueMapState_Open);  // QueueMap will be NULL if we are querying to create new maps.

		if (pQueueMatch->iMatchSize + iTeamSize > pQueue->iMaxMapSize)
			continue;

		//If the member has iJoinMapID set, then they want to go to a specific map
		if (pMember->iJoinMapKey && (!pQueueMap || pMember->iJoinMapKey != pQueueMap->iMapKey))
			continue;

		// Member can be flagged for only new maps. Make sure that's the case.
		if ((pMember->eJoinFlags & kQueueJoinFlags_JoinNewMap) && !bIsNewMap)
			continue;

		//Match the member, add the number matched to the counter
		iCurrentMembersMatched = aslQueue_MatchMember(pInstance, pQueueMatch, pQueueDef, pMember, iMemberIdx, bOvertime, &eaCopyMembers);
		
		//If the members is on a team, then validate that the whole team was added
		if (pMember->iTeamID && iCurrentMembersMatched != (S32)pMember->iTeamSize)
		{
			pQueueMatch->bAllGroupsValid = false;
		}
		iMembersMatched += iCurrentMembersMatched;

		//If we've matched enough, break and return
		if (pQueueMatch->iMatchSize >= pQueue->iMaxMapSize || !pQueueMatch->bAllGroupsValid)
		{
			break;
		}
	}

	if (!pQueueDef->MapRules.bDisableAutoBalance && !pInstance->bAutoFill &&
		!SAFE_MEMBER2(pInstance, pParams, uiOwnerID))
	{
		//Make sure that the match is balanced
		if (!aslQueue_AutoBalanceMatchIsBalanced(pQueueMatch, pQueueDef))
		{
			// Try balancing
			if (!aslQueue_AutoBalanceMatch(pInstance, pQueueMatch, pQueueDef, false))
			{
				pQueueMatch->bAllGroupsValid = false;
				iMembersMatched = 0;
			}
		}
	}
	if (pQueueMatch->bAllGroupsValid)
	{
		//Now validate the match
		pQueueMatch->bAllGroupsValid = queue_Match_Validate(pQueueMatch, 
															pQueueDef,
															pInstance->pParams, 
															pInstance->bOvertime, 
															false, 
															false);
	}

	eaDestroy(&eaCopyMembers);

	return iMembersMatched;
}


// Centralized Map matching. Both MMR and FillOpen use this

//Simple matching at first, better matching later
static S32 aslQueue_MapMatch(SA_PARAM_NN_VALID QueueInfo *pQueue,
							 SA_PARAM_NN_VALID QueueInstance *pInstance,
							 SA_PARAM_NN_VALID QueueDef *pQueueDef,
							 SA_PARAM_OP_VALID QueueMap *pQueueMap,
							 SA_PARAM_NN_VALID QueueMember ***peaReadyMembers,
							 SA_PARAM_NN_VALID QueueMatch *pQueueMatch)
{
	S32 iMembersMatched = 0;
	U32 iAttemptIdx = 0;
	U32 iNumMembers = eaSize(peaReadyMembers);

	QueueMatch *pLocalMatch = StructCreate(parse_QueueMatch);
	
	bool bIsNewMap = pQueueMap==NULL || (pQueueMap->eMapState == kQueueMapState_Open);  // QueueMap will be NULL if we are querying to create new maps.

	//Copy the initial data into the local match
	StructCopyAll(parse_QueueMatch, pQueueMatch, pLocalMatch);

	//1) Attempt the first simple match	
	iMembersMatched = aslQueue_AttemptMatch(pQueue, pInstance, pQueueDef, pQueueMap, peaReadyMembers, pLocalMatch, iAttemptIdx);

	//2) Brute force mode if we have equal or greater failed matches and it's attempting to fill a new map
	if (bIsNewMap && pInstance->iFailedMatchCount >= QUEUE_BRUTE_FORCE_COUNT)
	{
		U32 iAttemptsToMake = iNumMembers > pQueue->iMinMapSize ? iNumMembers - pQueue->iMinMapSize : 0;
		//This loop only occurs in brute force mode when the first attempted matching wasn't valid
		for (iAttemptIdx = 1;
			(!pLocalMatch->bAllGroupsValid &&
			 iAttemptIdx <= iAttemptsToMake);
			iAttemptIdx++)
		{
			//Copy the initial data into the local match
			StructCopyAll(parse_QueueMatch, pQueueMatch, pLocalMatch);

			iMembersMatched = aslQueue_AttemptMatch(pQueue, pInstance, pQueueDef, pQueueMap, peaReadyMembers, pLocalMatch, iAttemptIdx);
		}
	}

	//Copy the local match back out
	StructCopyAll(parse_QueueMatch, pLocalMatch, pQueueMatch);

	//Destroy local matching data
	StructDestroy(parse_QueueMatch,pLocalMatch);

	return iMembersMatched;
}


static void aslQueue_LaunchMap(SA_PARAM_NN_VALID QueueInfo* pQueue, 
							   SA_PARAM_NN_VALID QueueInstance* pInstance, 
							   SA_PARAM_NN_VALID QueueMap* pMap, 
							   SA_PARAM_OP_VALID QueueMatch* pMatch, 
							   SA_PARAM_NN_VALID QueueDef* pQueueDef)
{
	bool bIsPrivate = SAFE_MEMBER(pInstance->pParams, uiOwnerID) > 0;

	// Log information about the map launch
	aslQueue_LogMapLaunch(pQueueDef, pInstance, pMatch, pMap);

	// Set the update time
	pMap->iLastServerUpdateTime = timeSecondsSince2000();

	// Turn off auto-fill
	pInstance->bAutoFill = false;
	
	// Change the map state
	if (bIsPrivate || queue_InstanceShouldCheckOffers(pQueueDef, pInstance->pParams))
	{
		aslQueue_MapUpdate(pQueue, pInstance, pMap, pMatch, kQueueMapState_LaunchCountdown, NULL);
		if (pMatch)
		{
			aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, pMatch, kQueueMapState_LaunchCountdown);
		}
	}
	else
	{
		aslQueue_MapUpdate(pQueue, pInstance, pMap, pMatch, kQueueMapState_Launched, NULL);
		if (pMatch)
		{
			aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, pMatch, kQueueMapState_Launched);
		}
		
	}
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



static void aslQueue_HandleOfferValidationMatchFailure(QueueInfo *pQueue, 
													   QueueInstance* pInstance, 
													   QueueMap* pMap, 
													   QueueDef* pQueueDef,
													   bool bEjectOffered,
													   S32* piAccepted,
													   S32* piOffered)
{
	S32 iMemberIdx;
	for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		
		if (pMember->iMapKey != pMap->iMapKey)
			continue;

		if (pMember->eState == PlayerQueueState_Accepted)
		{
			PlayerQueueState eState = PlayerQueueState_InQueue;
			aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, QUEUE_LAUNCH_TIMEOUT_MESG, true);
			if (piAccepted)
			{
				(*piAccepted)++;
			}
		}
		else if (pMember->eState == PlayerQueueState_Offered)
		{
			ContainerID uEntID = pMember->iEntID;
			PlayerQueueState eState = bEjectOffered ? PlayerQueueState_Exiting : PlayerQueueState_InQueue;
			aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, QUEUE_OFFER_TIMEOUT_MESG, false);
			if (piOffered)
			{
				(*piOffered)++;
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Match validity checks. 
//  This gates when we can actually move members onto the map. It is checked once members have been issues invites. 
//	We distinguish a launching map from a live map adding members. I'm not sure why, but the old code treated them differently.
//	   For now preserve old functionality. bIgnoreMin, bLazyValidate, and bAllowInQueueUnmatched all differed slightly.

static bool aslQueue_CheckMatchValidAndOffers(QueueInfo *pQueue, 
								 QueueInstance* pInstance, 
								 QueueMap* pMap, 
								 QueueDef* pQueueDef,
								 QueueMatch* pMatch,
								bool bLastEffortMatching,
								bool bIsLaunching)
{

	// We distinguish a launching map from a live map adding members. I'm not sure why, but the old code treated them differently.
	//   For now preserve old functionality.  
	
	bool bValidMatch = false;
	bool bIgnoreMin = false;

	// Ignore min determines if we are considered valid even if below the minimum for the queue.
	//   Not sure why the launching case is treated differently (never ignore min) than the liveMap case
	if (!bIsLaunching)
	{
		bIgnoreMin = bLastEffortMatching;
	}

	if (pQueue->bNeverLaunchMaps)
	{
		return false;
	}

	queue_GroupCacheWithState(pInstance, pQueueDef, pMap, PlayerQueueState_Accepted, PlayerQueueState_Offered, pMatch);

	// If we're not LastEffortMatching, Check to see if all offers have been resolved
	if (!bLastEffortMatching)
	{
		S32 iMemberIdx, iMemberCount = eaSize(&pInstance->eaUnorderedMembers);
		for (iMemberIdx = 0; iMemberIdx < iMemberCount; iMemberIdx++)
		{
			QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
			if (pMember->iMapKey == pMap->iMapKey && pMember->eState == PlayerQueueState_Offered)
			{
				// Pending on an offer. Not Valid right now
				return(false);
			}
		}
	}

	if (pInstance->bAutoFill)
	{
		// Special AutoFill case. Don't rebalance and don't check actual validity. We just need all offers to be resolved and at least one person accepted and in the match.
		//   We need to be careful in launching that all initial members have accepted or not all will transfer. (See SVN 130052, NNO-9832). That check-in was incorrect
		//   in that it used the number of members in the queue. So would fail if there were 2 teams of 3 waiting and someone autoFilled, for instance.
		return ((S32)pMatch->iMatchSize > 0);
	}

	// Auto balancing enabled?
	if (!pQueueDef->MapRules.bDisableAutoBalance)
	{
		bool bLazyValidate = bLastEffortMatching;
		bool bDoAutoBalance = bLastEffortMatching;	// Only try rebalancing if this is our last try
		bool bBalanced=true;

		if (bIsLaunching) bLazyValidate=false;	// Not sure what this distinction accomplishes. This is the way it was in the old code.
		
		if (!SAFE_MEMBER2(pInstance, pParams, uiOwnerID))
		{
			// if bDoAutoBalance, actually try to do something. Otherwise just valdidate
			bBalanced = aslQueue_AutoBalanceMatchIsBalanced(pMatch, pQueueDef);
			if (!bBalanced && bDoAutoBalance)
			{
				bBalanced = aslQueue_AutoBalanceMatch(pInstance, pMatch, pQueueDef, bLazyValidate);
			}
		}
		if (bBalanced)
		{
			bValidMatch = queue_Match_Validate(pMatch, 
											   pQueueDef, 
											   pInstance->pParams, 
											   pInstance->bOvertime, 
											   bDoAutoBalance,  // (This actually just determines the minNumMembers used for validation)
											   bIgnoreMin);
		}

		
		// If the match was auto-balanced, make sure members that are no longer part of any groups are removed back into the queue. 
		if (bDoAutoBalance && bValidMatch)
		{
			bool bAllowInQueueUnmatched = false; // Matches old CheckOffers version from MapState update. 
			if (!bIsLaunching) bAllowInQueueUnmatched = true;
				// AllowInQueueUnmatched because we are bDoAutoBalance. This used to be either (true, true) or (false, false) for the passed in
				//   booleans so they always matched in the non-launching live-map case. 
				// This may be a bug? Not sure what it's trying to do. Poor Star Trek.
			
			aslQueue_AutoBalance_RemoveUngroupedMembers(pQueue, pInstance, pMap, pQueueDef, pMatch, bAllowInQueueUnmatched);
		}
	}
	else
	{
		bValidMatch = queue_Match_Validate(pMatch, 
										   pQueueDef, 
										   pInstance->pParams, 
										   pInstance->bOvertime,
										   false,	// No autobalance attempt if AutoBalancing disabled. (This actually just determines the minNumMembers used for validation)
													//  This is a change from the old code which would use the AutoBalance min even if autoBalancing was off.
										   bIgnoreMin);
		
	}
	return bValidMatch;
}


static void aslQueue_HandleLiveMapOfferValidation(QueueInfo *pQueue, 
												  QueueInstance* pInstance, 
												  QueueMap* pMap, 
												  QueueDef* pQueueDef)
{
	// Only for  kQueueMapState_Launched and kQueueMapState_Active
	
	U32 uCurrentTime = timeSecondsSince2000();
	QueueMatch QMatch = {0};
	if (!pMap->uOfferValidationStartTime)
	{
		return;
	}
	if (uCurrentTime > pMap->uOfferValidationStartTime + g_QueueConfig.uMemberResponseTimeout)
	{
		// Past the timeout. Try one last match, using aggressive matching (Try another AutoBalance if enabled, or relax minimum requirements)
		if (aslQueue_CheckMatchValidAndOffers(pQueue, pInstance, pMap, pQueueDef, &QMatch, true/*bLastEffort*/, false/*bIsLaunching*/))
		{
			// If that succeeds, go into countdown and then do the map transfer
			aslQueue_MatchMembersSetToCountdown(pQueue, pInstance, &QMatch);
		}
		else // If the match fails, cancel all offers
		{
			S32 iAccepted = 0;
			S32 iOffered = 0;
			aslQueue_HandleOfferValidationMatchFailure(pQueue, 
													   pInstance, 
													   pMap, 
													   pQueueDef, 
													   true, 
													   &iAccepted, 
													   &iOffered);
			aslQueue_LogAbortMatch(pQueue, pInstance, pMap, iAccepted, iOffered, "Live Map Offer Validation");
		}
		pMap->uOfferValidationStartTime = 0;
	}
	else
	{
		// Just regular checking for matchvalidity
		if (aslQueue_CheckMatchValidAndOffers(pQueue, pInstance, pMap, pQueueDef, &QMatch, false/*bLastEffort*/, false/*bIsLaunching*/))
		{
			// If all members have accepted their offers, and this is a valid match, go into countdown
			aslQueue_MatchMembersSetToCountdown(pQueue, pInstance, &QMatch);
			
			// Clear the validation start time
			pMap->uOfferValidationStartTime = 0;
		}
	}
	StructDeInit(parse_QueueMatch, &QMatch);
}


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Map State updating


	///////////////////////////////////////////////////
	//
	//  Live map (launched/active) updating

static bool aslQueue_IsMapEmpty(QueueInstance* pInstance, QueueMap* pMap)
{
	S32 iMemberIdx;
	for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		if (pMember->iMapKey == pMap->iMapKey)
		{
			return false;
		}
	}
	return true;
}


// Only for  kQueueMapState_Launched and kQueueMapState_Active
static void aslQueue_LiveMapUpdate(QueueInfo* pQueue, QueueInstance* pInstance, QueueMap* pMap, QueueDef* pQueueDef, U32 uTimeout)
{
	U32 uCurrentTime = timeSecondsSince2000();
	if (pMap->iLastServerUpdateTime + uTimeout < uCurrentTime)
	{
		// Destroy the map if the server stops responding
		aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Server stopped responding");
	}
	else if (pQueueDef->Settings.bDestroyEmptyMaps && aslQueue_IsMapEmpty(pInstance, pMap))
	{
		// Destroy the map if there are no players on it
		aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Map was empty");
	}
	else if (queue_InstanceShouldCheckOffers(pQueueDef, pInstance->pParams))
	{
		aslQueue_HandleLiveMapOfferValidation(pQueue, pInstance, pMap, pQueueDef);
	}
}


	///////////////////////////////////////////////////
	//
	//  Ready Members

static bool aslQueue_MemberIsAssociatedWithOtherInstance(QueueInstance* pCurInstance, QueueMember* pMember)
{
	S32 iQueueIdx, iInstIdx;
	for (iQueueIdx = eaSize(&s_LocalData.eaLocalQueues)-1; iQueueIdx >= 0; iQueueIdx--)
	{
		QueueInfo* pLocalInfo = s_LocalData.eaLocalQueues[iQueueIdx];

		for (iInstIdx = eaSize(&pLocalInfo->eaInstances)-1; iInstIdx >= 0; iInstIdx--)
		{
			QueueInstance* pInstance = pLocalInfo->eaInstances[iInstIdx];
			if (pInstance!=pCurInstance)
			{
				QueueMember* pCurrMember = queue_FindPlayerInInstance(pInstance, pMember->iEntID);
				if (pCurrMember && (aslQueue_MemberActivated(pCurrMember) || 
					pCurrMember->eState == PlayerQueueState_Invited))	
				{	
					return true;
				}
			}
		}
	}
	return false;
}

static S32 aslQueue_GetReadyMembers(QueueInfo* pQueue, QueueInstance* pInstance, QueueMember ***peaReadyMembers)
{
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iMemberIdx, iMemberSize = ea32Size(&pInstance->eaOrderedMemberEntIds);
	S32 iReadyMembers = 0;

	// We are copying members into the ReadyMembers array for placement. Here we need to pay attention to the actual
	//    join order in order to do the correct thing.

	devassert(eaSize(&pInstance->eaUnorderedMembers)==iMemberSize);	// Unordered members should be the same as the size of the ent ids

	//Update member indexes and team size
	for (iMemberIdx = 0; iMemberIdx < iMemberSize; iMemberIdx++)
	{
		QueueMember *pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, pInstance->eaOrderedMemberEntIds[iMemberIdx]);
		if (pMember->eState == PlayerQueueState_InQueue && pMember->iQueueEnteredTime <= iCurrentTime)
		{
			if (!aslQueue_MemberIsAssociatedWithOtherInstance(pInstance, pMember))
			{
				if (pMember->iTeamID)
				{
					QueueMemberTeam *pTeam = eaIndexedGetUsingInt(&pInstance->eaTeams,pMember->iTeamID);
					pMember->iTeamSize = pTeam ? pTeam->iTeamSize : 1;
				}
				else
				{
					pMember->iTeamSize = 0;
				}

				// Push the whole QueueMember onto the ready list. ReadyMembers is unindexed, so it will hold its order
				if (peaReadyMembers)
				{
					eaPush(peaReadyMembers, pMember);
				}
				iReadyMembers++;
			}
		}
	}
	return iReadyMembers;
}


	///////////////////////////////////////////////////
	//
	//  Cancel Map Launch

static void aslQueue_CancelMapLaunch(QueueInfo *pQueue, QueueInstance* pInstance, QueueMap* pMap, QueueDef* pQueueDef)
{
	S32 iAccepted = 0;
	S32 iOffered = 0;

	//Perform actions tied to match failure
	aslQueue_HandleOfferValidationMatchFailure(pQueue, pInstance, pMap, pQueueDef, true, &iAccepted, &iOffered);

	//Destroy the map
	aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Map launch cancelled");

	//Log cancelled map launch
	aslQueue_LogAbortMatch(pQueue, pInstance, pMap, iAccepted, iOffered, "Map Launch Attempt");	
}


static bool aslQueue_ShouldMapCancelLaunchPending(	U32 uCurrentTime,
													SA_PARAM_NN_VALID QueueInstance* pInstance,
													SA_PARAM_NN_VALID QueueDef *pDef, 
													SA_PARAM_NN_VALID QueueMap *pMap)
{
	// check if the launch pending time has expired completely
	if (pMap->iStateEnteredTime + g_QueueConfig.uMemberResponseTimeout < uCurrentTime)
	{
		return true;
	}
	
	return false;
}

	///////////////////////////////////////////////////
	//
	//  UPDATE
	//     Updates the states of the maps the QueueServer is tracking

static void aslQueue_MapStateUpdate(QueueInfo *pQueue, QueueDef *pQueueDef)
{
	S32 iInstanceIdx, iInstanceCount = eaSize(&pQueue->eaInstances);
	U32 iCurrentTime = timeSecondsSince2000();

	for (iInstanceIdx = 0; iInstanceIdx < iInstanceCount; iInstanceIdx++)
	{
		QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
		S32 iMapIdx;

		for (iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
		{
			QueueMap *pMap = pInstance->eaMaps[iMapIdx];

			// Set from  InformQueueServerOfGameServerDeath
			if (pMap->bGameServerDied)
			{
				aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Server died");
				continue;
			}

			else if (pMap->bPendingFinished)
			{
				// Set from aslQueue_MapFinish in QueueTransactions
				if (aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Finished, NULL))
				{
					pMap->bPendingFinished=false;
				}
			}
			else
			{
				switch (pMap->eMapState)
				{
					case kQueueMapState_None:
						break;
					case kQueueMapState_StartingUp:
						break;
	
					case kQueueMapState_Open:
					{
						if (pMap->iStateEnteredTime + QUEUE_MAP_OPEN_TIMEOUT < iCurrentTime)
						{
							int iReadyMembers = aslQueue_GetReadyMembers(pQueue, pInstance, NULL);
							char* estrTimeoutString = NULL;
							estrStackCreate(&estrTimeoutString);
							estrPrintf(&estrTimeoutString, "Timed out. Queue had %d members waiting to be matched", iReadyMembers);
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, estrTimeoutString);
							estrDestroy(&estrTimeoutString);
						}
					}
					break;
					
					case kQueueMapState_Finished:
						// Holding state for the map when we continue tracking post-map-finish.
						// If g_QueueConfig.bMaintainQueueTrackingOnMapFinish is off,
						//   we will have stopped tracking in QueueTransaction in aslQueue_MapFinish
						//   and will never get here.

						if (g_QueueConfig.bMaintainQueueTrackingOnMapFinish)
						{
							// We need to update the map so it will shut down when all the members are off of it (or we time out)
							aslQueue_LiveMapUpdate(pQueue, pInstance, pMap, pQueueDef, QUEUE_MAP_ACTIVE_TIMEOUT);	
						}
						
						break;
	
					case kQueueMapState_Limbo:
					{
						// PendingActive set from aslQueue_UpdateMapLeaderboard in QueueTransactions.
						//   The GameServer pinged us, so we can recover from limbo. That is the ONLY way to recover from Map Limbo
						if (pMap->bPendingActive)
						{
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Active, NULL);
							pMap->bPendingActive=false;
						}
						else if (pMap->iLastServerUpdateTime + QUEUE_MAP_LIMBO_TIMEOUT < iCurrentTime)
						{
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Limbo Timeout");
						}
					}
					break;
	
					case kQueueMapState_LaunchPending:
					{
						if (pMap->iLastServerUpdateTime + QUEUE_MAP_LAUNCH_TIMEOUT < iCurrentTime)
						{
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy, "Timeout");
						}
						else if (pInstance->pParams && pInstance->pParams->uiOwnerID == 0)
						{
							QueueMatch QMatch = {0};
	
							bool bShouldCancel = aslQueue_ShouldMapCancelLaunchPending(iCurrentTime, pInstance, pQueueDef, pMap);
							// If we are timing out and should cancel, make one last effort with a more permissive check in the MatchValidity
	
							if (aslQueue_CheckMatchValidAndOffers(pQueue, pInstance, pMap, pQueueDef, &QMatch, bShouldCancel/*bLastEffort*/, true/*bIsLaunching*/))
							{
								aslQueue_LaunchMap(pQueue, pInstance, pMap, &QMatch, pQueueDef);
							}
							else if (bShouldCancel)
							{
								aslQueue_CancelMapLaunch(pQueue, pInstance, pMap, pQueueDef);
							}
							StructDeInit(parse_QueueMatch, &QMatch);
						}
					}
					break;
	
					case kQueueMapState_LaunchCountdown:
					{
						if (pMap->iLastServerUpdateTime + g_QueueConfig.uMapLaunchCountdown < iCurrentTime)
						{
							// Update the state
							pMap->iLastServerUpdateTime = iCurrentTime;
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Launched, NULL);
						}
					}
					break;
	
					case kQueueMapState_Launched:
					{
						// PendingActive set from aslQueue_MapRequestActive in QueueTransactions
						if (pMap->bPendingActive)
						{
							aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Active, NULL);
							pMap->bPendingActive=false;
						}
						else
						{
							aslQueue_LiveMapUpdate(pQueue, pInstance, pMap, pQueueDef, QUEUE_MAP_LAUNCH_TIMEOUT);
						}
					}
					break;
					
					case kQueueMapState_Active:
					{
						aslQueue_LiveMapUpdate(pQueue, pInstance, pMap, pQueueDef, QUEUE_MAP_ACTIVE_TIMEOUT);	
					}
				}
			}
		}
	}
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Member updates

static bool aslQueue_RemoveFromAllOtherInstances(QueueInfo* pIgnoreQueue,
												 QueueInstance* pIgnoreInstance, 
												 U32 uiMemberID, 
												 PlayerQueueState eMatchState)
{
	S32 i, j;
	bool bFound = false;
	PlayerQueueState eSetState = PlayerQueueState_Exiting;

	for (i = eaSize(&s_LocalData.eaLocalQueues)-1; i >= 0; i--)
	{
		QueueInfo* pQueue = s_LocalData.eaLocalQueues[i];
		for (j = eaSize(&pQueue->eaInstances)-1; j >= 0; j--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[j];
			if (pQueue->iContainerID != pIgnoreQueue->iContainerID || pInstance->uiID != pIgnoreInstance->uiID)
			{
				QueueMember* pMember = queue_FindPlayerInInstance(pInstance, uiMemberID);
				
				if (!pMember || (eMatchState != -1 && eMatchState != pMember->eState))
					continue;

				bFound |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eSetState, NULL, false);
			}
		}
	}
	return bFound;
}

static bool aslQueue_MemberUpdateTeam(SA_PARAM_NN_VALID QueueInfo* pQueue,
									  SA_PARAM_NN_VALID QueueInstance* pInstance,
									  SA_PARAM_NN_VALID QueueMember* pMember,
									  SA_PARAM_NN_VALID QueueDef* pDef)
{
	bool bDirty = false;
	if (pMember->iTeamID)
	{
		CachedTeamStruct *pCachedTeam = eaIndexedGetUsingInt(&s_eaCachedTeams, pMember->iTeamID);
		if (pCachedTeam)
		{
			if (eaiFind(&pCachedTeam->eaiEntityIds, pMember->iEntID) < 0)
			{
				if (aslQueue_MemberChangeStateEx(pQueue, 
												 pInstance, 
												 pMember, 
												 PlayerQueueState_None, 
												 0, 
												 true, 
												 -1, 
												 false, 
												 0, 
												 false))
				{
					bDirty = true;
				}
			}
			else if (!pMember->iMapKey)
			{
				QueueMemberTeam *pTeam = eaIndexedGetUsingInt(&pInstance->eaTeams, pMember->iTeamID);
				if (!pTeam)
				{
					pTeam = StructCreate(parse_QueueMemberTeam);
					pTeam->iTeamID = pMember->iTeamID;
					eaPush(&pInstance->eaTeams, pTeam);
					eaIndexedEnable(&pInstance->eaTeams, parse_QueueMemberTeam);
				}
				pTeam->iTeamSize++;
			}
		}
	}
	else if (eaSize(&s_eaCachedTeams) > 0)
	{
		//Check to see if this member has teamed up. If so, update the team and group.
			// I believe this should only happen if auto-teaming is off, since it should be impossible to
			// get onto a team in a queue instance otherwise. Perhaps this will become of import when we do
			// WoW-style auto-teaming.
		S32 iTeamIdx;
		for (iTeamIdx = eaSize(&s_eaCachedTeams)-1; iTeamIdx >= 0; iTeamIdx--)
		{
			CachedTeamStruct* pCachedTeam = s_eaCachedTeams[iTeamIdx];
			if (ea32Find(&pCachedTeam->eaiEntityIds, pMember->iEntID) >= 0)
			{
				bool bGroupChanged = false;
				S32 iGroupIdx = -1;
				if (!pMember->iMapKey && SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
				{
					QueueMatch QMatch = {0};
					queue_GroupCache(pInstance, pDef, NULL, &QMatch); 
					iGroupIdx = queue_GetBestGroupIndexForPlayer(pMember->pchAffiliation, 
																 pCachedTeam->iTeamID,
																 pMember->iGroupIndex, 
																 false, 
																 pDef,
																 &QMatch);
					
					StructDeInit(parse_QueueMatch, &QMatch);
					bGroupChanged = (pMember->iGroupIndex != iGroupIdx);
				}
				if (aslQueue_MemberChangeStateEx(pQueue, 
												 pInstance, 
												 pMember, 
												 PlayerQueueState_None, 
												 pCachedTeam->iTeamID, 
												 true, 
												 iGroupIdx, 
												 bGroupChanged,
												 0, 
												 false))
				{
					bDirty = true;
				}
				break;
			}
		}
	}
	return bDirty;
}

static QueueMember* aslQueue_FindQueueMemberInInstance(QueueInstance* pInstance, ContainerID entityID)
{
	QueueMember *pMember = eaIndexedGetUsingInt(&pInstance->eaUnorderedMembers, entityID);
	return pMember;
}

static bool aslQueue_MemberStateUpdateInstance(QueueInfo *pQueue, 
											   QueueInstance* pInstance, 
											   QueueDef *pQueueDef)
{
	QueueMember **eaTeamMembersOfferExired = NULL;
	U32 iCurrentTime = timeSecondsSince2000();
	S32 iMemberIdx, iMemberSize = eaSize(&pInstance->eaUnorderedMembers);
	bool bDirty = false;
	bool bOvertime = pInstance->bOvertime;
	
	//Reset the iOlderMember timestamp
	pInstance->iOldestMemberTstamp = 0;

	eaClearStruct(&pInstance->eaTeams, parse_QueueMemberTeam);

	//Remove old members, update states
	for (iMemberIdx = iMemberSize-1; iMemberIdx >= 0; iMemberIdx--)
	{
		QueueMember *pMember = pInstance->eaUnorderedMembers[iMemberIdx];
		ContainerID uEntID = pMember->iEntID;
		bool bFirstTickInState = false;

		if (pMember->iStateEnteredTime > pMember->uFirstUpdateTimeInState)
		{
			pMember->uFirstUpdateTimeInState = pMember->iStateEnteredTime;
			bFirstTickInState = true;
		}
		if (pMember->iMapKey)
		{
			bDirty |= aslQueue_MemberUpdateTeam(pQueue, pInstance, pMember, pQueueDef);
		}
		switch (pMember->eState)
		{
			case PlayerQueueState_None:
				break;

			case PlayerQueueState_Limbo:
			{
				// Limbo seems to happen when A) The QueueServer is restarted
				// B) The player is in _Accepted and takes too long
				// C) The player is _InMap and the gameServer stops responding for too long
				
				U32 uPlayerLimboTimeout = QUEUE_MEMBER_LIMBO_TIMEOUT;
				if (pQueueDef->Settings.uPlayerLimboTimeoutOverride)
				{
					uPlayerLimboTimeout = pQueueDef->Settings.uPlayerLimboTimeoutOverride;
				}
				// If the member has been in limbo for a while (60 seconds by default), exit the queue
				//   We used to never timeout if bStayInQueueOnMapLeave is set. This causes problems with limbo players sticking around forever.
				//   See aslQueue_UpdateMapLeaderboard for how we are now trying to deal with this.
				if (pMember->iStateEnteredTime + uPlayerLimboTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Exiting;

					// If the leaver penalty is enabled, check to see if the player's map is finished (this seems a little unfair since
					//   limbo is not something the player really has control over. Unless it's the 'accepted' failure.)
					if (pQueueDef->Settings.bEnableLeaverPenalty)
					{
						QueueMap* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iMapKey);
						// If the map isn't finished, penalize the player
						if (pMap && pMap->eMapState != kQueueMapState_Finished)
						{
							U32 uPartitionID = queue_GetPartitionIDFromMapKey(pMap->iMapKey);
							U32 uMapID = queue_GetMapIDFromMapKey(pMap->iMapKey);
							RemoteCommand_gslQueue_AttemptToPenalizePlayer(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, pMember->iEntID, pQueueDef->pchName);
						}
					}
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
				//Place the member back into the 'InMap' state if they become active again
				if (pMember->iLastMapUpdate + 5 >= iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_InMap;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
			}
			break;

			case PlayerQueueState_WaitingForTeam:
			{
				PlayerQueueState eState = PlayerQueueState_InQueue;
				bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
			}
			break;
			
			case PlayerQueueState_InQueue:
			{
				//Used to track the oldest member to determine if the queue should be in "overtime" state where we let someone in with a lower (typically) min count
				if (pInstance->iOldestMemberTstamp==0 || pMember->iQueueEnteredTime < pInstance->iOldestMemberTstamp)
				{
					pInstance->iOldestMemberTstamp = pMember->iQueueEnteredTime;
				}

				//Remove the player from all other instances when accepting an invite to a private instance
				if (bFirstTickInState && SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0)
				{
					bDirty |= aslQueue_RemoveFromAllOtherInstances(pQueue, pInstance, uEntID, -1);
				}
				if (pMember->iJoinMapKey)
				{
					S32 iTeamSize = MAX(pMember->iTeamSize, 1);
					const char* pchAffiliation = pMember->pchAffiliation;
					QueueMap* pDesiredMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iJoinMapKey);
					if (!queue_MapAcceptingNewMembers(pDesiredMap, pQueueDef))
					{
						// Theoretically we could get here because the map state was wrong. We are really just trying to check the
						//   JoinTimeLimit. But it's likely we would never have a JoinMapKey unless the map was already in the correct state.
						//   This will cover for an edge case where the timeLimit expired while transactions were happening.
						PlayerQueueState eState = PlayerQueueState_Exiting;
						const char* pchMsgKey = QUEUE_BEYOND_MAX_JOIN_TIME_MESG;
						bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, pchMsgKey, false);
						break;
					}
					else if (queue_IsMapFull(pDesiredMap, pInstance, pQueueDef, iTeamSize, pchAffiliation, bOvertime))
					{
						PlayerQueueState eState = PlayerQueueState_Exiting;
						const char* pchMsgKey = QUEUE_MAP_FULL_MESG;
						bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, pchMsgKey, false);
						break;
					}
				}
				//Update team information
				bDirty |= aslQueue_MemberUpdateTeam(pQueue, pInstance, pMember, pQueueDef);
			}
			break;

			case PlayerQueueState_Exiting:
			{
				//Shouldn't get here anymore
			}
			break;

			case PlayerQueueState_Invited:
			{
				if (pMember->iStateEnteredTime + g_QueueConfig.uMemberInviteTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Exiting;
					const char* pchMsgKey = QUEUE_INVITE_TIMEOUT_MESG;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, pchMsgKey, false);
				}
			}
			break;

			case PlayerQueueState_Offered:
			{
				if (pMember->iStateEnteredTime + g_QueueConfig.uCheckOffersResponseTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Exiting;
					const char* pchMsgKey = QUEUE_OFFER_TIMEOUT_MESG;
					
					if((g_QueueConfig.bEnableStrictTeamRules || pQueueDef->bEnableStrictTeamRules) && pMember && pMember->iTeamID)
					{	// if we have strict teaming enabled and this member is on a team, 
						// save this guy to be processed afterwards so we can remove everyone on that team
						bool bFoundTeam = false;

						FOR_EACH_IN_EARRAY(eaTeamMembersOfferExired, QueueMember, pOtherMember)
						{
							if (pOtherMember->iTeamID == pMember->iTeamID)
							{	// another member on this team let the offer expire already, don't add this member 
								// because we'll process the whole team together
								bFoundTeam = true;
								break;
							}
						}
						FOR_EACH_END

						if (!bFoundTeam)
						{
							eaPush(&eaTeamMembersOfferExired, pMember);
						}
					}
					else 
					{
						bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, pchMsgKey, false);
					}
				}
				else if (g_QueueConfig.uAutoAcceptTime && pMember->iStateEnteredTime + g_QueueConfig.uAutoAcceptTime < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Accepted;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
			}
			break;
			
			case PlayerQueueState_Delaying:
			{
				if (pMember->iStateEnteredTime + g_QueueConfig.uMemberDelayTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_InQueue;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
			}
			break;
			
			case PlayerQueueState_Accepted:
			{
				U32 uTimeout = g_QueueConfig.uMemberAcceptTimeout;
				if (queue_InstanceShouldCheckOffers(pQueueDef, pInstance->pParams))
				{
					uTimeout = g_QueueConfig.uMemberAcceptValidateTimeout;
				}
				//Remove all invites when a member accepts an offer
				if (bFirstTickInState)
				{
					bDirty |= aslQueue_RemoveFromAllOtherInstances(pQueue, pInstance, uEntID, PlayerQueueState_Invited);
				}
				if (pMember->iStateEnteredTime + uTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Limbo;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
			}
			break;
			
			case PlayerQueueState_Countdown:
			{
				if (pMember->iStateEnteredTime + g_QueueConfig.uMapLaunchCountdown < iCurrentTime)
				{
					//At the end of the countdown timer, send the player to the map
					QueueMap* pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pMember->iMapKey);
					if (pMap)
					{
						PlayerQueueState eState = PlayerQueueState_InMap;
						bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
					}
					else
					{
						PlayerQueueState eState = PlayerQueueState_InQueue;
						const char* pchMsgKey = QUEUE_MAP_NOT_FOUND_MESG;
						bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, pchMsgKey, false);
					}
				}
			}
			break;
			
			case PlayerQueueState_InMap:
			{
				if (pMember->iLastMapUpdate + g_QueueConfig.uMemberInMapTimeout < iCurrentTime)
				{
					PlayerQueueState eState = PlayerQueueState_Limbo;
					bDirty |= aslQueue_MemberChangeState(pQueue, pInstance, pMember, eState, NULL, false);
				}
				//Remove the member from all other instances while 'InMap'
				if (bFirstTickInState)
				{
					bDirty |= aslQueue_RemoveFromAllOtherInstances(pQueue, pInstance, uEntID, -1);
				}
			}
		}
	}


	if (eaSize(&eaTeamMembersOfferExired))
	{
		// we have a list of members that failed to respond to an offer, so their team must also be removed from the queue
		// due to g_QueueConfig.bEnableStrictTeamRules

		FOR_EACH_IN_EARRAY(eaTeamMembersOfferExired, QueueMember, pMember)
		{
			CachedTeamStruct *pTeam = eaIndexedGetUsingInt(&s_eaCachedTeams, pMember->iTeamID);
			
			if (pTeam)
			{
				S32 i;

				// save the leaver's entity ID, aslQueue_MemberChangeState will end up destroying the QueueMember
				U32 iLeaverEntID = pMember->iEntID;

				for (i = eaiSize(&pTeam->eaiEntityIds) - 1; i >= 0; --i)
				{
					QueueMember *pTeamMember = aslQueue_FindQueueMemberInInstance(pInstance, pTeam->eaiEntityIds[i]);
					if (pTeamMember)
					{
						bDirty |= aslQueue_MemberChangeState(	pQueue, 
																pInstance, 
																pTeamMember, 
																PlayerQueueState_Exiting,
																NULL, 
																false);
					}
				} 
								
				queue_SendTeamResultMessage(pTeam, iLeaverEntID, pQueueDef->pchName, QUEUE_TEAM_MEMBER_DECLINED_MESG);
			}
			else
			{	// could not find the team, so we'll just update the member 
				bDirty |= aslQueue_MemberChangeState(	pQueue, 
														pInstance, 
														pMember, 
														PlayerQueueState_Exiting, 
														QUEUE_OFFER_TIMEOUT_MESG, 
														false);
			}
		}
		FOR_EACH_END
					
		eaDestroy(&eaTeamMembersOfferExired);
	}
	

	return bDirty;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Create New Map

//Creates the QueueInstance->pNewMap pointer, fills in the values
void aslQueue_CreateNewMap(QueueInfo* pQueue, QueueInstance* pInstance, QueueDef* pDef, bool bTransact, const char *pchDefaultMapName)
{
	const char* pchMapName;
	ChoiceTable *pChoiceTable;
	QueueMap *pNewMap;

	if (!pDef || !pQueue || !pInstance->pParams)
		return;

	pChoiceTable = GET_REF(pDef->QueueMaps.hMapChoiceTable);

	if (pInstance->pParams->uiOwnerID > 0 && bTransact)
	{
		QInstanceUpdate* pUpdate = StructCreate(parse_QInstanceUpdate);
		pUpdate->uiInstanceID = pInstance->uiID;
		pUpdate->bNewMap = true;
		pUpdate->bOvertime = pInstance->bOvertime;

		AutoTrans_aslQueue_tr_UpdateInstance(NULL,
											 GetAppGlobalType(),
											 GLOBALTYPE_QUEUEINFO,
											 pQueue->iContainerID,
											 pUpdate);
		StructDestroySafe(parse_QInstanceUpdate, &pUpdate);
	}

	pNewMap = StructCreate(parse_QueueMap);

	// record the last time the server updated this map, if it takes to long then the map will block future map creation which is very bad as it will block the queue
	pNewMap->iLastServerUpdateTime = timeSecondsSince2000();

	//Set the request id, but don't let it be 0
	pNewMap->uiMapCreateRequestID = s_uiRequestID++;
	if (!s_uiRequestID){ ++s_uiRequestID;}

	// map key required for earray
	CONTAINER_NOCONST(QueueMap, pNewMap)->iMapKey = pNewMap->uiMapCreateRequestID;

	pchMapName = pchDefaultMapName;

	//Decide what map to play
	if (pChoiceTable && pDef->QueueMaps.pchTableEntry)
	{
		WorldVariable* pVar = choice_ChooseValue(pChoiceTable, pDef->QueueMaps.pchTableEntry, 0, 0, timeSecondsSince2000());
		if(pVar->eType == WVAR_MAP_POINT)
		{
			pchMapName = pVar->pcZoneMap;
		}
	}
	else if(SAFE_MEMBER2(pInstance, pParams, pchMapName) && !pchMapName)
	{
		// Use param map if there isn't a passed in map name
		pchMapName = SAFE_MEMBER2(pInstance, pParams, pchMapName);
	}

	// if no map then get a random one
	if(!pchMapName)
	{
		// Use any map if we don't have a map param map
		S32 iCount = queue_GetMapIndexByName(pDef, NULL);
		S32 iMapIndex = randomIntRange(0, iCount-1);
		pchMapName = queue_GetMapNameByIndex(pDef, iMapIndex);		// Get the map by index
	}

	eaPush(&pInstance->eaNewMaps, pNewMap);

	// For any override world variables set by the designers in the queue def, make sure they get copied over.
	queue_WorldVariablesFromDef(pDef, pInstance, pchMapName, &pNewMap->eaVariables);

	//Create the map!
	aslQueue_MapCreateRequest(pDef, pInstance, pchMapName, pNewMap->uiMapCreateRequestID, NULL);
}



//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  MatchmakingRules matching  (MMR)
//
//		mmccarry SVN 124076. Leaderboard matchmaking. Not used currently. Possibly dead code. Possibly not.

typedef struct QueueMatchMakerGroup
{
	QueueMember **ppMembers;

	F32 fAvgRank;
	F32 fAvgLevel;

	S32 *ePossibleGameTypes;
	S32 *iPossibleMaps;
}QueueMatchMakerGroup;


// Descending order
static int MMR_cmpMemberRank(const void *a, const void *b)
{
	return (*(QueueMember**)b)->pJoinPrefs->fCurrentRank - (*(QueueMember**)a)->pJoinPrefs->fCurrentRank;
}

//Place members into group using matchmaking to make the "fairest" match
static S32 aslQueue_MMR_MatchmakingMatch(SA_PARAM_NN_VALID QueueInfo *pQueue,
									SA_PARAM_NN_VALID QueueInstance *pInstance,
									SA_PARAM_NN_VALID QueueDef *pDef,
									SA_PARAM_OP_VALID QueueMap *pQueueMap,
									SA_PARAM_NN_VALID QueueMember ***peaReadyMembers,
									SA_PARAM_NN_VALID QueueMatch *pQueueMatch,
									bool bMustMatchAll)
{
	QueueMatch *pLocalMatch = StructCreate(parse_QueueMatch);
	QueueMember **ppLocalMembers = NULL;
	S32 iMembersMatched = 0;

	StructCopyAll(parse_QueueMatch,pQueueMatch,pLocalMatch);

	eaCopy(&ppLocalMembers,peaReadyMembers);

	if(!pInstance->bAutoFill && pDef->MapRules.pMatchMakingRules && pDef->MapRules.pMatchMakingRules->pchMatchMakingLeaderboard)
	{
		F32 fRankAverage = 0;
		int i;
		F32 fMembersPerGroup = 0;

		eaQSort(ppLocalMembers,MMR_cmpMemberRank);

		for(i=0;i<eaSize(peaReadyMembers);i++)
		{
			fRankAverage += (*peaReadyMembers)[i]->pJoinPrefs->fCurrentRank;
		}

		fRankAverage = fRankAverage / i;

		fMembersPerGroup = eaSize(peaReadyMembers) / eaSize(&pDef->eaGroupDefs);

		for(i=0;i<eaSize(&pDef->eaGroupDefs);i++)
		{
			F32 fCurrentAvg = 0;
			F32 fCurrentMembers = 0;

			if(eaSize(&pLocalMatch->eaGroups) <= i)
				eaPush(&pLocalMatch->eaGroups,StructCreate(parse_QueueGroup));

			//Put the first member in that group
			queue_Match_AddMember(pLocalMatch,i,ppLocalMembers[0]->iEntID,ppLocalMembers[0]->iTeamID);
			fCurrentAvg = ppLocalMembers[0]->pJoinPrefs->fCurrentRank;
			fCurrentMembers++;
			iMembersMatched++;
			eaRemove(&ppLocalMembers,0);

			while(eaSize(&pLocalMatch->eaGroups[i]->eaMembers) < fMembersPerGroup && eaSize(&ppLocalMembers))
			{
				int j;
				F32 fAverageDiff = FLT_MAX; 
				QueueMember *pNewMember = NULL;

				if(fCurrentAvg >= fRankAverage)
				{
					for(j=eaSize(&ppLocalMembers)-1;j>=0;j--)
					{
						F32 fNewDiff = fRankAverage - (fCurrentAvg * fCurrentMembers + ppLocalMembers[j]->pJoinPrefs->fCurrentRank) / (fCurrentMembers + 1);
						if(abs(fNewDiff) < fAverageDiff )
						{
							fAverageDiff = abs(fNewDiff);
							pNewMember = ppLocalMembers[j];
						}
						else
						{
							break;
						}
					}
				}
				else
				{
					for(j=0;j<eaSize(&ppLocalMembers);j++)
					{
						F32 fNewDiff = fRankAverage - (fCurrentAvg * fCurrentMembers + ppLocalMembers[j]->pJoinPrefs->fCurrentRank) / (fCurrentMembers + 1);
						if(abs(fNewDiff) < fAverageDiff )
						{
							fAverageDiff = abs(fNewDiff);
							pNewMember = ppLocalMembers[j];
						}
						else
						{
							break;
						}
					}
				}

				eaFindAndRemove(&ppLocalMembers,pNewMember);
				queue_Match_AddMember(pLocalMatch,i,pNewMember->iEntID,pNewMember->iTeamID);
				fCurrentAvg = (fCurrentAvg * fCurrentMembers + pNewMember->pJoinPrefs->fCurrentRank) / (fCurrentMembers + 1);
				fCurrentMembers++;
				iMembersMatched++;
			}

			if(i + 1 < eaSize(&pDef->eaGroupDefs)) //Avoid divide by 0 error
				fMembersPerGroup = eaSize(&ppLocalMembers) / (eaSize(&pDef->eaGroupDefs) - i - 1);
		}

		if(!bMustMatchAll || eaSize(&ppLocalMembers) == 0)
		{
			//Copy the local match back out
			StructCopyAll(parse_QueueMatch, pLocalMatch, pQueueMatch);
		}
		else
		{
			iMembersMatched = 0;
		}

		//Destroy local matching data
		StructDestroy(parse_QueueMatch,pLocalMatch);
		eaDestroy(&ppLocalMembers);

		return iMembersMatched;
	}
	else
	{
		// We can only get here with an "Open" map. Let the bIsNewMap function normally in MapMatch.
		//   Previous versions assumed we were NOT filling a new map
		return aslQueue_MapMatch(pQueue,pInstance,pDef,pQueueMap,peaReadyMembers,pQueueMatch);
	}
}

static bool aslQueue_MMR_MatchMakingFindOpenMap(QueueInfo *pQueue, 
											QueueInstance* pInstance,
											QueueDef *pDef,
											QueueMember ***peaGroupedMembers)
{
	S32 iMapIdx, iMapSize = eaSize(&pInstance->eaMaps);
	bool bReturn = false;

	for(iMapIdx = 0; iMapIdx < iMapSize; iMapIdx++)
	{
		QueueMap *pMap = pInstance->eaMaps[iMapIdx];
		QueueMatch *pMatch = NULL;

		if(pMap->bDirty)
			continue;

		// This is slightly different than queue_AcceptingNewMembers
		
		if(pMap->eMapState != kQueueMapState_Open)
			continue;

		// JoinTimeLimit check isn't really valid until we are Launched (it LaunchTime is zero and acceptable up to that point).
		//  Since we must be open. We don't need to test MapJoinTimeLimit

		//TODO(MM): make sure the game type and map is good

		pMatch = StructCreate(parse_QueueMatch);

		queue_GroupCacheWithState(pInstance, pDef, pMap, PlayerQueueState_InQueue, PlayerQueueState_None, pMatch);

		if (aslQueue_MMR_MatchmakingMatch(pQueue, pInstance, pDef, pMap, peaGroupedMembers, pMatch, true) > 0)
		{
			aslQueue_MatchMembersRemoveFromReadyList(pMatch, peaGroupedMembers);

			pMap->iLastServerUpdateTime = timeSecondsSince2000();

			if (queue_InstanceShouldCheckOffers(pDef, pInstance->pParams))
			{
				aslQueue_MapUpdate(pQueue, pInstance, pMap, pMatch, kQueueMapState_LaunchPending, NULL);
				if (pMatch)
				{
					aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, pMatch, kQueueMapState_LaunchPending);
				}
			}
			else if (!pQueue->bNeverLaunchMaps)
			{
				aslQueue_LaunchMap(pQueue, pInstance, pMap, pMatch, pDef);
			}

			bReturn = true;
		}

		// Get rid of the matching information
		StructDestroySafe(parse_QueueMatch, &pMatch);
	}

	return bReturn;
}


static bool aslQueue_MMR_CheckMatchMaking(QueueMatchMakerGroup *pGroup, QueueMember *pMember, QueueDef *pDef)
{
	int iTimeInQueue = timeSecondsSince2000() - pMember->iQueueEnteredTime;
	QueueMatchMakingRules *pRules = pDef->MapRules.pMatchMakingRules;
	F32 fMargin = 0.0f;
	
	//Check to see if member is ready for matchmaking

	if(iTimeInQueue < 0)
		return false;

	if(pRules->fRankRange && !pMember->pJoinPrefs->fCurrentRank)
		return false;

	//Check to see if member is already part of group
	if(eaFind(&pGroup->ppMembers,pMember) != -1)
		return false;

	fMargin = pRules->fRankRange + ((pRules->fRankMax - pRules->fRankRange) * min(1.0f,iTimeInQueue/pRules->fRankTimeOut));

	if(pGroup->fAvgRank < pMember->pJoinPrefs->fCurrentRank - fMargin || pGroup->fAvgRank > pMember->pJoinPrefs->fCurrentRank + fMargin)
		return false;

	fMargin = pRules->iDefaultLevelRange + ((pRules->iLevelMax - pRules->iDefaultLevelRange) * min(1.0f,iTimeInQueue/pRules->fLevelTimeOut));

	if(pGroup->fAvgLevel < pMember->iLevel - fMargin || pGroup->fAvgLevel > pMember->iLevel + fMargin)
		return false;

	if(iTimeInQueue < pRules->fMapTimeOut)
	{
		int i;

		if(ea32Size(&pMember->pJoinPrefs->ePreferredGameTypes) > 0 && ea32Size(&pGroup->ePossibleGameTypes) > 0)
		{
			for(i=0;i<ea32Size(&pMember->pJoinPrefs->ePreferredGameTypes);i++)
			{
				if(ea32Find(&pGroup->ePossibleGameTypes,pMember->pJoinPrefs->ePreferredGameTypes[i]) == -1)
					return false;
			}
		}

		if(ea32Size(&pMember->pJoinPrefs->iPreferredMaps) > 0 && ea32Size(&pGroup->iPossibleMaps) > 0)
		{
			for(i=0;i<ea32Size(&pMember->pJoinPrefs->iPreferredMaps);i++)
			{
				if(ea32Find(&pGroup->iPossibleMaps,pMember->pJoinPrefs->iPreferredMaps[i]) == -1)
					return false;
			}
		}
	}

	return true;
}

static void aslQueue_MMR_AddToMatchGroup(QueueMatchMakerGroup *pGroup, QueueMember *pMember)
{
	int iMembers = 0;

	iMembers = eaSize(&pGroup->ppMembers);

	eaPush(&pGroup->ppMembers,pMember);

	pGroup->fAvgRank = (pGroup->fAvgRank * iMembers + pMember->pJoinPrefs->fCurrentRank) / (iMembers + 1);
	pGroup->fAvgLevel = (pGroup->fAvgLevel * iMembers + pMember->iLevel) / (iMembers + 1);

}

static bool aslQueue_MatchMaking_MMR_GroupLikesMapType(QueueDef *pDef, QueueCustomMapData *pMapType, QueueMember*** peaReadyMembers, bool bAccountForTime)
{
	int i;

	for(i=0;i<eaSize(peaReadyMembers);i++)
	{
		QueueMember *pMember = (*peaReadyMembers)[i];

		if(pMember && pMember->pJoinPrefs && ea32Size(&pMember->pJoinPrefs->ePreferredGameTypes))
		{
			int j;
			if(bAccountForTime && pDef->MapRules.pMatchMakingRules && pDef->MapRules.pMatchMakingRules->fMapTimeOut)
			{
				int iTimeInQueue = timeSecondsSince2000() - pMember->iQueueEnteredTime;
				QueueMatchMakingRules *pRules = pDef->MapRules.pMatchMakingRules;

				//Find out if this member still cares about it's preferred game types
				if(iTimeInQueue >= pRules->fMapTimeOut)
					continue;
			}

			//Make sure every game type listed this member likes
			for(j=0;j<ea32Size(&pMapType->puiPVPGameModes);j++)
			{
				if(ea32Find(&pMember->pJoinPrefs->ePreferredGameTypes,pMapType->puiPVPGameModes[j]) == -1)
				{
					return false;
				}
			}
		}
	}

	return true;
}

static const char *aslQueue_MMR_MatchmakingFindGoodMap(QueueDef *pDef, QueueMember*** peaReadyMembers, PVPGameType *pGameTypeOut)
{
	const char **ppchPossibleMaps = NULL;
	QueueCustomMapData **ppMapTypes = NULL;
	int i;
	bool bAccountForTime = true;

	while(eaSize(&ppchPossibleMaps) == 0)
	{
		for(i=0;i<eaSize(&pDef->QueueMaps.eaCustomMapTypes);i++)
		{
			if(aslQueue_MatchMaking_MMR_GroupLikesMapType(pDef,pDef->QueueMaps.eaCustomMapTypes[i],peaReadyMembers,bAccountForTime))
			{
				eaPush(&ppMapTypes,pDef->QueueMaps.eaCustomMapTypes[i]);
			}	
		}

		if(eaSize(&ppMapTypes) == 0 && bAccountForTime == true)
		{
			bAccountForTime = false;
		}
		else
		{
			break;
		}
	}

	//pick a random map and game type
	if(eaSize(&ppMapTypes) > 0)
	{
		int j;
		i = randomIntRange(0,eaSize(&ppMapTypes)-1);
		
		if(pGameTypeOut && ea32Size(&ppMapTypes[i]->puiPVPGameModes))
		{
			j = randomIntRange(0,ea32Size(&ppMapTypes[i]->puiPVPGameModes)-1);
			(*pGameTypeOut) = ppMapTypes[i]->puiPVPGameModes[j];
		}

		j = randomIntRange(0,eaSize(&ppMapTypes[i]->ppchMaps)-1);
		return ppMapTypes[i]->ppchMaps[j];
	}
	
	return NULL;
}



static void aslQueue_MMR_FillMatchMaking(QueueInfo *pQueue, QueueInstance *pInstance, QueueDef *pDef, QueueMember*** peaReadyMembers)
{
	int i;
	QueueMatchMakerGroup **ppMatches = NULL;
	int iMin;
	int iMax;
	QueueMember **ppMembersGrouped = NULL;

	for(i=0;i<eaSize(peaReadyMembers);i++)
	{
		int g;
		bool bFoundGroup = false;
		QueueMember *pMember = (*peaReadyMembers)[i];

		if(!pMember->pJoinPrefs)
			continue;

		for(g=0;g<eaSize(&ppMatches);g++)
		{
			if(aslQueue_MMR_CheckMatchMaking(ppMatches[g],pMember,pDef))
			{
				aslQueue_MMR_AddToMatchGroup(ppMatches[g],pMember);
				bFoundGroup = true;
			}
		}

		if(!bFoundGroup)
		{
			QueueMatchMakerGroup *pNewGroup = calloc(sizeof(QueueMatchMakerGroup),1);
			
			eaPush(&ppMatches,pNewGroup);
			aslQueue_MMR_AddToMatchGroup(pNewGroup,pMember);
		}
	}

	for(i=0;i<eaSize(peaReadyMembers);i++)
	{
		int g;

		for(g=0;g<eaSize(&ppMatches);g++)
		{
			if(aslQueue_MMR_CheckMatchMaking(ppMatches[g],(*peaReadyMembers)[i],pDef))
			{
				aslQueue_MMR_AddToMatchGroup(ppMatches[g],(*peaReadyMembers)[i]);
			}
		}
	}

	//Check to see which groups have enough matches to start a new map

	iMin = queue_QueueGetMinPlayersEx(pDef, false, false, queue_InstanceIsPrivate(pInstance));
	iMax = queue_QueueGetMaxPlayers(pDef, false);

	for(i=0;i<eaSize(&ppMatches);i++)
	{
		if(eaSize(&ppMatches[i]->ppMembers) >= iMin)
		{
			int m, mGrouped = 0;
			QueueMember **ppFoundMembers = NULL;

			for(m=0;m<eaSize(&ppMatches[i]->ppMembers) && mGrouped<iMax;m++)
			{
				if(eaFind(&ppMembersGrouped,ppMatches[i]->ppMembers[m]) == -1)
				{
					eaPush(&ppFoundMembers,ppMatches[i]->ppMembers[m]);
					mGrouped++;
				}
			}

			if(mGrouped >= iMin)
			{
				//Start map with these members
				for(m=0;m<eaSize(&ppFoundMembers);m++)
				{
					ppFoundMembers[m]->pJoinPrefs->bFoundMatch = true;
				}

				//Find a map for these members
				if(!aslQueue_MMR_MatchMakingFindOpenMap(pQueue,pInstance,pDef,&ppFoundMembers))
				{
					PVPGameType eNewGameType = kPVPGameType_None;
					const char *pchNewMap = aslQueue_MMR_MatchmakingFindGoodMap(pDef,&ppFoundMembers,&eNewGameType);

					//Create a new map for this group
//					aslQueue_CreateNewMap(pQueue, pInstance, pDef, false, pchNewMap, eNewGameType);

					// I am going to break this. WOLF[14Mar13] It's awkward that we determine the gameType here. When we changed
					//   from using MapVars to pass the information, it became odd that the game type was determined before map creation.
					//   We should probably do this determination post the map being created if we have multiple types the map could be.

					aslQueue_CreateNewMap(pQueue, pInstance, pDef, false, pchNewMap);
					
				}


				eaPushEArray(&ppMembersGrouped,&ppFoundMembers);
			}
		}
	}
}

//  End MMR
//  
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  FillOpenMaps matching

static void aslQueue_FillOpenMaps(QueueInfo *pQueue, 
								  QueueInstance* pInstance, 
								  QueueDef *pQueueDef, 
								  QueueMember ***peaReadyMembers)
{
	S32 iMapIdx, iMapSize = eaSize(&pInstance->eaMaps);

	for (iMapIdx = 0; iMapIdx < iMapSize; iMapIdx++)
	{
		QueueMap *pMap = pInstance->eaMaps[iMapIdx];
		QueueMatch *pMatch = NULL;

		//Awaiting a transaction, don't schedule the map for new members
		if (pMap->bDirty)
			continue;

		//Check to see if the map is ready to accept members (based on map state and joinTimeLimit)
		if (!queue_MapAcceptingNewMembers(pMap, pQueueDef))
			continue;
		
		pMatch = StructCreate(parse_QueueMatch);

		//Cache the people currently on the map
		queue_GroupCacheWithState(pInstance, pQueueDef, pMap, PlayerQueueState_InQueue, PlayerQueueState_None, pMatch);

		// Check to see if the map can accept new members
		if (pMatch->iMatchSize < pQueue->iMaxMapSize)
		{
			//Then try to match members
			if (aslQueue_MapMatch(pQueue, pInstance, pQueueDef, pMap, peaReadyMembers, pMatch) > 0)
			{
				switch (pMap->eMapState)
				{
					xcase kQueueMapState_Open:
					{
						// Since this is an opened map, have to launch with a minimum number of players AND all groups are at their minimum
						if (pInstance->bAutoFill || pMatch->bAllGroupsValid)
						{
							//Remove the members that were matched
							aslQueue_MatchMembersRemoveFromReadyList(pMatch, peaReadyMembers);

							pMap->iLastServerUpdateTime = timeSecondsSince2000();

							pInstance->iFailedMatchCount = 0;

							if (queue_InstanceShouldCheckOffers(pQueueDef, pInstance->pParams))
							{
								aslQueue_MapUpdate(pQueue, pInstance, pMap, pMatch, kQueueMapState_LaunchPending, NULL);
								if (pMatch)
								{
									aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, pMatch, kQueueMapState_LaunchPending);
								}
							}
							else if (!pQueue->bNeverLaunchMaps)
							{
								aslQueue_LaunchMap(pQueue, pInstance, pMap, pMatch, pQueueDef);
							}
						}
						else if (eaSize(peaReadyMembers) >= (int)pQueue->iMinMapSize)
						{
							pInstance->iFailedMatchCount++;
							if (pInstance->iFailedMatchCount > QUEUE_BRUTE_FORCE_COUNT)
								pInstance->iFailedMatchCount = 0;
						}
						else
						{
							pInstance->iFailedMatchCount = 0;
						}
					}
					//Else if it's a launched map, just add whoever else got matched
					xcase kQueueMapState_LaunchPending:
					acase kQueueMapState_LaunchCountdown:
					acase kQueueMapState_Launched:
					acase kQueueMapState_Active:
					{
						//Update the offer validation response time
						if (pMap->eMapState != kQueueMapState_LaunchPending &&
							queue_InstanceShouldCheckOffers(pQueueDef, pInstance->pParams))
						{
							aslQueue_MapUpdateOfferValidationTime(pMap, peaReadyMembers);
						}
						//Remove the members that were matched
						aslQueue_MatchMembersRemoveFromReadyList(pMatch, peaReadyMembers);

						//Offer members that are in state InQueue
						aslQueue_MapUpdate(pQueue, pInstance, pMap, pMatch, kQueueMapState_None, NULL);

						if (pMatch)
						{
							aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, pMatch, pMap->eMapState);
						}
					}
				}
			}
		}

		// Get rid of the matching information
		StructDestroySafe(parse_QueueMatch, &pMatch);
	}
}

	///////////////////////////////////////////////////
	//
	//  FillOpenMap style map creation 


static S32 aslQueue_CanFillNewMap(QueueInfo *pQueue, QueueInstance *pInstance, QueueDef *pQueueDef, QueueMember ***peaReadyMembers)
{
	QueueMatch *pMatch;
	S32 bCanFill = false;

	if (pQueue->bAutoFill && eaSize(peaReadyMembers))
	{
		pInstance->bAutoFill = true;
		pQueue->bAutoFill = false;
		return true;
	}
	// Don't try to fill a new map if there are less than min map size in the queue
	if (eaSize(peaReadyMembers) < (int)pQueue->iMinMapSize)
	{
		return false;
	}

	pMatch = StructCreate(parse_QueueMatch);

	//Initialize the groups
	queue_InitMatchGroups(pQueueDef, pMatch);

	//Try to make a match
	aslQueue_MapMatch(pQueue, pInstance, pQueueDef, NULL, peaReadyMembers, pMatch);

	//If the match is large enough, then start creating a new map
	if (pMatch->bAllGroupsValid)
	{
		bCanFill = true;
		//Don't reset this, so when it gets to fill the open map, it'll also brute force it and hopefully pick the same group
		//pInstance->iFailedMatchCount = 0;
	}
	else if (eaSize(peaReadyMembers) >= (int)pQueue->iMinMapSize)
	{
		pInstance->iFailedMatchCount++;
		if (pInstance->iFailedMatchCount > QUEUE_BRUTE_FORCE_COUNT)
			pInstance->iFailedMatchCount = 0;
	}
	else
	{
		pInstance->iFailedMatchCount = 0;
	}

	StructDestroySafe(parse_QueueMatch, &pMatch);

	return bCanFill;
}

// This gives a rough count of members available to launch a new map.
//  WOLF[15Jan13] Remove attempts to factor in partially filled maps. They will only serve to limit map creation. We
//  may overcreate some maps, but that's probably okay. It's better than getting stuck not creating a map when we need to.
//  (imagine a small queue with a team of 3 and a team of 2 (5 required) and a single active map with 4 people and one hole.
//  We'd never try creating the needed map under the old code.
static bool aslQueue_EnoughPotentialMembersToCheckForLaunch(QueueInstance* pInstance, QueueDef* pQueueDef, QueueMember*** peaReadyMembers)
{
	S32 iLaunchingMaps = eaSize(&pInstance->eaNewMaps);
	S32 i, iMaxPerMap = 0, iMembersAvailable = eaSize(peaReadyMembers);

	// Don't launch more than 10 maps at once
	if(iLaunchingMaps >= 10)
	{
		return 0;
	}

	// find maximum per map
	for(i = 0; i < eaSize(&pQueueDef->eaGroupDefs); ++i)
	{
		iMaxPerMap += pQueueDef->eaGroupDefs[i]->iMax;
	}

	if(iMaxPerMap < 1)
	{
		iMaxPerMap = 1;
	}

	// reduce number available members by maps launching
	iMembersAvailable -= iMaxPerMap * iLaunchingMaps;

	// leave a few extra people to fill in holes (1 per map or match size)
	if (iLaunchingMaps > 0)
	{
		iMembersAvailable -= max(iMaxPerMap, iLaunchingMaps);
	}

	return (iMembersAvailable>0);
}


static void aslQueue_ManageMapCreation(QueueInfo* pQueue, 
								   QueueInstance* pInstance, 
								   QueueDef* pQueueDef, 
								   QueueMember*** peaReadyMembers)
{
	U32 uiTimeNow = timeSecondsSince2000();
	S32 i;
	//TODO(BH): Some sort of total queue maps launched limit?
	//TODO(BH): if (pQueueDef->iMaxLiveMaps && iLiveMaps < pQueueDef->iMaxLiveMaps)

	if(pInstance->pParams && uiTimeNow >= pInstance->uiNextMapLaunchTime && aslQueue_EnoughPotentialMembersToCheckForLaunch(pInstance, pQueueDef, peaReadyMembers))
	{
		if(aslQueue_CanFillNewMap(pQueue,pInstance,pQueueDef,peaReadyMembers))
		{
			aslQueue_CreateNewMap(pQueue,pInstance,pQueueDef,false,NULL);			
		}
	}

	// Clean up any over-created maps that have been around for a long time. 
	for(i = eaSize(&pInstance->eaNewMaps) - 1; i >= 0 ; --i)
	{
		if(pInstance->eaNewMaps[i] && timeSecondsSince2000() > pInstance->eaNewMaps[i]->iLastServerUpdateTime + SECONDS_PER_MINUTE * 2)
		{
			Errorf("Destroying pInstance->eaNewMaps[i] for queue %s, instance %d as more than 120 seconds have passed since it was created.",pQueueDef->pchName , pInstance->uiID);
			queue_DestroyNewMap(pInstance, pInstance->eaNewMaps[i]->uiMapCreateRequestID);
		}
	}
}


///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//


static void aslQueue_PlaceMembersIntoMaps(SA_PARAM_NN_VALID QueueInfo* pQueue, 
										  SA_PARAM_NN_VALID QueueInstance* pInstance, 
										  SA_PARAM_NN_VALID QueueDef* pDef,
										  QueueMember*** peaReadyMembers)
{
	if(pDef->MapRules.pMatchMakingRules)
	{
		aslQueue_MMR_FillMatchMaking(pQueue, pInstance, pDef, peaReadyMembers);
	}
	else
	{
		aslQueue_FillOpenMaps(pQueue, pInstance, pDef, peaReadyMembers);
		aslQueue_ManageMapCreation(pQueue, pInstance, pDef, peaReadyMembers);
	}
	
}

static void aslQueue_MemberStateUpdate(QueueInfo* pQueue, QueueDef* pDef)
{
	static QueueMember **eaReadyMembers = NULL;
	S32 iInstanceIdx, iInstanceCount = eaSize(&pQueue->eaInstances);
	
	for (iInstanceIdx = 0; iInstanceIdx < iInstanceCount; iInstanceIdx++)
	{
		QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];

		aslQueue_MemberStateUpdateInstance(pQueue, pInstance, pDef);
	}
	
	for (iInstanceIdx = 0; iInstanceIdx < iInstanceCount; iInstanceIdx++)
	{
		QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
		U32 uOwnerID = SAFE_MEMBER(pInstance->pParams, uiOwnerID);

		if (uOwnerID)
		{
			QueueMap* pMap;
			S32 iMemberIdx;

			if (pInstance->uiPlayersPreventingMapLaunch > 0)
			{
				continue;
			}
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				QueueMember* pMember = pInstance->eaUnorderedMembers[iMemberIdx];
				if (pMember->eState == PlayerQueueState_InQueue && pMember->iMapKey)
				{
					break;
				}
			}
			if (iMemberIdx < 0)
			{
				continue;
			}

			if (pMap = queue_FindActiveMapForPrivateInstance(pInstance, pDef))
			{
				QueueMatch QMatch = {0};
				queue_GroupCacheWithState(pInstance, 
						  pDef,
						  pMap, 
						  PlayerQueueState_InQueue, 
						  PlayerQueueState_None, 
						  &QMatch);

				// If the match is valid, place the members on the map
				if (queue_Match_Validate(&QMatch, pDef, pInstance->pParams, false, false, false))
				{
					if (pMap->eMapState == kQueueMapState_Open)
					{
						aslQueue_LaunchMap(pQueue, pInstance, pMap, &QMatch, pDef); 
					}
					else
					{
						aslQueue_MapUpdate(pQueue, pInstance, pMap, &QMatch, kQueueMapState_None, NULL);
						aslQueue_MatchMembersMakeOffer(pQueue, pInstance, pMap->iMapKey, &QMatch, pMap->eMapState);
					}
				}
				else if (pMap->eMapState == kQueueMapState_Open)
				{
					const char* pchReason = "Private match failure";
					// Otherwise close the map and send a fail message
					if (aslQueue_MapUpdate(pQueue, pInstance, pMap, NULL, kQueueMapState_Destroy,pchReason))
					{
						queue_SendResultMessage(uOwnerID, 0, pDef->pchName, QUEUE_PLAYERS_NOT_ENOUGH_PLAYERS_MESG);
					}
				}
				StructDeInit(parse_QueueMatch, &QMatch);
			}
		}
		else
		{
			aslQueue_GetReadyMembers(pQueue, pInstance, &eaReadyMembers);

			if (eaSize(&eaReadyMembers))
			{
				// Cache MaxMapSize, MinMapSize
				pQueue->iMaxMapSize = queue_QueueGetMaxPlayers(pDef, pInstance->bOvertime);
				pQueue->iMinMapSize = queue_QueueGetMinPlayersEx(pDef, pInstance->bOvertime, false, queue_InstanceIsPrivate(pInstance));
				
				aslQueue_PlaceMembersIntoMaps(pQueue, pInstance, pDef, &eaReadyMembers);
			}
			eaClearFast(&eaReadyMembers);
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Post transaction updates. One for maps. One for Members

// -Update Wait Time
// -Send Launch Cancel message
// -Log Map Destruction
static void aslQueue_PostTrans_MapUpdate(QueueInfo* pQueue, QMapUpdate* pMapUpdate)
{
	U32 uInstanceID = pMapUpdate->uiInstanceID;
	QueueDef* pDef = GET_REF(pQueue->hDef);
	QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);

	if (pInstance && pMapUpdate->uAverageWaitTime > 0)
	{
		if (ea32Size(&pInstance->piHistoricalWaits) == QUEUE_MAX_HISTORY)
		{
			ea32Pop(&pInstance->piHistoricalWaits);
		}
		ea32Insert(&pInstance->piHistoricalWaits, pMapUpdate->uWaitTime, 0);
	}

	if (SAFE_MEMBER2(pInstance, pParams, uiOwnerID) > 0 &&
		pMapUpdate->eState == kQueueMapState_Destroy &&
		(pMapUpdate->eLastState == kQueueMapState_Open ||
		 pMapUpdate->eLastState == kQueueMapState_LaunchCountdown))
	{
		aslQueue_SendPrivateChatMessageToMembers(pQueue, pInstance->uiID, QUEUE_CANCEL_MAP_LAUNCH_MESG, 0, 0, -1);
	}
	if (pMapUpdate->eState == kQueueMapState_Destroy)
	{
		ContainerID uMapID = queue_GetMapIDFromMapKey(pMapUpdate->iMapKey);
		U32 uPartitionID = queue_GetPartitionIDFromMapKey(pMapUpdate->iMapKey);

		// Log that the map was destroyed
		aslQueue_LogMapDestroyed(pDef, uMapID, uPartitionID, pMapUpdate->eLastState, pMapUpdate->pchReason);
	}
}

// -LeavePrivateChannel
// -Send wWarnings/Messages (if (MemberUpdate->pchMessageKey))
// -AutoTeam removal if Member is exiting
// -Some matchmaking
// -Map Transfer request
static void aslQueue_PostTrans_MemberUpdate(QueueInfo* pQueue, 
											QueueDef* pDef, 
											QMemberUpdate* pMemberUpdate,
											QueueMapMatch*** peaMapMatches)
{
	S32 i;
	U32 uInstanceID = pMemberUpdate->uiInstanceID;
	U32 uMemberID = pMemberUpdate->uiMemberID;
	S64 iMapKey = pMemberUpdate->iMapKey;
	U32 uMapID = queue_GetMapIDFromMapKey(iMapKey);
	U32 uPartitionID = queue_GetPartitionIDFromMapKey(iMapKey);
	QueueInstance* pInstance = eaIndexedGetUsingInt(&pQueue->eaInstances, uInstanceID);	
	bool bCheckOffers = queue_InstanceShouldCheckOffers(pDef, NULL);
	const char* pchQueueName = SAFE_MEMBER(pDef, pchName);

	// Handle removal of members from chat channels
	if (pMemberUpdate->eState == PlayerQueueState_InMap ||
		pMemberUpdate->eState == PlayerQueueState_Exiting || 
		pMemberUpdate->eState == PlayerQueueState_Limbo)
	{
		U32 uOrigOwnerID = queue_GetOriginalOwnerID(pInstance);
		
		if (uOrigOwnerID > 0) // Leave the private chat channel
		{
			RemoteCommand_gslQueue_LeavePrivateChatChannel(GLOBALTYPE_ENTITYPLAYER, 
														   uMemberID, 
														   uMemberID,
														   uOrigOwnerID, 
														   uInstanceID);
		}
	}

	// Handle player notifications
	if (pMemberUpdate->pchMessageKey)
	{
		if (pMemberUpdate->bWarning)
		{
			RemoteCommand_gslQueue_SendWarning(GLOBALTYPE_ENTITYPLAYER, 
											   uMemberID, 
											   uMemberID, 
											   0, 
											   pchQueueName, 
											   pMemberUpdate->pchMessageKey);
		}
		else
		{
			queue_SendResultMessage(uMemberID, 0, pchQueueName, pMemberUpdate->pchMessageKey);
		}
	}

	// Send updated match information to maps for players being removed due to Exiting State
	if (pMemberUpdate->eState == PlayerQueueState_Exiting)
	{
		if (pMemberUpdate->eLastState == PlayerQueueState_Limbo)
		{
			char pchString[256];
			sprintf(pchString, "Queue Limbo Timeout: Removing member [%d]", uMemberID); 
			aslQueue_Log(pDef, pInstance, pchString);
		}
		if (uMapID)
		{
			RemoteCommand_gslQueue_RemoveMemberFromMatch(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, uMemberID);
		}
	}

	// Some Matchmaking?
	if (pMemberUpdate->eState == PlayerQueueState_Countdown ||
		((pMemberUpdate->eState == PlayerQueueState_Offered ||
		  pMemberUpdate->eState == PlayerQueueState_Accepted) && !bCheckOffers))
	{
		QueueMapMatch* pMapMatch;
		for (i = eaSize(peaMapMatches)-1; i >= 0; i--)
		{
			pMapMatch = (*peaMapMatches)[i];
			if (pMapMatch->iMapKey == iMapKey)
				break;
		}
		if (i < 0)
		{
			pMapMatch = StructCreate(parse_QueueMapMatch);
			pMapMatch->iMapKey = iMapKey;
			pMapMatch->pMatch = StructCreate(parse_QueueMatch);
			if (pDef)
			{
				queue_InitMatchGroups(pDef, pMapMatch->pMatch);
			}
			for (i = eaSize(&pInstance->eaUnorderedMembers)-1; i >= 0; i--)
			{
				QueueMember* pMember = pInstance->eaUnorderedMembers[i];
				if (pMember->iMapKey == iMapKey &&
					(pMember->eState == PlayerQueueState_InMap ||
					 pMember->eState == PlayerQueueState_Limbo ||
					 pMember->eState == PlayerQueueState_Countdown ||
					 pMember->eState == PlayerQueueState_Accepted ||
					 pMember->eState == PlayerQueueState_Offered))
				{
					queue_Match_AddMember(pMapMatch->pMatch, 
										  pMember->iGroupIndex, 
										  pMember->iEntID, 
										  pMember->iTeamID);
				}
			}
			eaPush(peaMapMatches, pMapMatch);
		}
	}
	
	// Handle map transfers
	if ((pMemberUpdate->eState == PlayerQueueState_InMap &&
		 pMemberUpdate->eLastState == PlayerQueueState_Countdown) ||
		(pMemberUpdate->eState == PlayerQueueState_Accepted && !bCheckOffers))
	{
		QueueMap* pMap = NULL;
		if (pInstance)
			pMap = eaIndexedGetUsingInt(&pInstance->eaMaps, pMemberUpdate->iMapKey);
		if (pDef && pMap)
		{	
			QueueGroupDef* pGroupDef = eaGet(&pDef->eaGroupDefs, pMemberUpdate->iGroupIndex);
			if (pGroupDef)
			{
				QueueInstantiationInfo InstantiationInfo = {0};
				InstantiationInfo.iMapKey = pMap->iMapKey;
				InstantiationInfo.uInstanceID = pInstance->uiID;
				InstantiationInfo.pchQueueDef = pDef->pchName;
				InstantiationInfo.pchMapName = pMap->pchMapName;
				InstantiationInfo.pchSpawnName = allocAddString(pGroupDef->pchSpawnTargetName);
				InstantiationInfo.iGroupIndex = pMemberUpdate->iGroupIndex;

				//Transfer the player to the map
				RemoteCommand_gslQueue_MapTransferMember(GLOBALTYPE_ENTITYPLAYER, uMemberID, uMemberID, &InstantiationInfo);
			}
		}
	}
}

static void QueueUpdate_PostTransactionUpdate(SA_PARAM_NN_VALID QueueInfo* pQueue,
											  SA_PARAM_NN_VALID QUpdateData* pData)
{
	S32 i, iUpdateIdx;
	QueueDef* pDef = GET_REF(pQueue->hDef);
	QueueMapMatch** eaMapMatches = NULL;
	static U64 su_UpdateID = 0;

	//  We just finished transacting
	//  

	for (iUpdateIdx = 0; iUpdateIdx < eaSize(&pData->eaList); iUpdateIdx++)
	{
		QGeneralUpdate* pUpdate = pData->eaList[iUpdateIdx];
		
		if (pUpdate->pMapUpdate)
		{
			aslQueue_PostTrans_MapUpdate(pQueue, pUpdate->pMapUpdate);
		}
		else if (pUpdate->pMemberUpdate)
		{
			aslQueue_PostTrans_MemberUpdate(pQueue, 
											pDef,
											pUpdate->pMemberUpdate, 
											&eaMapMatches);
		}
	}

	// Send the latest match data to the GameServer (and from there possibly to the team server)
	for (i = eaSize(&eaMapMatches)-1; i >= 0; i--)
	{
		QueueMapMatch* pMapMatch = eaMapMatches[i];
		U32 uMapID = queue_GetMapIDFromMapKey(pMapMatch->iMapKey);
		U32 uPartitionID = queue_GetPartitionIDFromMapKey(pMapMatch->iMapKey);
		pMapMatch->pMatch->uUpdateID = ++su_UpdateID;	// prevent older remote commands from overwriting more current ones
		// Send match information to the map
		RemoteCommand_gslQueue_UpdateQueueMatch(GLOBALTYPE_GAMESERVER, uMapID, uPartitionID, pMapMatch->pMatch);
	}
	eaDestroyStruct(&eaMapMatches, parse_QueueMapMatch);
}






///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  UPDATE once-per-frame stuff

// Transaction spam limiter. Checks to see if any queues are still updating. Once a full batch is processed, we allow another update
static bool s_bReadyForNextUpdate = true;

static bool aslQueue_ReadyForNextUpdate(void)
{
	S32 iQueueIdx;
	for (iQueueIdx = eaSize(&s_LocalData.eaLocalQueues)-1; iQueueIdx >= 0; iQueueIdx--)
	{
		if (s_LocalData.eaLocalQueues[iQueueIdx]->bUpdating)
		{
			return false;
		}
	}
	return true;
}


// Return from aslQueue_tr_PerformUpdate which is called once per QueueID with all updates to that queue.
// We know all transactions are complete. Do any posttransaction updates
static void QueueUpdate_CB(TransactionReturnVal* pVal, QUpdateData* pData)
{
	QueueInfo* pQueue = aslQueue_GetQueueLocal(pData->uiQueueID);
	if (pQueue)
	{
		pQueue->bUpdating = false;

		s_bReadyForNextUpdate = aslQueue_ReadyForNextUpdate();
	}

	if (pVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		if (pQueue)
		{
			QueueUpdate_PostTransactionUpdate(pQueue, pData);
		}
	}
	else
	{
		QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
		if (pDef)
		{
			char pchBuffer[256];
			sprintf(pchBuffer, "aslQueue_tr_PerformUpdate transaction failed!");
			aslQueue_Log(pDef, NULL, pchBuffer);
		}
	}
}


static bool aslQueue_Update(void)
{
	static bool bInitLocalData = false;
	static U32 uiNextLogTime = 0;
	
	S32 i, iSize;
	U32 uiNow = timeSecondsSince2000();
	bool bLogInfo = false;

	// Transaction spam limiter. We only update once the last batch of tranactions goes through.
	if (!s_bReadyForNextUpdate)
	{
		return false;
	}
	if (uiNow >= uiNextLogTime)
	{
		bLogInfo = true;
		uiNextLogTime = uiNow + QUEUE_PERIODIC_LOG_TIME;
	}

	aslQueue_RandomQueuesUpdate();

	// Clear the QueueUpdateList
	eaClearStruct(&s_QueueUpdateList.eaQueues, parse_QUpdateData);
	
	iSize = aslQueue_UpdateLocalData();

	for (i = 0; i < iSize; i++)
	{
		QueueInfo* pLocalInfo = s_LocalData.eaLocalQueues[i];
		QueueDef *pQueueDef = GET_REF(pLocalInfo->hDef);
		
		if (!pLocalInfo || pLocalInfo->bDirty || !pQueueDef || uiNow < pLocalInfo->iNextFrameTime)
			continue;

		if (bLogInfo)
		{
			aslQueue_LogPeriodic(pLocalInfo, pQueueDef);
		}
		// In general, gather up Update records in a list, then transact them all at the same time. aslQueue_tr_PerformUpdate (QueueTransactions)

		//Cleanup "expired" instances (ShouldCleanupInstance). Check for new maps finishing loading. 
		aslQueue_InstanceUpdate(pLocalInfo, pQueueDef);

		//Timed map state changes and queue info update,  Removes members on finished maps
		aslQueue_MapStateUpdate(pLocalInfo, pQueueDef);

		//Process members states and update accordingly
		aslQueue_MemberStateUpdate(pLocalInfo, pQueueDef);
	}

	// Process the QueueUpdateList
	iSize = eaSize(&s_QueueUpdateList.eaQueues);

	for (i = 0; i < iSize; i++)
	{
		QUpdateData* pData = s_QueueUpdateList.eaQueues[i];
		QueueInfo* pQueue = aslQueue_GetQueueLocal(pData->uiQueueID);
		
		if (devassert(pQueue))
		{
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(QueueUpdate_CB, pData);
			pQueue->bUpdating = true;
			s_bReadyForNextUpdate = false;
			AutoTrans_aslQueue_tr_PerformUpdate(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, pData);
		}
	}
	return true;
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
//  QueueCreation stuff.


	////////////////////////////////////
	//  Instance AutoCreate

static void aslQueue_AutoCreateOneInstance(SA_PARAM_NN_VALID QueueInfo* pQueue, SA_PARAM_NN_VALID QueueInstanceParams* pParams)
{
	if (queue_FindInstance(pQueue, pParams))
	{
		return;
	}
	AutoTrans_aslQueue_tr_CreateInstance(NULL,GetAppGlobalType(),GLOBALTYPE_QUEUEINFO,pQueue->iContainerID,0,0,0,0,NULL,NULL,NULL,pParams,NULL);
}

static void aslQueue_AutoCreateInstancesWithMapIndex(SA_PARAM_NN_VALID QueueInfo* pQueue, 
													 SA_PARAM_NN_VALID QueueDef* pDef,
													 S32 iMapIdx)
{
	NOCONST(QueueInstanceParams) QParams = {0};
	QParams.pchMapName = allocAddString(queue_GetMapNameByIndex(pDef, iMapIdx));
	if (eaSize(&pDef->eaLevelBands) > 0)
	{
		S32 i;
		for (i = eaSize(&pDef->eaLevelBands)-1; i >= 0; i--)
		{
			QParams.iLevelBandIndex = i;
			aslQueue_AutoCreateOneInstance(pQueue, (QueueInstanceParams*)(&QParams));
		}
	}
	else
	{
		QParams.iLevelBandIndex = 0;
		aslQueue_AutoCreateOneInstance(pQueue, (QueueInstanceParams*)(&QParams));
	}
}

//create all possible instances from the queue def - if they don't already exist
static void aslQueue_AutoCreateInstances(SA_PARAM_NN_VALID QueueInfo* pQueue, SA_PARAM_NN_VALID QueueDef* pDef)
{
	if (pDef->Settings.bRandomMap)
	{
		aslQueue_AutoCreateInstancesWithMapIndex(pQueue, pDef, -1);
	}
	else
	{
		S32 iMapIdx, iMapCount = queue_GetMapIndexByName(pDef, NULL);
		for (iMapIdx = iMapCount-1; iMapIdx >= 0; iMapIdx--)
		{
			aslQueue_AutoCreateInstancesWithMapIndex(pQueue, pDef, iMapIdx);
		}
	}
}

///////////////////////////////////////
	//  Other creation. Including placing things into Limbo when we restart. 
	//  Create gets called when changing the queue dictionary.

static void PlaceMapsIntoLimbo_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct *pCBStruct = (QueueCBStruct*)pData;
	QueueInfo *pQueue = aslQueue_GetQueueLocal(pCBStruct->iQueueID);
	if (pQueue)
	{
		S32 i, j;
		for (i = eaSize(&pQueue->eaInstances)-1; i >= 0; i--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[i];
			for (j = eaSize(&pInstance->eaMaps)-1; j >= 0; j--)
			{
				QueueMap* pMap = pInstance->eaMaps[j];
				if (pMap) 
				{
					pMap->bDirty = false;
				}
			}
		}
	}
	StructDestroy(parse_QueueCBStruct, pCBStruct);
}


#define QUEUE_LIMBO_EXTRA_TIME 15
static void PlaceMembersIntoLimbo_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct* pCBStruct = (QueueCBStruct*)pData;
	QueueInfo* pQueue = aslQueue_GetQueueLocal(pCBStruct->iQueueID);
	if (pQueue)
	{
		S32 iInstanceIdx, iMemberIdx;
		U32 uiNow = timeSecondsSince2000();
		
		//The queue is no longer dirty
		pQueue->bDirty = false;
		
		//extra time for maps to check in (before declaring their members suspect and 'kickable')
		pQueue->iNextFrameTime = uiNow + QUEUE_LIMBO_EXTRA_TIME;

		//Reset the last map update to give maps time to check in
		for (iInstanceIdx = eaSize(&pQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
		{
			QueueInstance* pInstance = pQueue->eaInstances[iInstanceIdx];
			for (iMemberIdx = eaSize(&pInstance->eaUnorderedMembers)-1; iMemberIdx >= 0; iMemberIdx--)
			{
				pInstance->eaUnorderedMembers[iMemberIdx]->iLastMapUpdate = uiNow;
			}
		}
	}

	StructDestroy(parse_QueueCBStruct, pCBStruct);
}


static void RemoveAllMembers_CB(TransactionReturnVal *pReturn, void *pData)
{
	QueueCBStruct* pCBStruct = (QueueCBStruct*)pData;
	QueueInfo* pQueue = aslQueue_GetQueueLocal(pCBStruct->iQueueID);
	if (pQueue)
	{
		pQueue->bDirty = false;
	}

	StructDestroy(parse_QueueCBStruct, pCBStruct);
}


static void aslQueue_CreateCB(TransactionReturnVal *pVal, void *pData)
{
	char *pchName = (char*)pData;
	
	if (pVal->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
	{
		//TODO(BH): What to do if it fails?
	}
	else
	{
		QueueInfo* pQueue = aslQueue_CreateLocalDataByName(pchName);
		QueueDef* pDef = pQueue ? GET_REF(pQueue->hDef) : NULL;
		S32 iIdx = eaFindString(&s_eaPendingQueues, pchName);
		if (iIdx >= 0)
		{
			free(eaGet(&s_eaPendingQueues, iIdx));
			eaRemove(&s_eaPendingQueues, iIdx);
		}
		if (pDef)
		{
			if (pDef->Settings.bAlwaysCreate)
			{
				aslQueue_AutoCreateInstances(pQueue, pDef);
			}
		}
		else
		{
			Errorf("aslQueueServer: aslQueue_CreateCB failed to find QueueDef. Unable to check bAlwaysCreate.");
		}
	}
	SAFE_FREE(pchName);
}


// Called on initial load and reload of queues
void aslQueue_CreateNewQueues(S32 bStartingUp)
{
	QueueDef *pDef = NULL;
	RefDictIterator queuedefIter;
	
	ContainerIterator queueIter;
	QueueInfo *pQueue = NULL;

	char **eaDuplicateQueues = NULL;

	//Go through the queue dictionary, making new queue containers for anything that doesn't have one
	RefSystem_InitRefDictIterator(g_hQueueDefDict, &queuedefIter);
	while (pDef = (QueueDef*)RefSystem_GetNextReferentFromIterator(&queuedefIter))
	{		
		objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
		while (pQueue = objGetNextObjectFromIterator(&queueIter))
		{
			if (stricmp(pQueue->pchName,pDef->pchName) == 0)
				break;
		}
		objClearContainerIterator(&queueIter);
		if (!pQueue)
		{
			if (eaFindString(&s_eaPendingQueues, pDef->pchName) < 0)
			{
				//If we don't have a queue for this, create a new structure
				NOCONST(QueueInfo) *pNewQueue = StructCreateNoConst(parse_QueueInfo);
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(aslQueue_CreateCB, strdup(pDef->pchName));
				eaPush(&s_eaPendingQueues, strdup(pDef->pchName));
				
				//Fill in the basic info
				pNewQueue->pchName = StructAllocString(pDef->pchName);
				SET_HANDLE_FROM_STRING(g_hQueueDefDict, pDef->pchName, pNewQueue->hDef);

				//And request a new container from the ObjectDB
				objRequestContainerCreate(pReturn, GLOBALTYPE_QUEUEINFO, pNewQueue, objServerType(), objServerID());
				StructDestroyNoConstSafe(parse_QueueInfo, &pNewQueue);
			}
		}
		else
		{
			QueueInfo* pLocalQueue = aslQueue_CreateLocalData(pQueue);

			devassert(pLocalQueue);

			if (pDef->Settings.bAlwaysCreate)
			{
				aslQueue_AutoCreateInstances(pLocalQueue, pDef);
			}
			if (bStartingUp)
			{
				S32 iInstanceIdx, iMapIdx;
				U32 iCurrentTime = timeSecondsSince2000();
				
				//Place the members AND maps into limbo until they check-in.
				QueueCBStruct *pCBStruct = StructCreate(parse_QueueCBStruct);
				QueueCBStruct *pMapCBStruct = StructCreate(parse_QueueCBStruct);
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(PlaceMembersIntoLimbo_CB, pCBStruct);
				TransactionReturnVal *pMapReturn = objCreateManagedReturnVal(PlaceMapsIntoLimbo_CB, pMapCBStruct);
			
				pCBStruct->iQueueID = pLocalQueue->iContainerID;
				pMapCBStruct->iQueueID = pLocalQueue->iContainerID;

				// The queue is dirty, waiting for the limbo transaction to return
				pLocalQueue->bDirty = true;

				for (iInstanceIdx = eaSize(&pLocalQueue->eaInstances)-1; iInstanceIdx >= 0; iInstanceIdx--)
				{
					QueueInstance* pInstance = pLocalQueue->eaInstances[iInstanceIdx];
					for (iMapIdx = eaSize(&pInstance->eaMaps)-1; iMapIdx >= 0; iMapIdx--)
					{
						QueueMap* pMap = pInstance->eaMaps[iMapIdx];
						if (pMap)
						{
							pMap->iLastServerUpdateTime = iCurrentTime;
							pMap->bDirty = true;
						}
					}
				}
			
				AutoTrans_aslQueue_tr_PlaceOldMapsIntoLimbo(pMapReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pLocalQueue->iContainerID);
				AutoTrans_aslQueue_tr_PlaceOldMembersIntoLimbo(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pLocalQueue->iContainerID);
			}
			else
			{
				QueueCBStruct *pCBStruct = StructCreate(parse_QueueCBStruct);
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(RemoveAllMembers_CB, pCBStruct);

				pCBStruct->iQueueID = pLocalQueue->iContainerID;

				pLocalQueue->bDirty = true;
				AutoTrans_aslQueue_tr_RemoveAllMembers(pReturn, GLOBALTYPE_QUEUESERVER, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID);
			}
		}
	}

	//Remove old queues that don't exist anymore and remove duplicate queues
	//  Duplicates should only happen under old code where CreateNewQueues was being called on AppServers other than the QueueServer.
	//  Since those AppServers did not have the containers, we would create duplicates.
	objInitContainerIteratorFromType(GLOBALTYPE_QUEUEINFO, &queueIter);
	while (pQueue = objGetNextObjectFromIterator(&queueIter))
	{
		pDef = RefSystem_ReferentFromString(g_hQueueDefDict, pQueue->pchName);
		if (!pDef || (pDef && eaFindString(&eaDuplicateQueues, pDef->pchName) != -1))
		{
			QueueInfo* pLocalQueue = aslQueue_GetQueueLocal(pQueue->iContainerID);
			if (pLocalQueue)
			{
				//No longer process this queue, let it get destroyed. (Though it won't actually get destroyed)
				pLocalQueue->bDirty = true;
			}

			if (pDef)
			{
				Errorf("Found duplicate object of QueueInfo (%s) with container ID %d. Sending request to destroy the duplicate object.", pDef->pchName, pQueue->iContainerID);
			}

			objRequestContainerDestroy(NULL, GLOBALTYPE_QUEUEINFO, pQueue->iContainerID, objServerType(), objServerID());
		}
		else
		{
			eaPush(&eaDuplicateQueues, strdup(pDef->pchName));
		}
	}
	eaDestroyEx(&eaDuplicateQueues, NULL);
	objClearContainerIterator(&queueIter);
}


void aslQueue_ReloadQueues(const char *pchRelPath, int UNUSED_when)
{
	Queues_ReloadQueues(pchRelPath, UNUSED_when);

	// We need to do this separately from Queues_ReloadQueues or we will attempt to do container manipulation multiple times when
	//    Reload gets called from other App servers. This version should only be called from the QueueServer.
	aslQueue_CreateNewQueues(false);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// ServerDeath

// Remote called from Controller.c
AUTO_COMMAND_REMOTE ACMD_IFDEF(CONTROLLER);
void InformQueueServerOfGameServerDeath (ContainerID iMapID, bool bUnnaturalDeath)
{
	S32 iSize = eaSize(&s_LocalData.eaLocalQueues);
	S32 i;

	for(i=0;i<iSize;i++)
	{
		QueueInfo* pLocalInfo = s_LocalData.eaLocalQueues[i];
		QueueDef *pQueueDef = GET_REF(pLocalInfo->hDef);
		int iInstance, iMap;

		for(iInstance=0;iInstance<eaSize(&pLocalInfo->eaInstances);iInstance++)
		{
			QueueInstance* pInstance = pLocalInfo->eaInstances[iInstance];
			for(iMap=0;iMap<eaSize(&pInstance->eaMaps);iMap++)
			{
				QueueMap* pMap = pInstance->eaMaps[iMap];
				if(iMapID == queue_GetMapIDFromMapKey(pMap->iMapKey))
				{
					pMap->bGameServerDied = true;
				}
			}
		}
	}
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//
// Main Once-per-frame

int QueueServerOncePerFrame(F32 fElapsed)
{
	static bool bOnce = false;
	static F32 fTime = 0.0f;
	
	if (!bOnce)
	{
		loadstart_printf("Creating new queues for those that don't have a container...");
		aslQueue_CreateNewQueues(true);
		loadend_printf("Done");
		
		RemoteCommand_InformControllerOfServerState(GLOBALTYPE_CONTROLLER, 0, GetAppGlobalType(), gServerLibState.containerID, "ready");
		ATR_DoLateInitialization();

		printf("Server completely initialized.\n");
		bOnce = true;
		s_bQueueServerReady = true;
	}

	fTime += fElapsed;
	if (fTime < 1.0f || !aslQueue_Update())
		return(0);

	fTime = 0.0f;
	return 1;
}

AUTO_RUN;
int aslQueue_RegisterServer(void)
{
	aslRegisterApp(GLOBALTYPE_QUEUESERVER, QueueServerLibInit, APPSERVERTYPEFLAG_NOPIGGSINDEVELOPMENT);
	return 1;
}





#include "aslQueue_h_ast.c"
