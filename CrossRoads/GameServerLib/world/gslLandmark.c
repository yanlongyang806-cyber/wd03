/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "error.h"
#include "gslLandmark.h"
#include "gslMission.h"
#include "gslVolume.h"
#include "gslWaypoint.h"
#include "oldencounter_common.h"
#include "StringCache.h"
#include "wlVolumes.h"
#include "../StaticWorld/WorldCellEntry.h"


static LandmarkData **s_eaLandmarkData = NULL;


// ----------------------------------------------------------------------------------
// Landmark Tracking
// ----------------------------------------------------------------------------------


void landmark_AddLandmarkVolume(WorldVolumeEntry *pEntry)
{
	LandmarkData *pData;
	int i;

	// Check for duplicate add
	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		if (s_eaLandmarkData[i]->pEntry == pEntry) {
			Errorf("Attempted to add two copies of landmark volume.");
			return;
		}
	}

	// Add new entry
	pData = calloc(1, sizeof(LandmarkData));
	pData->pcIconName = allocAddString(pEntry->server_volume.landmark_volume_properties->icon_name);
	SET_HANDLE_FROM_REFERENT("Message", GET_REF(pEntry->server_volume.landmark_volume_properties->display_name_msg.hMessage), pData->hDisplayNameMsg);
	pData->pEntry = pEntry;
	pData->bHideUnlessRevealed = pEntry->server_volume.landmark_volume_properties->hide_unless_revealed;
	eaPush(&s_eaLandmarkData, pData);
	
	// Flag all players to refresh waypoints
	waypoint_FlagWaypointRefreshAllPlayers();
}


void landmark_RemoveLandmarkVolume(WorldVolumeEntry *pEntry)
{
	int i;

	// Remove the volume if present
	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		if (s_eaLandmarkData[i]->pEntry == pEntry) {
			REMOVE_HANDLE(s_eaLandmarkData[i]->hDisplayNameMsg);
			free(s_eaLandmarkData[i]);
			eaRemove(&s_eaLandmarkData, i);

			// Flag all players to refresh waypoints
			waypoint_FlagWaypointRefreshAllPlayers();

			return;
		}
	}
}

LandmarkData **landmark_GetLandmarkData(void)
{
	return s_eaLandmarkData;
}


static const char *landmark_GetSystemName(LandmarkData *pData)
{
	const char *pcName = volume_NameFromWorldEntry(pData->pEntry);
	return pcName ? pcName : "(NoNameFound)";
}


bool landmark_GetCenterPoint(LandmarkData *pData, Vec3 vCenterPos)
{
	if (!pData->bCenterPosInitialized && pData->pEntry) {
		int i;

		for(i=eaSize(&pData->pEntry->eaVolumes)-1; i>=0; --i) {
			if (pData->pEntry->eaVolumes[i]) {
				// Get the center of the volume
				pData->bCenterPosInitialized = true;
				wlVolumeGetVolumeWorldMid(pData->pEntry->eaVolumes[i], pData->vCenterPos);
			}
		}
	}

	copyVec3(pData->vCenterPos, vCenterPos);
	return pData->bCenterPosInitialized;
}


void landmark_MapValidate(void)
{
	int i;

	for(i=eaSize(&s_eaLandmarkData)-1; i>=0; --i) {
		LandmarkData *pData = s_eaLandmarkData[i];

		// Ensure that each landmark has a display message
		if (!GET_REF(pData->hDisplayNameMsg)) {
			if (REF_STRING_FROM_HANDLE(pData->hDisplayNameMsg)) {
				Errorf("Landmark '%s' has missing display name with key '%s'", landmark_GetSystemName(pData), REF_STRING_FROM_HANDLE(pData->hDisplayNameMsg));
			} else {
				//// TODO sdangelo: enable this once designers fix their data
				//Errorf("Landmark '%s' has no display name defined.", landmark_GetSystemName(pData));
			}
		}

		// Icon is optional
	}
}
