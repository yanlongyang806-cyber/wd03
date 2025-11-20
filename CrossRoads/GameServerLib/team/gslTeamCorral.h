/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#pragma once

#include "Team.h"

typedef struct MissionInfo MissionInfo;

AUTO_STRUCT;
typedef struct TeamCorralMember
{
	ContainerID iEntID;
	bool		bIsReady;
	bool		bWindowShown;
	bool		bFXCircleStarted;

	//We store the map container id so that we can call remote commands
	ContainerID iMapContainerID;
} TeamCorralMember;

AUTO_STRUCT;
typedef struct TeamCorralInfo
{
	int iPartitionIdx;

	Vec3 v3CenterPoint;

	U32 uiCountdownTimer;

	//Team *pTeam;	NO_AST
	ContainerID uiTeamID;
	ContainerID uiPrimaryPlayerID;

	TeamCorralMember **eaMembers;

	bool bIsTeamReady;

	bool bOldAllReady;

	bool bIsCountdownComplete;

	bool bAllowSingleMemberCorral;
} TeamCorralInfo;

//Searches the game server's list of team corrals for one that matches the entity
TeamCorralInfo *gslTeam_FindTeamCorralInfo(Entity *pPlayerEnt);

//Searches the game server's list of team corrals for one that matches the team
TeamCorralInfo *gslTeam_FindTeamCorralInfoForTeam(Team *pTeam);

TeamCorralInfo *gslTeam_CreateTeamCorral(Entity *pPlayerEnt, bool bAllowSingleMember);
void gslTeam_DestroyTeamCorral(TeamCorralInfo *pCorral);

//Iterates through all the team corrals on the game server, updating each one's status
void gslTeam_UpdateAllTeamCorrals();

//This function does periodic checks of the passed in entity to update the team corral
//status that is stored on the mission info
void gslTeam_UpdateEnitityTeamCorralStatus(Entity *pPlayerEnt, MissionInfo *pInfo);

//Updates the entity's mission info to hide the team corral window on the player's client
void gslTeam_HideTeamCorralWindow(Entity *pEnt);

//Updates the entity's mission info to show the team corral window on the player's client
void gslTeam_ShowTeamCorralWindow(Entity *pEnt);

//#include "AutoGen/gslTeamCorral_h_ast.h"