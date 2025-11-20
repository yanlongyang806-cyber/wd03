/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslTeamCorral.h"
#include "Entity.h"
#include "EntityLib.h"
#include "TeamCommands.h"
#include "mission_common.h"
#include "gslWaypoint.h"


#include "gslTeamCorral_h_ast.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"

#define MISSIONINFO_UPDATE_TIME 6
#define MISSIONINFO_POKE_TIME 3
#define ENTITY_TIMEOUT_TIME 10


static TeamCorralInfo **s_eaTeamCorrals;


TeamCorralInfo *gslTeam_FindTeamCorralInfo(Entity *pEntity)
{
	Team* pTeam = team_GetTeam( pEntity );
	S32 i;

	if ( pEntity==NULL )
		return NULL;

	for (i = 0; i < eaSize(&s_eaTeamCorrals); ++i)
	{
		TeamCorralInfo *pCorral = s_eaTeamCorrals[i];

		if (pCorral && pCorral->uiPrimaryPlayerID == entGetContainerID(pEntity) ||
			(pTeam && pTeam->iContainerID == pCorral->uiTeamID) )
		{
			return pCorral;
		}
	}

	return NULL;
}

TeamCorralInfo *gslTeam_FindTeamCorralInfoForTeam(Team *pTeam)
{
	S32 i;

	if ( pTeam == NULL )
		return NULL;

	for (i = 0; i < eaSize(&s_eaTeamCorrals); ++i)
	{
		TeamCorralInfo *pCorral = s_eaTeamCorrals[i];

		if (pTeam->iContainerID == pCorral->uiTeamID)
		{
			return pCorral;
		}
	}

	return NULL;
}

static Entity *gslTeam_FindEntityForID(TeamCorralInfo *pInfo, ContainerID id)
{
	S32 i;
	Entity **eaEntities = NULL;
	TeamMember **eaMembers = NULL;
	Entity *pLeader = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pInfo->uiPrimaryPlayerID);

	team_GetTeamListSelfFirst(pLeader, &eaEntities, &eaMembers, true, false);

	for (i = 0; i < eaSize(&eaEntities); ++i)
	{
		Entity *pEnt = eaEntities[i];

		if (pEnt && entGetContainerID(pEnt) == id)
		{
			return pEnt;
		}
	}

	return NULL;
}

void gslTeam_AddEntityToTeamCorral(Entity *pEnt, TeamMember *pTeamMember, TeamCorralInfo *pInfo)
{
	if (pEnt && pInfo)
	{
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
		TeamCorralMember *pMember = StructCreate(parse_TeamCorralMember);
		//pMember->iEntRef = entGetRef(pEnt);
		pMember->iEntID = entGetContainerID(pEnt);
		pMember->bIsReady = false;
		pMember->bWindowShown = false;
		if (pTeamMember)
		{
			pMember->iMapContainerID = pTeamMember->iMapContainerID;
		}

		if (pMissionInfo)
		{
			waypoint_FlagWaypointRefresh(pMissionInfo);
		}

		eaPush(&pInfo->eaMembers, pMember);
	}
}

void gslTeam_RemoveMemberFromTeamCorral(TeamCorralInfo *pInfo, S32 index)
{
	if (pInfo && pInfo->eaMembers)
	{
		TeamCorralMember *pMember = pInfo->eaMembers[index];

		if (pMember)
		{
			Entity *pEnt = entFromContainerID(pInfo->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);

			if (!pEnt)
			{
				pEnt = gslTeam_FindEntityForID(pInfo, pMember->iEntID);
			}

			if (pEnt)
			{
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
				if(pMember->iMapContainerID)
					RemoteCommand_gslTeam_HideTeamCorralWindowOnOtherServer(GLOBALTYPE_GAMESERVER, pMember->iMapContainerID, pInfo, pMember->iEntID);
				gslTeam_HideTeamCorralWindow(pEnt);
				gslTeam_HidePartyCircleOnClient(pEnt);

				if (pMissionInfo)
				{
					waypoint_FlagWaypointRefresh(pMissionInfo);
				}
			}

			StructDestroy(parse_TeamCorralMember, pMember);
		}

		eaRemove(&pInfo->eaMembers, index);
	}
}

TeamCorralInfo *gslTeam_CreateTeamCorral(Entity *pPlayerEnt, bool bAllowSingleMember)
{
	TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfo(pPlayerEnt);
	Entity **eaTeamEntities = NULL;
	TeamMember **eaTeamMembers = NULL;
	S32 i;
	
	if (!pPlayerEnt)
	{
		return NULL;
	}

	if(pCorral)
	{
		return pCorral;
	}
	else
	{
		pCorral = StructCreate(parse_TeamCorralInfo);
	}

	pCorral->uiPrimaryPlayerID = entGetContainerID(pPlayerEnt);
	pCorral->uiTeamID = team_GetTeamID(pPlayerEnt);
	pCorral->iPartitionIdx = entGetPartitionIdx(pPlayerEnt);

	team_GetTeamListSelfFirst(pPlayerEnt, &eaTeamEntities, &eaTeamMembers, true, false);

	for (i = 0; i < eaSize(&eaTeamEntities); ++i)
	{
		gslTeam_AddEntityToTeamCorral(eaTeamEntities[i], eaTeamMembers[i], pCorral);
	}

	entGetPos(pPlayerEnt, pCorral->v3CenterPoint);

	pCorral->bAllowSingleMemberCorral = bAllowSingleMember;

	eaPush(&s_eaTeamCorrals, pCorral);

	return pCorral;
}

void gslTeam_DestroyTeamCorral(TeamCorralInfo *pCorral)
{
	S32 i;

	if (pCorral && pCorral->eaMembers)
	{
		for (i = 0; i < eaSize(&pCorral->eaMembers); ++i)
		{
			TeamCorralMember *pMember =  pCorral->eaMembers[i];
			Entity *pEnt = gslTeam_FindEntityForID(pCorral, pMember->iEntID);
			if (pEnt)
			{
				MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
				if (pMember->iMapContainerID)
					RemoteCommand_gslTeam_HideTeamCorralWindowOnOtherServer(GLOBALTYPE_GAMESERVER, pMember->iMapContainerID, pCorral, pMember->iEntID);
				gslTeam_HideTeamCorralWindow(pEnt);
				gslTeam_HidePartyCircleOnClient(pEnt);

				if (pMissionInfo)
				{
					waypoint_FlagWaypointRefresh(pMissionInfo);
				}
			}
		}

		eaClearStruct(&pCorral->eaMembers, parse_TeamCorralMember);
	}

	for (i = 0; i < eaSize(&s_eaTeamCorrals); ++i)
	{
		if (s_eaTeamCorrals[i] == pCorral)
		{
			StructDestroy(parse_TeamCorralInfo, pCorral);
			eaRemove(&s_eaTeamCorrals, i);
			return;
		}
	}

	StructDestroy(parse_TeamCorralInfo, pCorral);
}

static bool gslTeam_CheckTeamCorralMemberReady(Entity *pEnt, TeamCorralMember *pMember, TeamCorralInfo *pCorral)
{
	F32 fDist = FLT_MAX;
	Vec3 v3EntPos;
	bool bOldReady = false;
	F32 fRadiusSquared = gslTeam_GetTeamCorralCircleRadiusSquared();

	if (!pEnt || !pMember || !pCorral)
	{
		return false;
	}

	bOldReady = pMember->bIsReady;
	entGetPos(pEnt, v3EntPos);
	fDist = distance3Squared(v3EntPos, pCorral->v3CenterPoint);

	if (fDist <= fRadiusSquared)
	{
		pMember->bIsReady = true;
	}
	else
	{
		pMember->bIsReady = false;
	}

	if (bOldReady != pMember->bIsReady)
		ClientCmd_team_SetEntInCircle(pEnt, pMember->bIsReady);

	return pMember->bIsReady;
}

//This function checks the players in a team corral against the members of the team and adds and removes members as necessary
static void gslTeam_UpdateAllMemberData(TeamCorralInfo *pCorral, S32 iTimer, bool bAllReady)
{
	S32 i;

	for (i = 0; i < eaSize(&pCorral->eaMembers); ++i)
	{
		TeamCorralMember *pMember =  pCorral->eaMembers[i];
		Entity *pEnt = entFromContainerID(pCorral->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);

		//If pEnt is not NULL, then the player should be on the current map
		if (pEnt)
		{
			ClientCmd_teamUpdateTeamCorralData(pEnt, iTimer, bAllReady);
		}
		else //Otherwise we should check elsewhere
		{
			pEnt = gslTeam_FindEntityForID(pCorral, pMember->iEntID);

			if (pEnt)
			{
				ClientCmd_teamUpdateTeamCorralData(pEnt, iTimer, bAllReady);
			}
		}
	}
}

static bool gslTeam_UpdateMemberList(TeamCorralInfo *pCorral)
{
	Entity **eaTeamEntities = NULL;
	TeamMember **eaTeamMembers = NULL;
	TeamCorralMember **eaTeamCorralMembers = pCorral->eaMembers;
	S32 iNumTeamMembers = 0;
	S32 iNumCorralMembers = eaSize(&eaTeamCorralMembers);
	Entity *pEnt = entFromContainerID(pCorral->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pCorral->uiPrimaryPlayerID);
	S32 i, j;

	if (pEnt && pCorral)
	{
		team_GetTeamListSelfFirst(pEnt, &eaTeamEntities, &eaTeamMembers, true, false);

		iNumTeamMembers = eaSize(&eaTeamEntities);

		if (iNumTeamMembers == 1 && !pCorral->bAllowSingleMemberCorral)
		{
			return false;
		}

		for (i = 0; i < iNumTeamMembers; ++i)
		{
			Entity *pCurEnt = eaTeamEntities[i];
			bool bFound = false;

			if (pCurEnt)
			{
				for (j = 0; j < iNumCorralMembers; ++j)
				{
					TeamCorralMember *pCorralMember = eaTeamCorralMembers[j];

					if (pCorralMember && pCorralMember->iEntID == entGetContainerID(pCurEnt))
					{
						bFound = true;
						if (i < eaSize(&eaTeamMembers) && eaTeamMembers[i])
						{
							pCorralMember->iMapContainerID = eaTeamMembers[i]->iMapContainerID;
						}
					}
				}

				if (!bFound)
				{
					gslTeam_AddEntityToTeamCorral(pCurEnt, eaTeamMembers[i], pCorral);
				}
			}
		}

		if (eaSize(&eaTeamCorralMembers) > iNumTeamMembers)
		{
			for (i = 0; i < eaSize(&eaTeamCorralMembers); ++i)
			{
				TeamCorralMember *pCorralMember = eaTeamCorralMembers[i];
				bool bFound = false;

				if (pCorralMember)
				{
					for (j = 0; j < iNumTeamMembers; ++j)
					{
						Entity *pCurEnt = eaTeamEntities[j];

						if (pCurEnt && entGetContainerID(pCurEnt) == pCorralMember->iEntID)
						{
							bFound = true;
						}
					}
				}

				if (!bFound)
				{
					gslTeam_RemoveMemberFromTeamCorral(pCorral, i--);
				}
			}
		}
	}
	else
	{
		return false;
	}

	return eaSize(&eaTeamCorralMembers) > 0;
}

void gslTeam_UpdateTeamCorral(TeamCorralInfo *pCorral)
{
	bool bAllReady = true;
	bool bNoneReady = true;
	S32 i;
	bool bShouldUpdateTime = false;
	static U32 s_uUpdateTime = 0;

	if(!gslTeam_UpdateMemberList(pCorral))
	{
		gslTeam_DestroyTeamCorral(pCorral);
		return;
	}

	for (i = 0; i < eaSize(&pCorral->eaMembers); ++i)
	{
		TeamCorralMember *pMember =  pCorral->eaMembers[i];
		Entity *pEnt = entFromContainerID(pCorral->iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pMember->iEntID);

		//If pEnt is not NULL, then the player should be on the current map
		if (pEnt)
		{
			bool bIsInCircle = gslTeam_CheckTeamCorralMemberReady(pEnt, pMember, pCorral);
			if (bIsInCircle)
			{
				bNoneReady = false;
			}
			else
			{
				bAllReady = false;
			}

			if (!pMember->bWindowShown)
			{
				ClientCmd_teamShowTeamCorralWindow(pEnt, gslTeam_GetTeamTransferTimeDefault(), false);
				gslTeam_ShowTeamCorralWindow(pEnt);
				ClientCmd_team_SetEntInCircle(pEnt, bIsInCircle);
				pMember->bWindowShown = true;
			}

			if (!pMember->bFXCircleStarted)
			{
				gslTeam_ShowPartyCircleOnClient(pEnt, pCorral->v3CenterPoint);
				pMember->bFXCircleStarted = true;
			}
		}
		else //Otherwise we should check elsewhere
		{
			pEnt = gslTeam_FindEntityForID(pCorral, pMember->iEntID);

			if (pEnt)
			{
				if (!pMember->bWindowShown)
				{
					RemoteCommand_gslTeam_ShowTeamCorralWindowOnOtherServer(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID, pCorral, entGetContainerID(pEnt));
					pMember->bWindowShown = true;
				}
				
				if (timeSecondsSince2000() >= s_uUpdateTime)
				{
					//s_uUpdateTime = timeSecondsSince2000() + MISSIONINFO_POKE_TIME;
					bShouldUpdateTime = true;

					RemoteCommand_gslTeam_UpdateTeamCorralStatus(GLOBALTYPE_ENTITYPLAYER, pMember->iEntID, entGetContainerID(pEnt));
				}

				bAllReady = false;
			}
			else //We should remove this team member
			{
				gslTeam_RemoveMemberFromTeamCorral(pCorral, i--);
			}
		}
	}

	if (bShouldUpdateTime)
	{
		s_uUpdateTime = timeSecondsSince2000() + MISSIONINFO_POKE_TIME;
	}

	if (bAllReady)
	{
		pCorral->bIsTeamReady = true;

		//When the countdown is finished, execute command
		if (pCorral->uiCountdownTimer <= timeSecondsSince2000())
		{
			pCorral->bIsCountdownComplete = true;
		}

		if (!pCorral->bOldAllReady)
		{
			gslTeam_UpdateAllMemberData(pCorral, gslTeam_GetTeamTransferTimeDefault(), bAllReady);
		}
	}
	else if (bNoneReady)
	{
		gslTeam_DestroyTeamCorral(pCorral);
		return;
	}
	else
	{
		pCorral->bIsTeamReady = false;
		pCorral->bIsCountdownComplete = false;
		pCorral->uiCountdownTimer = 0;

		if (pCorral->bOldAllReady)
		{
			gslTeam_UpdateAllMemberData(pCorral, pCorral->uiCountdownTimer - timeSecondsSince2000(), bAllReady);
		}
	}

	pCorral->bOldAllReady = bAllReady;
}

void gslTeam_UpdateAllTeamCorrals()
{
	S32 i;
	for (i = 0; i < eaSize(&s_eaTeamCorrals); ++i)
	{
		TeamCorralInfo *pInfo = s_eaTeamCorrals[i];

		if (pInfo)
		{
			gslTeam_UpdateTeamCorral(s_eaTeamCorrals[i]);
		}
	}
}

extern ParseTable parse_MissionInfo[];


void gslTeam_UpdateEnitityTeamCorralStatus(Entity *pPlayerEnt, MissionInfo *pInfo)
{
	//Periodically we want to ensure that if an entity thinks it is on a team corral, that it actually is
	if (pPlayerEnt && pInfo && timeSecondsSince2000() >= pInfo->uLastTeamCorralUpdateTime + MISSIONINFO_UPDATE_TIME)
	{
		TeamCorralInfo *pCorral = gslTeam_FindTeamCorralInfo(pPlayerEnt);
		bool bHasCorral = !(!pCorral);

		if (bHasCorral != pInfo->bHasTeamCorral)
		{
			if (bHasCorral)
			{
				pInfo->bHasTeamCorral = bHasCorral;
				pInfo->uLastTeamCorralUpdateTime = timeSecondsSince2000();
				waypoint_FlagWaypointRefresh(pInfo);
				entity_SetDirtyBit(pPlayerEnt, parse_MissionInfo, pInfo, false);
			}
			else if (timeSecondsSince2000() >= pInfo->uLastTeamCorralUpdateTime + ENTITY_TIMEOUT_TIME)
			{
				//If enough time has passed, we assume the team corral no longer exists and we tell the client to stop showing the UI
				pInfo->bHasTeamCorral = bHasCorral;
				pInfo->uLastTeamCorralUpdateTime = timeSecondsSince2000();
				waypoint_FlagWaypointRefresh(pInfo);
				entity_SetDirtyBit(pPlayerEnt, parse_MissionInfo, pInfo, false);
			}
		}

	}
}

void gslTeam_HideTeamCorralWindow(Entity *pEnt)
{
	if (pEnt)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

		if (pInfo)
		{
			pInfo->bHasTeamCorral = false;
			pInfo->uLastTeamCorralUpdateTime = timeSecondsSince2000();
			waypoint_FlagWaypointRefresh(pInfo);
			entity_SetDirtyBit(pEnt, parse_MissionInfo, pInfo, false);
		}

		ClientCmd_teamHideTeamCorralWindow(pEnt);
		ClientCmd_team_hidePartyCircle(pEnt);
	}
}

void gslTeam_ShowTeamCorralWindow(Entity *pEnt)
{
	if (pEnt)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEnt);

		if (pInfo)
		{
			pInfo->bHasTeamCorral = true;
			pInfo->uLastTeamCorralUpdateTime = timeSecondsSince2000();
			waypoint_FlagWaypointRefresh(pInfo);
			entity_SetDirtyBit(pEnt, parse_MissionInfo, pInfo, false);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslTeam_UpdateTeamCorralStatus(ContainerID entid)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entid);

	if(pEntity)
	{
		MissionInfo *pInfo = mission_GetInfoFromPlayer(pEntity);

		if (pInfo)
		{
			pInfo->bHasTeamCorral = true;
			pInfo->uLastTeamCorralUpdateTime = timeSecondsSince2000();
			entity_SetDirtyBit(pEntity, parse_MissionInfo, pInfo, false);
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslTeam_ShowTeamCorralWindowOnOtherServer(TeamCorralInfo *pCorral, ContainerID entid)
{
	Entity *pEntity = gslTeam_FindEntityForID(pCorral, entid);

	if(pEntity)
	{
		ClientCmd_teamShowTeamCorralWindow(pEntity, gslTeam_GetTeamTransferTimeDefault(), false);
		gslTeam_ShowTeamCorralWindow(pEntity);
	}
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
void gslTeam_HideTeamCorralWindowOnOtherServer(TeamCorralInfo *pCorral, ContainerID entid)
{
	Entity *pEntity = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entid);

	if(pEntity)
	{
		gslTeam_HideTeamCorralWindow(pEntity);
	}
}

#include "AutoGen/gslTeamCorral_h_ast.c"