/***************************************************************************
 *     Copyright (c) Cryptic Studios
 *     All Rights Reserved
 *     Confidential Property of Cryptic Studios
 ***************************************************************************/

#include "EArray.h"
#include "Entity.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntitySavedData.h"
#include "FolderCache.h"
#include "GameServerLib.h"
#include "GameStringFormat.h"
#include "gslCommandParse.h"
#include "gslMapState.h"
#include "gslPartition.h"
#include "gslTimeControl.h"
#include "mapstate_common.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "PowerModes.h"
#include "team.h"
#include "wlTime.h"
#include "WorldGrid.h"

#include "gslTimeControl_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// ----------------------------------------------------------------------------------
// Data Definitions
// ----------------------------------------------------------------------------------

TimeControlConfig g_TimeControlConfig;

// Tells whether map type has been tested on this map yet and what the value is
static int s_iMapTypeAllowed = -1;

#if 0
#define DBG_PRINTF(x) printf x
#else
#define DBG_PRINTF(x)
#endif


// ----------------------------------------------------------------------------------
// Configuration Load Logic
// ----------------------------------------------------------------------------------

static void timecontrol_Validate(void)
{
	if (g_TimeControlConfig.fSlowTimeRate != 0) {
		ErrorFilenamef("defs/config/TimeControlConfig.def", "Time control only supports SlowTimeRate of zero");
	}
}


static void timecontrol_LoadConfig(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading %s...","TimeControl");

	// Fill-in the default values
	StructInit(parse_TimeControlConfig, &g_TimeControlConfig);

	ParserLoadFiles(NULL, "defs/config/TimeControlConfig.def", "TimeControlConfig.bin", PARSER_OPTIONALFLAG,
		parse_TimeControlConfig, &g_TimeControlConfig);

	loadend_printf(" done.");
}


AUTO_STARTUP(TimeControlConfig);
void timecontrol_Load(void)
{
	timecontrol_LoadConfig(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/TimeControlConfig.def", timecontrol_LoadConfig);
	timecontrol_Validate();
}


// ----------------------------------------------------------------------------------
// Tests to see if time control can be used
// ----------------------------------------------------------------------------------

static bool timecontrol_IsAllowedOnMap(void)
{
	ZoneMapType eType;
	int i;

	if (!gConf.bTimeControlAllowed || (s_iMapTypeAllowed == 0)) {
		return false;
	} else if (s_iMapTypeAllowed == 1) {
		return true;
	}

	eType = zmapInfoGetMapType(NULL);
	s_iMapTypeAllowed = 0;

	for(i=ea32Size(&g_TimeControlConfig.piMapTypesAllowed)-1; i>=0; i--) {
		if (g_TimeControlConfig.piMapTypesAllowed[i] == eType) {
 			s_iMapTypeAllowed = 1;
		}
	}

	return s_iMapTypeAllowed;
}


static bool timecontrol_IsAllowedOnPartition(int iPartitionIdx)
{
	if (g_TimeControlConfig.uMaxPlayersOnMapAllowed > 0
		&& ((U32)partition_GetPlayerCount(iPartitionIdx) > g_TimeControlConfig.uMaxPlayersOnMapAllowed)) {
		return false;
	}

	return true;
}



static bool timecontrol_IsAllowedInRegion(Entity *e)
{
	int i = ea32Size(&g_TimeControlConfig.piRegionTypesAllowed);

	// If no regions are specified, all are allowed
	if (i==0) {
		return true;
	}

	for(i=i-1; i>=0; i--) {
		if (entGetWorldRegionTypeOfEnt(e)==g_TimeControlConfig.piRegionTypesAllowed[i]) {
			return true;
		}
	}

	return false;
}


static bool TimeControlIsAllowedForPlayer(Entity *e, bool bReport)
{
	if (g_TimeControlConfig.bMustBeInCombat && !character_HasMode(e->pChar, kPowerMode_Combat)) {
		if (bReport) {
			char *estrTemp = NULL;
			estrStackCreate(&estrTemp);

			entFormatGameMessageKey(e, &estrTemp, "TimeControl_NotInCombat", STRFMT_END);
			notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);

			estrDestroy(&estrTemp);
		}

	} else if (g_TimeControlConfig.bMustBeTeamLeader && team_GetTeam(e) && !team_IsTeamLeader(e)) {
		if (bReport) {
			char *estrTemp = NULL;
			estrStackCreate(&estrTemp);

			entFormatGameMessageKey(e, &estrTemp, "TimeControl_NotLeader", STRFMT_END);
			notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);

			estrDestroy(&estrTemp);
		}

	} else if(!timecontrol_IsAllowedInRegion(e)) {
		if (bReport) {
			char *estrTemp = NULL;
			estrStackCreate(&estrTemp);

			entFormatGameMessageKey(e, &estrTemp, "TimeControl_NotInRegion", STRFMT_END);
			notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);

			estrDestroy(&estrTemp);
		}

	} else {
		return true;
	}

	return false;
}


// ----------------------------------------------------------------------------------
// Tick Function
// ----------------------------------------------------------------------------------

void timecontrol_OncePerFrame(void)
{
	bool bHasAnyPaused = false;
	int iPartitionIdx;

	PERFINFO_AUTO_START_FUNC();

	if (!gConf.bTimeControlAllowed || !timecontrol_IsAllowedOnMap()) {
		PERFINFO_AUTO_STOP();
		return;
	}

	// Update players, find out if we want to slow time
	for(iPartitionIdx = 0; iPartitionIdx < partition_GetCurNumPartitionsCeiling(); ++iPartitionIdx) {
		bool bPaused = false;
		char *estrTemp = NULL;
		Entity *pEnt;
		bool bControlValid;
		EntityIterator *pIter;
		MapState *pState;

		if (!partition_ExistsByIdx(iPartitionIdx)) {
			continue;
		}
		
		pState =  mapState_FromPartitionIdx(iPartitionIdx);
		if (pState->bBeingDestroyed) {
			continue;
		}

		bControlValid = timecontrol_IsAllowedOnPartition(iPartitionIdx);

		estrStackCreate(&estrTemp);

		pIter = entGetIteratorSingleType(iPartitionIdx, 0, ENTITYFLAG_IGNORE, GLOBALTYPE_ENTITYPLAYER);
		while(pEnt = EntityIteratorGetNext(pIter)) {
			if (pEnt && pEnt->pPlayer) {
				bool bDirty = false;
				bool bAllowed = bControlValid && TimeControlIsAllowedForPlayer(pEnt, false);
				bool bCurAllowed = pEnt->pPlayer->bTimeControlAllowed;

				if ((bAllowed && !bCurAllowed) || (!bAllowed && bCurAllowed)) {
					pEnt->pPlayer->bTimeControlAllowed = bAllowed;
					bDirty = true;
				}

				if (pEnt->pPlayer->bTimeControlAllowed && pState->fTimeControlTimer>0) {
					if (pEnt->pPlayer->bTimeControlPause) {
						if (bPaused) {
							estrConcatStatic(&estrTemp, ", ");
						}

						estrAppend2(&estrTemp, SAFE_MEMBER2(pEnt, pSaved, savedName));

						bPaused = true;
					}
				} else if (pEnt->pPlayer->bTimeControlPause) {
					char *estrTemp2 = NULL;
					estrStackCreate(&estrTemp2);
					entFormatGameMessageKey(pEnt, &estrTemp2, "TimeControl_Unpaused", STRFMT_END);
					notify_NotifySend(pEnt, kNotifyType_TimeControl, estrTemp2, NULL, NULL);
					estrDestroy(&estrTemp2);

					pEnt->pPlayer->bTimeControlPause = false;
					bDirty = true;
				}

				if (bDirty) {
					entity_SetDirtyBit(pEnt, parse_Player, pEnt->pPlayer, false);
				}
			}
		}
		EntityIteratorRelease(pIter);

		// Update MapState
		pState->bPaused = bPaused;

		if (bPaused) {
			pState->fTimeControlTimer -= gGSLState.secondsElapsed.reality.cur;
			bHasAnyPaused = true;
		} else {
			pState->fTimeControlTimer += g_TimeControlConfig.fRechargeRate * gGSLState.secondsElapsed.reality.cur;
			if (pState->fTimeControlTimer > g_TimeControlConfig.fMaxTime) {
				pState->fTimeControlTimer = g_TimeControlConfig.fMaxTime;
			}
		}

		// Apply pause to any entity in the partition that need it
		pIter = entGetIteratorAllTypes(iPartitionIdx, 0, ENTITYFLAG_DONOTSEND);
		while(pEnt = EntityIteratorGetNext(pIter)) {
			if (bPaused) {
				if (!pEnt->mm.mdhPaused) {
					mmDisabledHandleCreate(&pEnt->mm.mdhPaused, pEnt->mm.movement, __FILE__, __LINE__);
				}
			} else if (pEnt->mm.mdhPaused) {
				mmDisabledHandleDestroy(&pEnt->mm.mdhPaused);
			}
		}
		EntityIteratorRelease(pIter);

		if (pState->pchTimeControlList) {
			// Only update the string if we need to
			if (!estrTemp || stricmp(estrTemp, pState->pchTimeControlList)!=0) {
				StructFreeString(pState->pchTimeControlList);
				if (estrTemp) {
					pState->pchTimeControlList = StructAllocString(estrTemp);
					DBG_PRINTF(("Updating pause list to [%s].\n", pState->pchTimeControlList));
				}
			}
		} else if (estrTemp) {
			pState->pchTimeControlList = StructAllocString(estrTemp);
			DBG_PRINTF(("Updating pause list to [%s] (from empty).\n", pState->pchTimeControlList));
		}

		estrDestroy(&estrTemp);
	}

	mapState_SetHasAnyPausedPartition(bHasAnyPaused);

	PERFINFO_AUTO_STOP();
}


// ----------------------------------------------------------------------------------
// Commands
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME(pause) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void timecontrol_Pause(Entity *e)
{
	int iPartitionIdx = entGetPartitionIdx(e);

	if (timecontrol_IsAllowedOnMap() && timecontrol_IsAllowedOnPartition(iPartitionIdx)) {
		char *estrTemp = NULL;
		MapState *pState;
		estrStackCreate(&estrTemp);

		pState =  mapState_FromPartitionIdx(iPartitionIdx);

		if (pState->fTimeControlTimer > 0) {
			if (e && e->pPlayer && !e->pPlayer->bTimeControlPause) {
				if (TimeControlIsAllowedForPlayer(e, true)) {
					entFormatGameMessageKey(e, &estrTemp, "TimeControl_Paused", STRFMT_END);
					notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);

					e->pPlayer->bTimeControlPause = true;
					entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
				}
			}
		} else {
			entFormatGameMessageKey(e, &estrTemp, "TimeControl_OutOfTime", STRFMT_END);
			notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);
		}

		estrDestroy(&estrTemp);
	}
}


AUTO_COMMAND ACMD_NAME(unpause) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD;
void timecontrol_Unpause(Entity *e)
{
	int iPartitionIdx = entGetPartitionIdx(e);
	if (timecontrol_IsAllowedOnMap() && timecontrol_IsAllowedOnPartition(iPartitionIdx)) {
		if (e && e->pPlayer && e->pPlayer->bTimeControlPause) {
			char *estrTemp = NULL;
			estrStackCreate(&estrTemp);

			entFormatGameMessageKey(e, &estrTemp, "TimeControl_Unpaused", STRFMT_END);
			notify_NotifySend(e, kNotifyType_TimeControl, estrTemp, NULL, NULL);

			e->pPlayer->bTimeControlPause = false;
			entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);

			estrDestroy(&estrTemp);
		}
	}
}


AUTO_COMMAND ACMD_NAME(timecontrol_set) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void timecontrol_Control(Entity *e, bool bPause)
{
	if (bPause) {
		timecontrol_Pause(e);
	} else {
		timecontrol_Unpause(e);
	}
}


AUTO_COMMAND ACMD_NAME(timecontrol, timecontrol_toggle) ACMD_ACCESSLEVEL(0) ACMD_SERVERCMD ACMD_HIDE;
void timecontrol_Toggle(Entity *e)
{
	if (e && e->pPlayer) {
		if(e->pPlayer->bTimeControlPause) {
			timecontrol_Unpause(e);
		} else {
			timecontrol_Pause(e);
		}
	}
}


AUTO_EXPR_FUNC(util) ACMD_NAME(SafeTimeStepScale);
void exprSafeTimeStepScale(ExprContext *pContext, F32 fTimeStep)
{
	ZoneMapType eType = zmapInfoGetMapType(NULL);
	if ((eType == ZMTYPE_STATIC) || (eType == ZMTYPE_SHARED)) {
		Errorf("Cannot use SafeTimeStepScale on a STATIC or SHARED map");
		return;
	}
	if ((fTimeStep != 1.0) && (partition_GetActualActivePartitionCount() > 1)) {
		Errorf("Cannot use SafeTimeStepScale on a map with more than one partition");
		return;
	}
	timeStepScaleSetHandler(wlTimeGetStepScaleDebug(), fTimeStep);
}


#include "AutoGen/gslTimeControl_h_ast.c"
















































