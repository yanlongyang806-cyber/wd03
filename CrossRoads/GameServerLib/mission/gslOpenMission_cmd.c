/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entity.h"
#include "gslMission.h"
#include "gslOpenMission.h"
#include "mission_common.h"


// ----------------------------------------------------------------------------------
// Debug Commands
// ----------------------------------------------------------------------------------

// openmissioncomplete <missionName>: Completes the named open mission
AUTO_COMMAND ACMD_NAME(OpenMissionComplete) ACMD_ACCESSLEVEL(7);
void openmission_CmdOpenMissionComplete(Entity *pPlayerEnt, ACMD_NAMELIST("AllMissionsIndex", RESOURCEDICTIONARY) const char *pcMissionName)
{
	if (pcMissionName && strstr(pcMissionName, "::")) {
		Mission *pMission = openmission_FindMissionFromRefString(entGetPartitionIdx(pPlayerEnt), pcMissionName);
		if (pMission) {
			mission_CompleteMission(pPlayerEnt, pMission, true );
		}
	} else {
		OpenMission *pOpenMission = openmission_GetFromName(entGetPartitionIdx(pPlayerEnt), pcMissionName );
		if (pOpenMission && pOpenMission->pMission) {
			mission_CompleteMission(pPlayerEnt, pOpenMission->pMission, true );
		}
	}
}


// openmissioncompletecurrent: Completes the player's current open mission
AUTO_COMMAND ACMD_NAME(OpenMissionCompleteCurrent) ACMD_ACCESSLEVEL(7);
void openmission_CmdOpenMissionCompleteCurrent(Entity *pPlayerEnt)
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		openmission_CmdOpenMissionComplete( pPlayerEnt, pInfo->pchCurrentOpenMission );
	}
}


// OpenMissionCompleteCurrentPhase: Completes the current phase of the open mission on the player
//  Current phase is defined as the first InProgress mission.
AUTO_COMMAND ACMD_NAME(OpenMissionCompleteCurrentPhase) ACMD_ACCESSLEVEL(7);
void openmission_CmdOpenMissionCompleteCurrentPhase(Entity *pPlayerEnt)
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		OpenMission *pOpenMission = openmission_GetFromName(entGetPartitionIdx(pPlayerEnt), pInfo->pchCurrentOpenMission);
		if (pOpenMission && pOpenMission->pMission) {
			mission_CompleteCurrentPhase(pPlayerEnt, pOpenMission->pMission);			
		}
	}
}


AUTO_COMMAND ACMD_NAME(OpenMissionTriggerCurrentEvent);
void openmission_CmdOpenMissionTriggerCurrentEvent(Entity *pPlayerEnt)
{
	MissionInfo* pInfo = mission_GetInfoFromPlayer(pPlayerEnt);
	if (pInfo) {
		OpenMission *pOpenMission = openmission_GetFromName(entGetPartitionIdx(pPlayerEnt), pInfo->pchCurrentOpenMission);
		if (pOpenMission && pOpenMission->pMission) {
			mission_TriggerCurrentEvent(pPlayerEnt, pOpenMission->pMission);
		}
	}
}


// MissionResetOpenMissions: Closes all running open missions and restarts any auto-start ones
AUTO_COMMAND ACMD_NAME(MissionResetOpenMissions);
void openmission_CmdMissionResetOpenMissions(Entity *pPlayerEnt)
{
	openmission_ResetOpenMissions(entGetPartitionIdx(pPlayerEnt));
}

