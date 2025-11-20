/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "EString.h"
#include "Expression.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslInteraction.h"
#include "Player.h"
#include "wlEncounter.h"
#include "WorldGrid.h"


// ----------------------------------------------------------------------------------
// Static Data
// ----------------------------------------------------------------------------------

// Debug flag.  Stop hiding uninteresting interactables
U32 g_InteractableDebug = 0;
U32 g_InteractableVisible = 0;
U32 g_InteractableVisibleOld = 0;

extern FoundDoorStruct **s_eaDebugDoorList;


// ----------------------------------------------------------------------------------
// Debug commands
// ----------------------------------------------------------------------------------

AUTO_CMD_INT(g_InteractableDebug, clickabledebug);
AUTO_CMD_INT(g_InteractableDebug, interactabledebug);

AUTO_COMMAND ACMD_ACCESSLEVEL(4);
void InteractableVisible(U32 val)
{
	if (isDevelopmentMode())
		g_InteractableVisible = val;
}

// Dumps a bunch of potentially useful info about a specific named interactable
AUTO_COMMAND ACMD_ACCESSLEVEL(4);
char* interactable_PrintInfoByName(Entity *pEnt, const char *pchInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pchInteractableName, NULL);
	static char *estrBuffer = NULL;
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	estrClear(&estrBuffer);

	if (pInteractable && pInteractable->pWorldInteractable && pInteractable->pWorldInteractable->entry){
		WorldInteractionNode *pNode = GET_REF(pInteractable->pWorldInteractable->entry->hInteractionNode);
		const InteractedObjectState *pState = interaction_GetInteractedObjectFromNode(iPartitionIdx, pNode);
		GameInteractablePartitionState *pPartitionState = interactable_GetPartitionState(iPartitionIdx, pInteractable);
		bool bCanInteract;

		estrConcatf(&estrBuffer, "\nPrinting info for '%s':\n", pchInteractableName);

		bCanInteract = interactable_CanEntityInteract(pEnt, pInteractable);

		if (bCanInteract){
			estrConcatf(&estrBuffer, "  You can currently interact with this object.\n");
		} else {
			estrConcatf(&estrBuffer, "  You cannot currently interact with this object.\n");
		}

		if (pPartitionState->bActive){
			estrConcatf(&estrBuffer, "  Active 1\n");
		}

		if (pPartitionState->bSleeping){
			estrConcatf(&estrBuffer, "  Sleeping 1\n");
		}

		if (pPartitionState->iPlayerOwnerID){
			estrConcatf(&estrBuffer, "  Owner %d\n", pPartitionState->iPlayerOwnerID);
		}

		if (pState){
			estrConcatf(&estrBuffer, "  InteractedObjectState: \n");
			estrConcatf(&estrBuffer, "    playerEntRef %d\n", pState->playerEntRef);

			switch(pState->state){
				xcase InteractedState_Active:
					estrConcatf(&estrBuffer, "    state Active\n");
				xcase InteractedState_Cooldown:
					estrConcatf(&estrBuffer, "    state Cooldown\n");
				xcase InteractedState_NoRespawn:
					estrConcatf(&estrBuffer, "    state NoRespawn\n");
				xdefault:
					estrConcatf(&estrBuffer, "    state unknown\n");
			}

			estrConcatf(&estrBuffer, "    bRespawn %d\n", pState->bRespawn);
			estrConcatf(&estrBuffer, "    fActiveTimeRemaining %f\n", pState->fActiveTimeRemaining);
			estrConcatf(&estrBuffer, "    fCooldownTimeRemaining %f\n", pState->fCooldownTimeRemaining);
			estrConcatf(&estrBuffer, "    fCooldownMultiplier %f\n", pState->fCooldownMultiplier);
		}

		return estrBuffer;
	}
	return "Interactable not found";
}


// Prints the debug log from a specific named interactable
AUTO_COMMAND ACMD_ACCESSLEVEL(4);
char* interactable_PrintDebugLogByName(const char *pchInteractableName)
{
	GameInteractable *pInteractable = interactable_GetByName(pchInteractableName, NULL);
	if (pInteractable){
		return NULL_TO_EMPTY(pInteractable->estrDebugLog);
	}
	return "Interactable not found"; 
}


AUTO_COMMAND;
void interactable_WarpToDoor(Entity *pEnt, char *pMap ACMD_NAMELIST("doors", NAMED))
{
	int i;
	
	if (!s_eaDebugDoorList) {
		interactable_FindAllDoors(&s_eaDebugDoorList,NULL, false);
	}
	
	for (i=0; i < eaSize(&s_eaDebugDoorList); i++) {
		if (stricmp(pMap, s_eaDebugDoorList[i]->pDoorName) == 0) {
			gslEntSetPos(pEnt, s_eaDebugDoorList[i]->vPos);
			return;
		}
	}
}


// Resets all Interactables on the map (like initencounters, but only resets interactables)
AUTO_COMMAND ACMD_ACCESSLEVEL(9);
void interactable_ResetAll(void)
{
	interactable_ResetInteractables();
}
