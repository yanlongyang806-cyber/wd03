/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "encounter_common.h"
#include "Entity.h"
#include "GlobalTypes.h"
#include "gslEncounter.h"
#include "gslOldEncounter.h"
#include "wlEncounter.h"
#include "wlGroupPropertyStructs.h"
#include "WorldLib.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

extern ContainerID s_uEditingClient;
extern GameEncounter **s_eaEncounters;

// ----------------------------------------------------------------------------------
// Simple Debug Commands
// ----------------------------------------------------------------------------------

AUTO_CMD_INT(g_EncounterProcessing, encounterprocessing) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_encounterDisableSleeping, encounterDisableSleeping) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_encounterIgnoreProximity, encounterIgnoreProximity) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_ForceTeamSize, forceteamsize);

AUTO_CMD_INT(g_AmbushCooldown, ambushCooldown) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_AmbushChance, ambushChance) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_AmbushSkipChance, ambushSkipChance) ACMD_ACCESSLEVEL(9);
AUTO_CMD_INT(g_AmbushDebugEnabled, ambushDebug) ACMD_ACCESSLEVEL(9);

void encounter_disableSleeping(int bDisable)
{
	g_encounterDisableSleeping = bDisable;
}

// ----------------------------------------------------------------------------------
// Encounter Debug Commands
// ----------------------------------------------------------------------------------

AUTO_COMMAND ACMD_NAME(EncounterForceSpawn) ACMD_ACCESSLEVEL(9);
void encounter_ForceSpawn(Entity *pClientEntity, char *pcEncounterName)
{
	GameEncounter *pEncounter = encounter_GetByName(pcEncounterName, NULL);
	if (pEncounter) {
		GameEncounterPartitionState *pState = encounter_GetPartitionState(entGetPartitionIdx(pClientEntity), pEncounter);
		if ((pState->eState == EncounterState_Asleep) || (pState->eState == EncounterState_Waiting)) {
			encounter_SpawnEncounter(pEncounter, pState);
		}
	} else if (gConf.bAllowOldEncounterData) {
		OldEncounter *pOldEncounter = oldencounter_FromStaticEncounterName(entGetPartitionIdx(pClientEntity), pcEncounterName);
		if (pOldEncounter && oldencounter_IsWaitingToSpawn(pOldEncounter)) {
			oldencounter_Spawn(oldencounter_GetDef(pOldEncounter), pOldEncounter);
		}
	}
}

// Used by a client in edit mode to request updated actor indices (for fillActorsInOrder encounters)
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void encounter_RequestActorIndexUpdates(Entity* pClientEnt)
{
	int iPartitionIdx;
	int i, j;

	if (!pClientEnt || (entGetAccessLevel(pClientEnt) < ACCESS_DEBUG)) {
		return;
	}

	iPartitionIdx = entGetPartitionIdx(pClientEnt);
	s_uEditingClient = entGetContainerID(pClientEnt);

	for(i = eaSize(&s_eaEncounters)-1; i>=0; i--) {
		GameEncounter *pEncounter = s_eaEncounters[i];
		if (pEncounter) {
			GameEncounterPartitionState *pState = encounter_GetPartitionState(iPartitionIdx, pEncounter);

			switch(pState->eState)
			{
				case EncounterState_Spawned:
				case EncounterState_Active:
				case EncounterState_Aware:
				case EncounterState_Success:
				case EncounterState_Failure:
					{
						WorldEncounterProperties *pProps = pEncounter->pWorldEncounter ? pEncounter->pWorldEncounter->properties : NULL;
						if (pProps && pProps->bFillActorsInOrder) {
							for(j = eaSize(&pProps->eaActors)-1; j >= 0; j--) {
								ClientCmd_encounter_UpdateActorIndex(pClientEnt, pEncounter->pcName, pProps->eaActors[j]->pcName, pProps->eaActors[j]->iActorIndex, pProps->eaActors[j]->bActorIndexSet);
							}
						}
						break;
					}
				default:
					break;
			}
		}
	}
}

// Used by a client in edit mode to request the EntityRef for an Encounter Actor
AUTO_COMMAND ACMD_SERVERCMD ACMD_ACCESSLEVEL(9);
void encounter_RequestEncounterActorEntityRef(Entity* pClientEnt, SA_PARAM_NN_VALID const char *pcEncounterName, SA_PARAM_NN_VALID const char *pcActorName)
{
	int iPartitionIdx;
	int i;

	if (!pClientEnt || (entGetAccessLevel(pClientEnt) < ACCESS_DEBUG))
		return;

	iPartitionIdx = entGetPartitionIdx(pClientEnt);

	for(i = eaSize(&s_eaEncounters) - 1; i >= 0; i--)
	{
		GameEncounter *pEncounter = s_eaEncounters[i];
		if(pEncounter && stricmp(pcEncounterName, pEncounter->pcName) == 0)
		{
			Entity *pEntity = encounter_GetActorEntity(iPartitionIdx, pEncounter, pcActorName);
			if(pEntity)
				ClientCmd_encounter_UpdateEncounterActorEntityRef(pClientEnt, pEncounter->pcName, pcActorName, pEntity->myRef);
		}
	}
}
