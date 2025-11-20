/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "entity.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslLayerFSM.h"
#include "gslMechanics.h"
#include "gslOldEncounter.h"
#include "gslPatrolRoute.h"
#include "gslSpawnPoint.h"
#include "gslTriggerCondition.h"
#include "gslVolume.h"
#include "MapDescription.h"
#include "oldencounter_common.h"
#include "wlBeacon.h"
#include "WorldGrid.h"

#include "AutoGen/oldencounter_common_h_ast.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

extern U32 g_ForceTeamSize;
extern int g_checkEncounterExternVars;
extern OldEncounterPartitionState **s_eaOldEncounterPartitionStates;

// ----------------------------------------------------------------------------------
// Debugging Commands
// ----------------------------------------------------------------------------------

AUTO_CMD_INT(g_checkEncounterExternVars, checkEncounterExternVars);


AUTO_COMMAND ACMD_SERVERCMD ACMD_NAME(EncounterLayerKill);
void oldencounter_CmdEncounterLayerKill(char* layerName)
{
	if (g_EncounterMasterLayer) {
		EncounterLayer* targetLayer = NULL;
		int i, n = eaSize(&g_EncounterMasterLayer->encLayers);
		for (i = 0; i < n; i++) {
			char currLayerName[1024];
			EncounterLayer* encLayer = g_EncounterMasterLayer->encLayers[i];
			getFileNameNoExt(currLayerName, encLayer->pchFilename);
			if (!stricmp(layerName, currLayerName)) {
				targetLayer = encLayer;
				break;
			}
		}

		// Now find all encounters in this layer and kill all their spawns
		if (targetLayer) {
			int iPartitionIdx;
			for(iPartitionIdx=eaSize(&s_eaOldEncounterPartitionStates)-1; iPartitionIdx>=0; --iPartitionIdx) {
				OldEncounterPartitionState *pState = s_eaOldEncounterPartitionStates[iPartitionIdx];
				if (pState) {
					int j, numEncs = eaSize(&pState->eaEncounters);
					for (j = 0; j < numEncs; j++) {
						OldEncounter* encounter = pState->eaEncounters[j];
						OldStaticEncounter* staticEnc = GET_REF(encounter->staticEnc);
						if (staticEnc && (staticEnc->layerParent == targetLayer)) {
							int k, numCritters = eaSize(&encounter->ents);
							for (k = 0; k < numCritters; k++) {
								entDie(encounter->ents[k], 0, 0, 0, NULL);
							}
						}
					}
				}
			}
		}
	}
}


AUTO_COMMAND ACMD_NAME(encounterIgnoreLayer);
void oldencounter_CmdIgnoreLayer(const char* layerName)
{
	oldencounter_IgnoreLayer(layerName);
	// Force InitEncounters on next tick
	g_EncounterReloadCounter = 1;
}


AUTO_COMMAND ACMD_NAME(encounterIgnoreAllLayersExcept);
void oldencounter_CmdIgnoreLayersExcept(const char* layerName)
{
	oldencounter_IgnoreAllLayersExcept(layerName);
	// Force InitEncounters on next tick
	g_EncounterReloadCounter = 1;
}


AUTO_COMMAND ACMD_NAME(encounterUnignoreAllLayers);
void oldencounter_CmdUnignoreLayers()
{
	oldencounter_UnignoreAllLayers();
	// Force InitEncounters on next tick
	g_EncounterReloadCounter = 1;
}
