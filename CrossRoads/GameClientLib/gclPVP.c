#include "gclPVP.h"
#include "Entity.h"
#include "pvp_common.h"
#include "Character_target.h"
#include "queue_common.h"
#include "../StaticWorld/ZoneMap.h"
#include "gclEntity.h"
#include "EntityIterator.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"
#include "StringCache.h"
#include "WorldLib.h"
#include "mapstate_common.h"
#include "gclMapState.h"
#include "GameStringFormat.h"
#include "NotifyCommon.h"
#include "PowersMovement.h"
#include "PVPScoreboardUI.h"

#define FX_FACTION_CHANGED_MESSAGE "FactionChange"


// ----------------------------------------------------------------------------------------------------------------------------
static void gclEntity_SendFXFactionChangedMessage(Entity *e)
{
	DynFxManager* pFXMan;

	if (entCheckFlag(e,ENTITYFLAG_IGNORE))
		return;

	pFXMan = dynFxManFromGuid(e->dyn.guidFxMan);
	if (pFXMan)
	{
		const char* msgName = allocAddString(FX_FACTION_CHANGED_MESSAGE);
		dynFxManBroadcastMessage(pFXMan, msgName);
	} 
}

// ----------------------------------------------------------------------------------------------------------------------------
static void gclPVP_SetFX(Entity *pEnt)
{
	Entity *eActivePlayer = entActivePlayerPtr();
	if (eActivePlayer)
	{
		EntityRelation relation = entity_GetRelationEx(PARTITION_CLIENT, eActivePlayer, pEnt, false);
		if(relation == kEntityRelation_Foe)
		{
			if (g_QueueConfig.pchEnemyFX)
			{
				dtFxManAddMaintainedFx(pEnt->dyn.guidFxMan, g_QueueConfig.pchEnemyFX, NULL, 0, 0, eDynFxSource_HardCoded);	
			}
		}
		else
		{
			if (g_QueueConfig.pchFriendlyFX)
			{
				dtFxManAddMaintainedFx(pEnt->dyn.guidFxMan, g_QueueConfig.pchFriendlyFX, NULL, 0, 0, eDynFxSource_HardCoded);	
			}
		}
	}
}

// ----------------------------------------------------------------------------------------------------------------------------
static void gclPVP_ResetFX(Entity *pEnt)
{
	if(g_QueueConfig.pchEnemyFX)
		dtFxManRemoveMaintainedFx(pEnt->dyn.guidFxMan, g_QueueConfig.pchEnemyFX);
	if(g_QueueConfig.pchFriendlyFX)
		dtFxManRemoveMaintainedFx(pEnt->dyn.guidFxMan, g_QueueConfig.pchFriendlyFX);
	gclPVP_SetFX(pEnt);
}

// ----------------------------------------------------------------------------------------------------------------------------
void gclPVP_EntityUpdate(Entity *pEnt)
{
	// if we are on a pvp match, see if we need a splat
	if (entIsPlayer(pEnt) && ZMTYPE_PVP == zmapInfoGetMapType(NULL))
	{
		if (TRUE_THEN_RESET(pEnt->factionDirtiedCount))
		{
			if (entIsLocalPlayer(pEnt))
			{	// local player faction updated
				// send the faction changed message to all other entities' FxManager
				EntityIterator*	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
				Entity*			pCurEnt;
				while(pCurEnt = EntityIteratorGetNext(iter))
				{
					gclPVP_ResetFX(pCurEnt);
				}
				EntityIteratorRelease(iter);

				worldNotifyPlayerFactionChanged( );
			}
			else
			{
				gclPVP_ResetFX(pEnt);
			}
		}
		else
		{
			gclPVP_SetFX(pEnt);
		}
	}	
	

}

////////////////////////////////////////////////////////////////////////////////////////////////

S32 gclPVP_GetWinningGroupID()
{
	PVPGroupGameParams ***pppGameParams = NULL;

	pppGameParams = mapState_GetScoreboardGroupDefs(mapStateClient_Get());

	if(pppGameParams && eaSize(pppGameParams) > 1)
	{
		return ((*pppGameParams)[0]->iScore > (*pppGameParams)[1]->iScore ? 0 : 1);
	}

	return 0;
}

//Right now this function just handles sending some of the notifications for PvP by checking the match state every frame
void gclPVP_Tick()
{
	if (zmapInfoGetMapType(NULL) == ZMTYPE_PVP)
	{
		MapState *pMapState = mapStateClient_Get();

		if(pMapState)
		{
			static ScoreboardState s_eOldState = kScoreboardState_Init;

			if (pMapState->matchState.eState != s_eOldState)
			{
				if (pMapState->matchState.eState == kScoreboardState_Active)
				{
					notify_NotifySend(entActivePlayerPtr(), kNotifyType_PvPStart, TranslateMessageKey("PVPGame_Begin"), "", NULL);
				}

				// Win/loss notfications
				if (pMapState->matchState.eState == kScoreboardState_Final)
				{
					if (gclPVP_GetWinningGroupID() == gclPVP_EntGetGroupID(entActivePlayerPtr()))
					{
						notify_NotifySend(entActivePlayerPtr(), kNotifyType_PvPWin, TranslateMessageKey("PVPGame_Win"), "", NULL);
					}
					else
					{
						notify_NotifySend(entActivePlayerPtr(), kNotifyType_PvPLoss, TranslateMessageKey("PVPGame_Loss"), "", NULL);
					}
				}

				s_eOldState = pMapState->matchState.eState;
			}

			//Handle countdown notifications
			if (pMapState->matchState.bCountdown && pMapState->matchState.eState == kScoreboardState_Init)
			{
				static U32 s_uOldCountdownTime = -1;
				static U32 uNewCountdownTime;

				uNewCountdownTime = ((pMapState->matchState.uCounterTime > pmTimestamp(0)) ? (pMapState->matchState.uCounterTime - pmTimestamp(0)) / 60 : 0);

				if (s_uOldCountdownTime != uNewCountdownTime)
				{
					static unsigned char *string = NULL;
					estrClear(&string);
					FormatGameString(&string, "{Value}",
						STRFMT_INT("Value", uNewCountdownTime), 
						STRFMT_END);
					notify_NotifySend(entActivePlayerPtr(), kNotifyType_PvPCountdown, string, "", NULL);
					s_uOldCountdownTime = uNewCountdownTime;
				}
			}
		}
	}
}