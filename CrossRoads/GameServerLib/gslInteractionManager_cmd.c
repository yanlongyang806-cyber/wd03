/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslInteractionManager.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "InteractionManager_common.h"
#include "referencesystem.h"
#include "wlInteraction.h"
#include "WorldGrid.h"


AUTO_COMMAND ACMD_SERVERCMD;
void im_RespawnAll(Entity *pEnt)
{
	RefDictIterator iterator;
	WorldInteractionNode *pNode;
	static U32 iBitMaskDestruct;
	int iPartitionIdx = entGetPartitionIdx(pEnt);

	if (!iBitMaskDestruct) {
		iBitMaskDestruct = wlInteractionClassNameToBitMask("Destructible");
	}

	RefSystem_InitRefDictIterator(INTERACTION_DICTIONARY, &iterator);
	while (pNode = RefSystem_GetNextReferentFromIterator(&iterator)) {
		if (pNode && wlInteractionCheckClass(pNode,iBitMaskDestruct)) {
			Entity *pEntity = im_FindCritterforObject(iPartitionIdx, wlInteractionNodeGetKey(pNode));
			WorldInteractionEntry *pEntry = wlInteractionNodeGetEntry(pNode);
			GameInteractable *pInteractable = interactable_GetByEntry(pEntry);
			if (pEntity) {
				gslQueueEntityDestroy(pEntity);
			}

			im_RemoveNodeFromRespawnTimers(iPartitionIdx, pNode);
			im_RemoveNodeFromEntityTimers(iPartitionIdx, pNode);

			interactable_SetHideState(iPartitionIdx, pInteractable, I_STATE_SHOW, 0, false);

			if (pEntry->visible_child_count > 0) {
				interactable_SetVisibleChild(iPartitionIdx, pInteractable, 0, false);
			}

			interactable_SetDisabledState(iPartitionIdx, pInteractable, false);
		}
	}
}

