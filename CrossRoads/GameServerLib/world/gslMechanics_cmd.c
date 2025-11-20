/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "gslEntity.h"
#include "gslMechanics.h"
#include "mechanics_common.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "WorldGrid.h"

#include "mechanics_common_h_ast.h"


extern MapSummaryList* s_pDoorDestStatusRequestData;
extern bool g_EnablePatrolValidation;


// ----------------------------------------------------------------------------------
// Debug commands
// ----------------------------------------------------------------------------------

AUTO_CMD_INT(g_EnableDynamicRespawn, DynamicRespawn);
AUTO_CMD_FLOAT(g_fDynamicRespawnScale, DynamicRespawnScale);



AUTO_COMMAND ACMD_NAME(bcnNoEncError) ACMD_ACCESSLEVEL(9);
void encounter_disableErrorChecking(int unused)
{
	g_EncounterNoErrorCheck = 1;
}


AUTO_COMMAND ACMD_NAME(InitEncounters) ACMD_ACCESSLEVEL(9);
void mechanics_InitEncounters(void)
{
	game_MapReInit();
}


AUTO_COMMAND ACMD_CMDLINE;
void EnablePatrolValidation(bool enable)
{
	g_EnablePatrolValidation = enable;
}


// ----------------------------------------------------------------------------------
// Commands run by the client
// ----------------------------------------------------------------------------------

// Leave the map immediately, if you have a map logout timer counting down or if you're on a PvP map
//AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_NAME(LeaveNow);
//void gslLogoutTimer_LeaveNow(Entity *pPlayerEnt);
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_SERVERONLY ACMD_HIDE;
void gslLogoutTimer_LeaveNow(Entity *pPlayerEnt)
{
	Player *pPlayer = pPlayerEnt->pPlayer;
	if (!pPlayer) {
		return;
	}

	// Is the player on a PvP map or Queued PvE?  They can always return from here
	if ((zmapInfoGetMapType(NULL) == ZMTYPE_PVP) || (zmapInfoGetMapType(NULL) == ZMTYPE_QUEUED_PVE)) {
		LeaveMap(pPlayerEnt);
	}

	// If the player isn't on a PvP map and doesn't have a logout timer, this command does nothing
	if (pPlayer->pLogoutTimer) {
		switch(pPlayer->pLogoutTimer->eType)
		{		
		case LogoutTimerType_NotOnInstanceTeam:
		case LogoutTimerType_MissionReturn:
		case LogoutTimerType_MapDoesNotMatchProgression:
			// These timers are allowed to exit early.  Advance the countdown to zero.
			pPlayer->pLogoutTimer->expirationTime = timeSecondsSince2000();
			pPlayer->pLogoutTimer->timeRemaining = 0;
			entity_SetDirtyBit(pPlayerEnt, parse_Player, pPlayerEnt->pPlayer, false);
		}
	}
}


AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(LogoffCancelServer) ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gslLogoffcmd_Cancel(Entity *pEnt)
{
	gslLogoff_Cancel(pEnt, kLogoffCancel_Requested);
}


// ----------------------------------------------------------------------------------
// Remote commands
// ----------------------------------------------------------------------------------

AUTO_COMMAND_REMOTE ACMD_IFDEF(APPSERVER);
void SendDoorDestinationStatusRequestsToServer(MapSummaryList* pData)
{
	if (s_pDoorDestStatusRequestData && 
		eaSize(&pData->eaList) == eaSize(&s_pDoorDestStatusRequestData->eaList) ) {
		int i;
		for (i = eaSize(&pData->eaList) - 1; i >= 0; i--) {
			MapSummary* pSummary = pData->eaList[i];
			MapSummary* pLocalSummary = s_pDoorDestStatusRequestData->eaList[i];

			if (pSummary->pchMapName == pLocalSummary->pchMapName &&
				pSummary->pchMapVars == pLocalSummary->pchMapVars) {
				StructCopy(parse_MapSummary, pSummary, pLocalSummary, 0, 0, TOK_USEROPTIONBIT_1);
			} else {
				// Arrays don't match, this should never happen
				break;
			}
		}
	}
}



