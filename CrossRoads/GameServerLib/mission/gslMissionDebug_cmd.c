/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "gslMission.h"
#include "gslMissionDebug.h"
#include "mission_common.h"
#include "Player.h"


// ----------------------------------------------------------------------------------
// Debugging commands
// ----------------------------------------------------------------------------------

// missiondebug <0/1>: Turns on mission debugging information
AUTO_COMMAND ACMD_NAME(MissionDebug);
void missiondebug_CmdMissionDebug(Entity *pPlayerEnt, int iSet)
{
	MissionInfo *pInfo;
	if (pPlayerEnt && (pInfo = mission_GetInfoFromPlayer(pPlayerEnt))) {
		pInfo->showDebugInfo = iSet;
		missiondebug_UpdateAllDebugInfo(pInfo->missions);
	}
}


// Populate the full mission list in mmm
AUTO_COMMAND ACMD_NAME("missionDebugMenuEnable");
void missiondebug_CmdMissionDebugMenuEnable(	Entity * e, int bEnabled)
{
	PlayerDebug *pDebug = entGetPlayerDebug(e, bEnabled);
	if (pDebug) {
		pDebug->showMissionDebugMenu = !!bEnabled;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}


