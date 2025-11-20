/***************************************************************************
*     Copyright (c) 2005-2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "cmdparse.h"
#include "Entity.h"
#include "EntityLib.h"
#include "gslLogSettings.h"
#include "gslUserExperience.h"
#include "loggingEnums.h"
#include "mission_enums.h"
#include "Player.h"
#include "WorldGrid.h"



// Used to check if user experience logging should occur for this user
bool DEFAULT_LATELINK_UserExp_ShouldLogThisUser(Entity *pEnt)
{
	// Defaults to false.  Use the OVERRIDE_LATELINK function to set this per game
	return false;
}


// This is a utility function to lay out common formatting information for user experience logs
static void UserExp_BaseLogText(char **pestrLogString, Entity *pEnt)
{
	Vec3 vPos = { 0,0,0 };
	const char *pcMapName;

	entGetPos(pEnt, vPos);
	pcMapName = zmapInfoGetPublicName(NULL);

	estrPrintf(pestrLogString, "TimePlayed %d Map \"%s\" Pos \"%g,%g,%g\"", 
			((int)pEnt->pPlayer->fTotalPlayTime),
			pcMapName,
			vPos[0],
			vPos[1],
			vPos[2]
			);
}


// Used to record a player logging into the game
void UserExp_LogLogin(Entity *pEnt)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);

	entLog(LOG_USEREXPERIENCE, pEnt, "UserLogin", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used to report a player logging out of the game
void UserExp_LogLogout(Entity *pEnt)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);

	entLog(LOG_USEREXPERIENCE, pEnt, "UserLogout", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used to log user system information
void UserExp_LogSystemInfo(Entity *pEnt, const char *pcInfoString)
{
	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	entLog(LOG_USEREXPERIENCE, pEnt, "SystemSpecs", "%s", pcInfoString);
}


// Used to log arrival on a new map
void UserExp_LogMapTransferArrival(Entity *pEnt)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);

	entLog(LOG_USEREXPERIENCE, pEnt, "MapArrival", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used to log departure from a map
void UserExp_LogMapTransferDeparture(Entity *pEnt)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);

	entLog(LOG_USEREXPERIENCE, pEnt, "MapDeparture", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used to log a mission or submission being granted
// Normally only recorded for persisted missions.  Nonpersisted ones are spammy.
void UserExp_LogMissionGranted(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);
	estrConcatf(&estrLogString, " Mission \"%s\" SubMission \"%s\"", 
			pcMissionName, 
			pcSubMissionName ? pcSubMissionName : "BASE_MISSION");

	entLog(LOG_USEREXPERIENCE, pEnt, "MissionGranted", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used when a transaction needs to record a mission being granted
AUTO_COMMAND_REMOTE;
void UserExp_RemoteLogMissionGranted(const char *pcMissionName, const char *pcSubMissionName, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			UserExp_LogMissionGranted(pEnt, pcMissionName, pcSubMissionName);
		}
	}
}


// Used to log when a mission or submission is completed
void UserExp_LogMissionComplete(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName, int bDropped)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);
	estrConcatf(&estrLogString, " Mission \"%s\" SubMission \"%s\"", 
			pcMissionName, 
			pcSubMissionName ? pcSubMissionName : "BASE_MISSION");

	entLog(LOG_USEREXPERIENCE, pEnt, "MissionComplete", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used when a transaction needs to record when a mission or submission is completed
AUTO_COMMAND_REMOTE;
void UserExp_RemoteLogMissionComplete(const char *pcMissionName, const char *pcSubMissionName, int bDropped, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			UserExp_LogMissionComplete(pEnt, pcMissionName, pcSubMissionName, bDropped);
		}
	}
}


// Used to record when a mission or submission changes state, which may indicate completion or dropping
AUTO_COMMAND_REMOTE;
void UserExp_RemoteLogMissionState(const char *pcMissionName, const char *pcSubMissionName, int eState, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			if ((eState == MissionState_Succeeded) || (eState == MissionState_Failed)) {
				UserExp_LogMissionComplete(pEnt, pcMissionName, pcSubMissionName, false);
			} else if (eState == MissionState_Dropped) {
				UserExp_LogMissionComplete(pEnt, pcMissionName, pcSubMissionName, true);
			} else {
				Errorf("Unexpected RemoteLogMissionState state %d", eState);
			}
		}
	}
}


// Used to log a mission event being modified
void UserExp_LogMissionProgress(Entity *pEnt, const char *pcMissionName, const char *pcSubMissionName, const char *pcProgressStat, int iValue)
{
	char *estrLogString = NULL;

	if (!gbEnableUserExperienceLogging || !pEnt || !UserExp_ShouldLogThisUser(pEnt)) {
		return;
	}

	estrStackCreate(&estrLogString);
	UserExp_BaseLogText(&estrLogString, pEnt);
	estrConcatf(&estrLogString, " Mission \"%s\" SubMission \"%s\" Progress \"%s\" Value %d", 
			pcMissionName, 
			pcSubMissionName ? pcSubMissionName : "BASE_MISSION",
			pcProgressStat,
			iValue);

	entLog(LOG_USEREXPERIENCE, pEnt, "MissionProgress", "%s", estrLogString);

	estrDestroy(&estrLogString);
}


// Used when a mission event is modified from within a transaction
AUTO_COMMAND_REMOTE;
void UserExp_RemoteLogMissionProgress(const char *pcMissionName, const char *pcSubMissionName, const char *pcProgressStat, int iValue, CmdContext *pContext)
{
	if (pContext && pContext->clientType == GLOBALTYPE_ENTITYPLAYER)
	{
		Entity *pEnt = entFromContainerIDAnyPartition(pContext->clientType, pContext->clientID);
		if (pEnt) {
			UserExp_LogMissionProgress(pEnt, pcMissionName, pcSubMissionName, pcProgressStat,  iValue);
		}
	}
}


