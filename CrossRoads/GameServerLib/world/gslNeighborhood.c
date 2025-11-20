/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "Entity.h"
#include "Expression.h"
#include "GameStringFormat.h"
#include "gslChat.h"
#include "gslNeighborhood.h"
#include "gslSendToClient.h"
#include "gslVolume.h"
#include "NotifyCommon.h"
#include "Player.h"
#include "ReferenceSystem.h"
#include "StashTable.h"
#include "StringCache.h"
#include "wlEncounter.h"
#include "WorldGrid.h"
#include "wlVolumes.h"

#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/SoundLib_autogen_ClientCmdWrappers.h"
#include "autogen/GameClientLib_AutoGen_ClientCmdWrappers.h"


// ----------------------------------------------------------------------------------
// Entity Neighborhood Logic
// ----------------------------------------------------------------------------------

static U32 neighborhood_volume_type;

AUTO_RUN;
void neighborhood_SetupVolumeTypeFlags( void )
{
    neighborhood_volume_type = wlVolumeTypeNameToBitMask("Neighborhood");
}

void neighborhood_SetEntityCurrentHood(Entity *pEnt, const char *pcPooledNeighborhoodName, const char *pcDisplayNameKey)
{
	if (!pEnt->currentNeighborhood) {
		pEnt->currentNeighborhood = StructCreate(parse_CurrentHood);
	}
	pEnt->currentNeighborhood->pchName = pcPooledNeighborhoodName;
	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	SET_HANDLE_FROM_STRING("Message", pcDisplayNameKey, pEnt->currentNeighborhood->hMessage);
}


void neighborhood_ClearEntityCurrentHood(Entity *pEnt, const char *pcNeighborhoodName)
{
	// Don't clear if neighborhood unless name matches
	if (!pEnt->currentNeighborhood || (stricmp(pEnt->currentNeighborhood->pchName, pcNeighborhoodName) != 0)) {
		return;
	}
	StructDestroy(parse_CurrentHood, pEnt->currentNeighborhood);
	pEnt->currentNeighborhood = NULL;
	entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
}


void neighborhood_ClearEntityHoodData(Entity *pEnt)
{
	// Destroy current neighborhood
	if (pEnt->currentNeighborhood) {
		StructDestroy(parse_CurrentHood, pEnt->currentNeighborhood);
		pEnt->currentNeighborhood = NULL;
		entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	}

	// Destroy hood music stash
	if (pEnt->pPlayer && pEnt->pPlayer->hoodMusicTimes) {
		stashTableDestroy(pEnt->pPlayer->hoodMusicTimes);
		pEnt->pPlayer->hoodMusicTimes = NULL;
		entity_SetDirtyBit(pEnt, parse_Entity, pEnt, false);
	}
}


void neighborhood_PlayHoodMusicForEntity(Entity *pEnt, const char *pcPooledNeighborhoodName, const char *pcSoundEffect)
{
	Player *pPlayer = pEnt->pPlayer;
	U32 uTime = timeSecondsSince2000();
	U32 uTimeHeard = 0;

	if (!pPlayer->hoodMusicTimes) {
		pPlayer->hoodMusicTimes = stashTableCreateWithStringKeys(5, StashDefault);
	}

	stashFindInt(pPlayer->hoodMusicTimes, pcPooledNeighborhoodName, &uTimeHeard);
	if (!uTimeHeard || (uTime - uTimeHeard > 300)) { // Time currently hardwired to 5 minutes
		char fullPath[512];
		strcpy(fullPath, "Music/Neighborhoods/");
		strcat(fullPath, pcSoundEffect);

		ClientCmd_sndPlayMusic(pEnt, fullPath, "Neighborhood", -1);
		stashAddInt(pPlayer->hoodMusicTimes, pcPooledNeighborhoodName, uTime, 1);
	}
}


// ----------------------------------------------------------------------------------
// Volume Callbacks
// ----------------------------------------------------------------------------------


void neighborhood_VolumeEnteredCB(WorldVolumeEntry *pEntry, Entity *pEnt, GameNamedVolume *pGameVolume)
{
	WorldNeighborhoodVolumeProperties *pHoodData = pEntry->server_volume.neighborhood_volume_properties;
	const char *pcPooledHoodName = allocAddString(volume_NameFromWorldEntry(pEntry));

	// Process the neighborhood
	if (pHoodData && pcPooledHoodName && (!pEnt->currentNeighborhood || pEnt->currentNeighborhood->pchName != pcPooledHoodName)) {
		Player *pPlayer = pEnt->pPlayer;

		if (pPlayer) {
			// Keep track of current neighborhood
			neighborhood_SetEntityCurrentHood(pEnt, pcPooledHoodName, REF_STRING_FROM_HANDLE(pHoodData->display_name_msg.hMessage));

			// Play neighborhood music, if any
			if (pHoodData->sound_effect) {
				neighborhood_PlayHoodMusicForEntity(pEnt, pcPooledHoodName, pHoodData->sound_effect);
			}

			ServerChat_PlayerUpdate(pEnt, CHATUSER_UPDATE_SHARD);

			// Always show floater for now
			ClientCmd_NotifySend(pEnt, kNotifyType_NeighborhoodEntered, entTranslateDisplayMessage(pEnt, pHoodData->display_name_msg), pcPooledHoodName, NULL);
		}

	} else if (!pcPooledHoodName) {
		Errorf("Player entered neighborhood with no name!  File for this neighborhood is not known.");
	}
}


void neighborhood_VolumeExitedCB(WorldVolumeEntry *pEntry, Entity *pEnt)
{
	WorldNeighborhoodVolumeProperties *pHoodData = pEntry->server_volume.neighborhood_volume_properties;

	// Process the neighborhood
	if (pHoodData && pEnt->pPlayer) {
		const char *pcName = volume_NameFromWorldEntry(pEntry);
		if (pcName) {
			neighborhood_ClearEntityCurrentHood(pEnt, pcName);
		}

		// look to see if we're in any OTHER hoods still
		if (pEnt->volumeCache)
		{
			const WorldVolume ** pVolumeArray = wlVolumeCacheGetCachedVolumes(pEnt->volumeCache);
			int i;
			for (i=0;i<eaSize(&pVolumeArray);i++)
			{
				const WorldVolume *pVolume = pVolumeArray[i];
				
				if (wlVolumeIsType(pVolume,neighborhood_volume_type))
				{
					WorldVolumeEntry * pNewEntry = wlVolumeGetVolumeData(pVolume);
					WorldNeighborhoodVolumeProperties *pNewHoodData = pNewEntry->server_volume.neighborhood_volume_properties;
					if (pNewHoodData)
					{
						const char *pcPooledHoodName = allocFindString(volume_NameFromWorldEntry(pNewEntry));
						neighborhood_SetEntityCurrentHood(pEnt, pcPooledHoodName, REF_STRING_FROM_HANDLE(pNewHoodData->display_name_msg.hMessage));
						break;
					}
				}
			}
		}
	}
}

void neighborhood_ValidateNeighborhoodVolume(GameNamedVolume *pGameVolume)
{
	WorldNeighborhoodVolumeProperties *pHoodData;
	
	if (!pGameVolume->pNamedVolume->entry) {
		return;
	}

	pHoodData = pGameVolume->pNamedVolume->entry->server_volume.neighborhood_volume_properties;

	// Ensure that the neighborhood has a display message
	if (!GET_REF(pHoodData->display_name_msg.hMessage)) {
		if (REF_STRING_FROM_HANDLE(pHoodData->display_name_msg.hMessage)) {
			ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
						   "Neighborhood '%s' has missing display name with key '%s'", pGameVolume->pcName, REF_STRING_FROM_HANDLE(pHoodData->display_name_msg.hMessage));
		} else {
			ErrorFilenamef(layerGetFilename(pGameVolume->pNamedVolume->common_data.layer), 
						   "Neighborhood '%s' has no display name defined.", pGameVolume->pcName);
		}
	}

	// TODO: Should we complain if two neighborhood volumes overlap?
	// Should not be a necessary failure case now, but volumes that overlap but aren't contained will still be weird [RMARR - 2/10/12]
}


