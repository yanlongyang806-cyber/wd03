/***************************************************************************
*     Copyright (c) 2006-2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslTeamUp.h"

#include "Message.h"
#include "TeamUpCommon.h"
#include "Entity.h"
#include "GlobalTypes.h"
#include "Message.h"
#include "referencesystem.h"
#include "StringCache.h"
#include "EntityLib.h"
#include "chatCommon.h"
#include "WorldGrid.h"
#include "Player.h"
#include "GameServerLib.h"
#include "rand.h"

#include "AutoGen/gslTeamUp_h_ast.h"
#include "AutoGen/TeamUpCommon_h_ast.h"
#include "AutoGen/Message_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ChatServer_autogen_RemoteFuncs.h"

TeamUpPartition **ppTeamUpPartitions = NULL;

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

AUTO_RUN;
void gslTeamUp_AutoRun(void)
{
	eaIndexedEnable(&ppTeamUpPartitions,parse_TeamUpPartition);
}

TeamUpPartition *gslTeamUp_GetPartition(S32 iPartitionIdx)
{
	int i;
	i = eaIndexedFindUsingInt(&ppTeamUpPartitions,iPartitionIdx);

	// return partition if valid
	if(i != -1)
	{
		return ppTeamUpPartitions[i];
	}

	return NULL;	// this will likely cause an assert later
}

void gslTeamUp_PartitionLoad(S32 iPartitionIdx)
{
	TeamUpPartition *pNewPartition = NULL;

	PERFINFO_AUTO_START_FUNC();

	pNewPartition = StructCreate(parse_TeamUpPartition);

	pNewPartition->iPartitionIdx = iPartitionIdx;
	eaIndexedEnable(&pNewPartition->ppTeamUps,parse_TeamUpInstance);

	eaIndexedAdd(&ppTeamUpPartitions,pNewPartition);

	PERFINFO_AUTO_STOP();
}

void gslTeamUp_PartitionUnload(S32 iPartitionIdx)
{
	TeamUpPartition *pPartition = gslTeamUp_GetPartition(iPartitionIdx);

	if(pPartition)
	{
		eaFindAndRemove(&ppTeamUpPartitions,pPartition);
		StructDestroy(parse_TeamUpPartition,pPartition);
	}
}

static void aslTeamUp_MemberJoinTeamChat(int iPartitionIdx, U32 uTeamID, U32 uAccountID, U32 uEntID, U32 uServerID)
{
	char pchTeamChannelName[512];
	teamUp_MakeTeamChannelNameFromID(SAFESTR(pchTeamChannelName), iPartitionIdx, uTeamID, uServerID, zmapInfoGetPublicName(NULL));
	RemoteCommand_ChatServerJoinOrCreateChannel_Special(GLOBALTYPE_CHATSERVER, 0, uAccountID, uEntID, 
		pchTeamChannelName, CHANNEL_SPECIAL_TEAMUP);
}

static void aslTeamUp_MemberRemoveFromTeamChat(int iPartitionIdx, U32 uTeamID, U32 uAccountID, U32 uEntID, U32 uServerID)
{
	char pchTeamChannelName[512];
	teamUp_MakeTeamChannelNameFromID(SAFESTR(pchTeamChannelName), iPartitionIdx, uTeamID, uServerID, zmapInfoGetPublicName(NULL));
	RemoteCommand_ChatServerLeaveChannel_Special(GLOBALTYPE_CHATSERVER, 0, uAccountID, uEntID, pchTeamChannelName);
}

void gslTeamUp_UpdateGroup(int iPartitionIdx, TeamUpInstance *pInstance, int iGroupIdx)
{
	TeamUpGroup *pGroup = eaIndexedGetUsingInt(&pInstance->ppGroups,iGroupIdx);
	int i;

	for(i=0;i<eaSize(&pInstance->ppGroups);i++)
	{
		int m;

		for(m=0;m<eaSize(&pInstance->ppGroups[i]->ppMembers);m++)
		{
			Entity *pEntity = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYPLAYER,pInstance->ppGroups[i]->ppMembers[m]->iEntID);

			if(pEntity)
			{
				if(pGroup)
					ClientCmd_gclUpdateTeamUpGroup(pEntity,pGroup);
				else
					ClientCmd_gclRemoveTeamUpGroup(pEntity,iGroupIdx);
			}
		}
	}
}
	
void gslTeamUp_InitGroup(Entity *pEntity, TeamUpInstance *pInstance)
{
	if(pEntity && eaSize(&pInstance->ppGroups))
	{
		TeamUpInit *pInit = StructCreate(parse_TeamUpInit);

		eaCopyStructs(&pInstance->ppGroups,&pInit->ppGroups,parse_TeamUpGroup);
		ClientCmd_gclInitTeamUp(pEntity,pInit);
		StructDestroy(parse_TeamUpInit,pInit);
	}
}

TeamUpInstance *gslTeamUp_CreateNewInstance(S32 iPartitionIdx, const char *pchKey, DisplayMessage *pMessage, TeamUpFlags eFlags)
{
	TeamUpInstance *pNewInstance = StructCreate(parse_TeamUpInstance);
	TeamUpPartition *pPartition = gslTeamUp_GetPartition(iPartitionIdx);

	pNewInstance->iTeamKey = pPartition->sTeamUpKey++;

	if(pchKey)
		pNewInstance->pchName = allocAddString(pchKey);

	pNewInstance->eFlags = eFlags;

	if(pMessage && IS_HANDLE_ACTIVE(pMessage->hMessage))
	{
		SET_HANDLE_FROM_STRING(gMessageDict,REF_HANDLE_GET_STRING(pMessage->hMessage),pNewInstance->msgDisplayName.hMessage);
	}
		
	eaIndexedEnable(&pNewInstance->ppGroups,parse_TeamUpGroup);

	eaPush(&pPartition->ppTeamUps,pNewInstance);

	return pNewInstance;
}

TeamUpInstance *gslTeamUpInstance_FromKey(S32 iPartitionIdx, U32 sKey)
{
	TeamUpPartition *pPartition = gslTeamUp_GetPartition(iPartitionIdx);
	int i;

	i = eaIndexedFindUsingInt(&pPartition->ppTeamUps,sKey);

	if(i>-1)
		return pPartition->ppTeamUps[i];

	return NULL;
}

TeamUpInstance *gslTeamUpInstance_FromString(S32 iPartitionIdx, const char *pchKey)
{
	TeamUpPartition *pPartition = gslTeamUp_GetPartition(iPartitionIdx);
	int i;

	for(i=0;i<eaSize(&pPartition->ppTeamUps);i++)
	{
		if(pPartition->ppTeamUps[i]->pchName == pchKey)
			return pPartition->ppTeamUps[i];
	}

	return NULL;
}

void gslTeamUp_RemoveInstanceByName(S32 iPartitionIdx, const char *pchKey)
{
	TeamUpPartition *pPartition = gslTeamUp_GetPartition(iPartitionIdx);
	TeamUpInstance *pInstance = gslTeamUpInstance_FromString(iPartitionIdx,pchKey);

	if(pPartition && pInstance)
	{
		eaFindAndRemove(&pPartition->ppTeamUps,pInstance);
	}
}

TeamUpGroup *gslTeamUpInstance_AddGroup(TeamUpInstance *pInstance)
{
	int iGroupIdx = 0;
	TeamUpGroup *pNewGroup = StructCreate(parse_TeamUpGroup);

	while(eaIndexedFindUsingInt(&pInstance->ppGroups,iGroupIdx) > -1)
		iGroupIdx++;

	pNewGroup->iGroupIndex = iGroupIdx;

	eaIndexedAdd(&pInstance->ppGroups,pNewGroup);

	return pNewGroup;
}

void gslTeamUpGroup_AddMemberEx(Entity *pEntity, TeamUpGroup *pGroup, TeamUpMember *pMember)
{
	eaPush(&pGroup->ppMembers,pMember);

	if(pEntity->pPlayer)
	{
		int iPartitionIdx = entGetPartitionIdx(pEntity);
		U32 uServerID = gGSLState.gameServerDescription.baseMapDescription.containerID;

		if(pEntity && pEntity->pPlayer)
			aslTeamUp_MemberJoinTeamChat(iPartitionIdx, pEntity->pTeamUpRequest->uTeamID, pEntity->pPlayer->accountID, pEntity->myContainerID, uServerID);
	}
}

TeamUpMember *gslTeamUpGroup_AddMember(TeamUpGroup *pGroup, Entity *pEntity)
{
	char idBuf[128];
	TeamUpMember *pNewMember = StructCreate(parse_TeamUpMember);

	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER),ContainerIDToString(pEntity->myContainerID, idBuf),pNewMember->hEnt);

	pNewMember->iEntID = pEntity->myContainerID;

	gslTeamUpGroup_AddMemberEx(pEntity,pGroup,pNewMember);

	pEntity->pTeamUpRequest->iGroupIndex = pGroup->iGroupIndex;
	pEntity->pTeamUpRequest->eState = kTeamUpState_Member;

	return pNewMember;
}

void gslTeamUpInstance_RemoveGroup(TeamUpInstance *pInstance, TeamUpGroup *pGroup)
{
	if(eaFindAndRemove(&pInstance->ppGroups,pGroup))
	{
		StructDestroy(parse_TeamUpGroup,pGroup);
	}
}

bool gslTeamUpGroup_RemoveMember(Entity *pEntity, TeamUpInstance *pInstance, TeamUpGroup *pGroup, TeamUpMember *pMember, bool bDeleteMemeber)
{
	if(pGroup && pMember)
	{
		int i = eaFind(&pGroup->ppMembers,pMember);

		if(i>-1)
		{
			eaRemove(&pGroup->ppMembers,i);

			if(pEntity && pEntity->pPlayer)
			{
				int iPartitionIdx = entGetPartitionIdx(pEntity);
				U32 uServerID = gGSLState.gameServerDescription.baseMapDescription.containerID;

				aslTeamUp_MemberRemoveFromTeamChat(iPartitionIdx, pEntity->pTeamUpRequest->uTeamID, pEntity->pPlayer->accountID, pEntity->myContainerID, uServerID);
			}

			if(bDeleteMemeber)
			{
				StructDestroy(parse_TeamUpMember, pMember);
			}

			if(eaSize(&pGroup->ppMembers) == 0)
				gslTeamUpInstance_RemoveGroup(pInstance,pGroup);


			return true;
		}
	}

	return false;
}

TeamUpMember *gslTeamUpInstance_FindMember(TeamUpInstance *pInstance, TeamUpGroup **ppOutGroup, ContainerID eEntID)
{
	int i;

	for(i=0;i<eaSize(&pInstance->ppGroups);i++)
	{
		int m;

		for(m=0;m<eaSize(&pInstance->ppGroups[i]->ppMembers);m++)
		{
			if(pInstance->ppGroups[i]->ppMembers[m]->iEntID == eEntID)
			{
				if(ppOutGroup)
					*ppOutGroup = pInstance->ppGroups[i];

				return pInstance->ppGroups[i]->ppMembers[m];
			}
		}
	}

	return NULL;
}

bool gslTeamUp_EntityJoinTeam(int iPartitionIdx, Entity *pEntity, TeamUpInstance *pInstance)
{
	int i;
	TeamUpGroup *pGroup = NULL;
	TeamUpMember *pNewMember = NULL;

	if(!pInstance || !pEntity)
		return false;

	//Check to see that the entity isn't already in a team

	//Add the entity to an existing group if possible
	for(i=0;i<eaSize(&pInstance->ppGroups);i++)
	{
		if(eaSize(&pInstance->ppGroups[i]->ppMembers) < gConf.iMaxTeamUpGroupSize)
		{
			pGroup = pInstance->ppGroups[i];
		}
	}

	if(!pGroup)
	{
		pGroup = gslTeamUpInstance_AddGroup(pInstance);
	}

	pNewMember = gslTeamUpGroup_AddMember(pGroup,pEntity);

	gslTeamUp_UpdateGroup(iPartitionIdx,pInstance,pGroup->iGroupIndex);

	gslTeamUp_InitGroup(pEntity,pInstance);

	return true;
}

bool gslTeamUp_EntitiesJoinTeam(Entity ***pppEntites, TeamUpInstance *pInstance)
{
	int i;
	TeamUpGroup *pGroup = NULL;

	if(eaSize(pppEntites) == 0 || !pInstance)
		return false;

	//Make sure entities are not already in a group

	if(eaSize(pppEntites) <= gConf.iMaxTeamUpGroupSize)
	{
		for(i=0;i<eaSize(&pInstance->ppGroups);i++)
		{
			if(eaSize(&pInstance->ppGroups[i]->ppMembers) <= gConf.iMaxTeamUpGroupSize - eaSize(pppEntites))
			{
				pGroup = pInstance->ppGroups[i];
			}
		}

		if(!pGroup)
		{
			pGroup = gslTeamUpInstance_AddGroup(pInstance);
		}

		for(i=0;i<eaSize(pppEntites);i++)
		{
			gslTeamUpGroup_AddMember(pGroup,(*pppEntites)[i]);
		}
	}

	return true;
}

// -1 passed in for iGroupIdx is allowed here
bool gslTeamUp_GroupChangeRequest(Entity *pEntity, int iGroupIdx)
{
	TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(entGetPartitionIdx(pEntity),pEntity->pTeamUpRequest->uTeamID);
	TeamUpGroup *pOldGroup = NULL;
	TeamUpGroup *pNewGroup = NULL;
	TeamUpMember *pMember = NULL;
	int iOldGroupIdx;

	if(!pInstance)
		return false;

	pMember = gslTeamUpInstance_FindMember(pInstance,&pOldGroup,pEntity->myContainerID);

	if(!pMember || !pOldGroup || pOldGroup->iGroupIndex == iGroupIdx)
		return false;

	pNewGroup = eaIndexedGetUsingInt(&pInstance->ppGroups,iGroupIdx);

	if(!pNewGroup)
	{
		pNewGroup = gslTeamUpInstance_AddGroup(pInstance);
	}

	if(eaSize(&pNewGroup->ppMembers) >= gConf.iMaxTeamUpGroupSize)
		return false;

	iOldGroupIdx = pOldGroup->iGroupIndex;

	gslTeamUpGroup_RemoveMember(pEntity,pInstance,pOldGroup,pMember, false);
	gslTeamUpGroup_AddMemberEx(pEntity,pNewGroup,pMember);

	gslTeamUp_UpdateGroup(entGetPartitionIdx(pEntity),pInstance,iOldGroupIdx);
	gslTeamUp_UpdateGroup(entGetPartitionIdx(pEntity),pInstance,pNewGroup->iGroupIndex);

	pEntity->pTeamUpRequest->iGroupIndex = pNewGroup->iGroupIndex;

	return true;
}

bool gslTeamUp_LeaveTeam(Entity *pEntity, bool bForced)
{
	TeamUpInstance *pInstance = pEntity && pEntity->pTeamUpRequest ? gslTeamUpInstance_FromKey(entGetPartitionIdx(pEntity),pEntity->pTeamUpRequest->uTeamID) : NULL;
	TeamUpGroup *pGroup = NULL;
	TeamUpMember *pMember = pInstance ? gslTeamUpInstance_FindMember(pInstance,&pGroup,pEntity->myContainerID) : NULL;

	if(!pEntity || !pEntity->pTeamUpRequest)
		return false;

	if(pInstance && pGroup && pMember && (bForced == true || pInstance->eFlags != kTeamUpFlags_Locked))
	{
		int iGroupIdx = pGroup->iGroupIndex;

		gslTeamUpGroup_RemoveMember(pEntity,pInstance,pGroup,pMember, true);
		gslTeamUp_UpdateGroup(entGetPartitionIdx(pEntity),pInstance,iGroupIdx);
	}
	else
	{
		return false;
	}

	StructDestroy(parse_TeamUpRequest,pEntity->pTeamUpRequest);
	pEntity->pTeamUpRequest = NULL;

	entity_SetDirtyBit(pEntity,parse_Entity,pEntity,false);

	return true;
}

void gslTeamUp_AddTeamRequest(Entity *pEntity, TeamUpInstance *pInstance)
{
	if(pEntity->pTeamUpRequest && gslTeamUp_LeaveTeam(pEntity,false) == false)
		return;

	if(pInstance)
	{
		TeamUpRequest *pNewRequest = StructCreate(parse_TeamUpRequest);

		pNewRequest->eState = kTeamUpState_Invite;
		pNewRequest->iGroupIndex = -1;
		pNewRequest->uTeamID = pInstance->iTeamKey;
		if(IS_HANDLE_ACTIVE(pInstance->msgDisplayName.hMessage))
			SET_HANDLE_FROM_STRING(gMessageDict,REF_HANDLE_GET_STRING(pInstance->msgDisplayName.hMessage),pNewRequest->msgDisplayMessage.hMessage);

		pEntity->pTeamUpRequest = pNewRequest;
	}
}

void gslTeamUp_EntityJoinTeamWithoutRequest(int iPartitionIdx, Entity *pEntity, TeamUpInstance *pInstance)
{
	if(pInstance)
	{
		gslTeamUp_AddTeamRequest(pEntity,pInstance);
		gslTeamUp_EntityJoinTeam(iPartitionIdx,pEntity,pInstance);
	}
}

void gslTeamUp_HandleTeamUpRequest(Entity *pEntity, const char *pchTeamName)
{
	TeamUpInstance *pCurrentInstance = NULL;

	if(pEntity->pTeamUpRequest)
	{
		pCurrentInstance = gslTeamUpInstance_FromKey(entGetPartitionIdx(pEntity),pEntity->pTeamUpRequest->uTeamID);

		if(pCurrentInstance->pchName == pchTeamName)
		{
			return;
		}
	}

	if(!pchTeamName && !pCurrentInstance)
		return;

	if(pchTeamName && !pEntity->pTeamUpRequest)
	{
		gslTeamUp_AddTeamRequest(pEntity,gslTeamUpInstance_FromString(entGetPartitionIdx(pEntity),pchTeamName));
		return;
	}

	if(!pchTeamName && pEntity->pTeamUpRequest->eState == kTeamUpState_Invite)
	{
		gslTeamUp_LeaveTeam(pEntity,false);
		return;
	}

	if(!pchTeamName && pEntity->pTeamUpRequest->eState == kTeamUpState_Member)
	{
		gslTeamUp_LeaveTeam(pEntity,false);
		return;
	}
}

void teamup_GetOnMapEntIds(int iPartitionIdx, int **res_ids, int teamInstanceID)
{
	TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(iPartitionIdx,teamInstanceID);

	if(pInstance)
	{
		int i,m;

		for(i=0;i<eaSize(&pInstance->ppGroups);i++)
		{
			for(m=0;m<eaSize(&pInstance->ppGroups[i]->ppMembers);m++)
			{
				ea32Push(res_ids,pInstance->ppGroups[i]->ppMembers[m]->iEntID);
			}
		}
	}
}

void teamup_getMembers(Entity *pEntity, TeamUpMember ***pppMembers)
{
	if(pEntity && pEntity->pTeamUpRequest && pEntity->pTeamUpRequest->eState == kTeamUpState_Member)
	{
		int i;
		TeamUpInstance *pInstance = gslTeamUpInstance_FromKey(entGetPartitionIdx(pEntity), pEntity->pTeamUpRequest->uTeamID);

		for(i=0;i<eaSize(&pInstance->ppGroups);i++)
		{
			int m;

			for(m=0;m<eaSize(&pInstance->ppGroups[i]->ppMembers);m++)
			{
				eaPush(pppMembers,pInstance->ppGroups[i]->ppMembers[m]);
			}
		}
	}
}

U32 teamup_GetRewardIndex(int iPartitionIdx, S32 iTeamSize)
{
	TeamUpPartition *pTeamUpPart = gslTeamUp_GetPartition(iPartitionIdx);
	if(pTeamUpPart)
	{
		if(!pTeamUpPart->bRewardIndexInited)
		{
			pTeamUpPart->bRewardIndexInited = true;
			pTeamUpPart->uRewardIndex = randomIntRange(0, iTeamSize);
		}
		return pTeamUpPart->uRewardIndex;
	}

	return 0;
}

void teamup_SetRewardIndex(int iPartitionIdx, U32 uRewardIndex)
{
	TeamUpPartition *pTeamUpPart = gslTeamUp_GetPartition(iPartitionIdx);
	if(pTeamUpPart)
	{
		pTeamUpPart->uRewardIndex = uRewardIndex;
	}
}

#include "AutoGen/gslTeamUp_h_ast.c"