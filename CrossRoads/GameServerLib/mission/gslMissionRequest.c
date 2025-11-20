/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "gslMissionRequest.h"
#include "gslMissionSet.h"
#include "gslMission_Transact.h"
#include "InventoryCommon.h"
#include "ItemCommon.h"
#include "mission_common.h"
#include "Player.h"


// ----------------------------------------------------------------------------------
// Mission Request System
// ----------------------------------------------------------------------------------

bool missionrequest_IsRequestOpen(Entity *pEnt, MissionRequest *pRequest)
{
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	int i;

	if (pEnt && pRequest) {
		MissionDef *pRequestedDef = GET_REF(pRequest->hRequestedMission);

		// Must be in the "Open" state
		if (pRequest->eState != MissionRequestState_Open) {
			return false;
		}

		// Not an Open request if we already have the mission
		if (!pInfo || mission_GetMissionFromDef(pInfo, pRequestedDef)) {
			return false;
		}

		// Not an Open request if the InactiveTime is in the future
		if (pRequest->uInactiveTime >= timeSecondsSince2000()) {
			return false;
		}

		// Not an Open Request if the player has an item that will grant the mission
		if (pEnt && pEnt->pInventoryV2) {
			MissionRequestFindItemData data = {0};
			data.pcPooledMissionName = pRequestedDef->name;
			data.bFound = false;

			for (i = eaSize(&pEnt->pInventoryV2->ppInventoryBags)-1; i>=0; i--) {
				InventoryBag *pBag = pEnt->pInventoryV2->ppInventoryBags[i];
				BagIterator *pIter;
				bool bFound = false;

				if (!pBag) {
					continue;
				}
				
				pIter = invbag_IteratorFromBag(CONTAINER_NOCONST(InventoryBag, pBag));
				for(; !bagiterator_Stopped(pIter); bagiterator_Next(pIter)) {
					ItemDef *pItemDef = bagiterator_GetDef(pIter);
					MissionDef *pMissionDef = SAFE_GET_REF(pItemDef, hMission);

					if (!pItemDef || !pMissionDef) {
						continue;
					}
					if (pItemDef->eType == kItemType_MissionGrant && pMissionDef->name == pRequestedDef->name) {
						bFound = true;
						break;
					}
				}
				bagiterator_Destroy(pIter);

				if (bFound) {
					return false;
				}
			}
		}

		return true;
	}

	return false;
}


// Returns TRUE if a Mission Request exists for the given MissionDef, and the mission should be offered
bool missionrequest_MissionRequestIsOpen(Entity *pEnt, MissionDef *pDef)
{
	MissionInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, missionInfo);
	MissionRequest *pRequest = NULL;
	int i;

	// See if the player has a Mission Request for this Mission
	if (pInfo && pDef) {
		for (i = eaSize(&pInfo->eaMissionRequests)-1; i>=0; --i) {
			MissionDef *pRequestDef = GET_REF(pInfo->eaMissionRequests[i]->hRequestedMission);
			if (pRequestDef && pRequestDef == pDef) {
				pRequest = pInfo->eaMissionRequests[i];
				break;
			}
		}
	}

	if (pRequest) {
		return missionrequest_IsRequestOpen(pEnt, pRequest);
	}
	return false;
}


bool missionrequest_Update(int iPartitionIdx, MissionInfo *pInfo, MissionRequest *pRequest)
{
	Entity *pPlayerEnt = pInfo ? pInfo->parentEnt:NULL;

	if (pPlayerEnt && pInfo && pRequest && pRequest->eState == MissionRequestState_Open) {
		MissionDef *pDef = GET_REF(pRequest->hRequestedMission);
		if (pDef) {
			if (pDef->eRequestGrantType == MissionRequestGrantType_Direct){
				Mission *pMission = mission_GetMissionFromDef(pInfo, pDef);
				if (!pMission) {
					missioninfo_AddMission(iPartitionIdx, pInfo, pDef, NULL, NULL, NULL);
					return true;
				}
			}
		} else {
			if (GET_REF(pRequest->hRequestedMissionSet)){
				// Either this request hasn't been initialized yet, or a missiondef was deleted.
				// Try to pick a new mission from the mission set
				MissionDef *pNewDef = missionset_RandomAvailableMissionFromSet(pPlayerEnt, GET_REF(pRequest->hRequestedMissionSet));
				if (pNewDef) {
					missionrequest_SetRequestedMission(pPlayerEnt, pRequest->uID, pNewDef);
				} else {
					// Something is wrong.  Force complete the Mission Request so that the requester mission doesn't get stuck
					missionrequest_ForceComplete(pPlayerEnt, pRequest->uID);
				}
			} else {
				// Something is wrong.  Force complete the Mission Request so that the requester mission doesn't get stuck
				missionrequest_ForceComplete(pPlayerEnt, pRequest->uID);
			}
		}
	}
	return false;
}


bool missionrequest_HasRequestsRecursive(MissionInfo *pInfo, Mission *pMission, Mission *pRootMission)
{
	char refstring[MAX_MISSIONREF_LEN];
	int i;

	// We don't want to use the MissionDef's refstring here, because this needs to work even if the
	// MissionDef has been deleted 
	if (pRootMission != pMission) {
		sprintf(refstring, "%s::%s", pRootMission->missionNameOrig, pMission->missionNameOrig);
	} else {
		sprintf(refstring, "%s", pMission->missionNameOrig);
	}

	for (i = eaSize(&pInfo->eaMissionRequests)-1; i>=0; --i) {
		if (!stricmp(pInfo->eaMissionRequests[i]->pchRequesterRef, refstring)){
			return true;
		}
	}

	for (i = eaSize(&pMission->children)-1; i>=0; --i) {
		if (missionrequest_HasRequestsRecursive(pInfo, pMission->children[i], pRootMission)){
			return true;
		}
	}

	return false;
}

