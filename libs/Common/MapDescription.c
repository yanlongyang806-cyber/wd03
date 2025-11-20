#include "MapDescription.h"
#include "WorldGrid.h"
#include "AutoGen/worldgrid_h_ast.h"
#include "AutoGen/MapDescription_h_ast.h"
#include "quat.h"
#include "StringCache.h"
#include "timing.h"
#include "Alerts.h"
#include "StringUtil.h"
#include "ugcProjectUtils.h"

// Keep these two in sync
STATIC_ASSERT(sizeof(MapDescription) == sizeof(SavedMapDescription));

bool IsSameMapDescription(MapDescription *map1, MapDescription *map2)
{
	PERFINFO_AUTO_START_FUNC();


	if (!map1 || !map2)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (map1->iVirtualShardID != map2->iVirtualShardID)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	//UGC maps are super simple... strcmp is necessary and sufficient
	if (map1->bUGC != map2->bUGC)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (map1->bUGC)
	{
		if (stricmp_safe(map1->mapDescription, map2->mapDescription) == 0)
		{
			PERFINFO_AUTO_STOP();
			return true;
		}
		else
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}



	if (map1->eMapType != map2->eMapType && map1->eMapType != ZMTYPE_UNSPECIFIED && map2->eMapType != ZMTYPE_UNSPECIFIED)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}
	// for owned maps, ownerID is a vetoing decision factor. For others (ie mission maps) more checking is needed
	if((map1->eMapType == ZMTYPE_OWNED || map2->eMapType == ZMTYPE_OWNED) && (map1->ownerType != map2->ownerType || map1->ownerID != map2->ownerID))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if(!!EMPTY_TO_NULL(map1->mapVariables) != !!EMPTY_TO_NULL(map2->mapVariables))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	// Variables need to match for a map to match
	if (map1->mapVariables && map2->mapVariables &&
		stricmp(map1->mapVariables,map2->mapVariables) != 0)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	switch (map1->eMapType)
	{
	case ZMTYPE_MISSION:
	case ZMTYPE_OWNED:
	case ZMTYPE_PVP:
	case ZMTYPE_QUEUED_PVE:

		// Check if they have the same ownership type, and if owned, are owned by the same player/team
		if (map1->ownerType != map2->ownerType)
		{
			PERFINFO_AUTO_STOP();	
			return(false);
		}
		if (map1->ownerType!=0 && map1->ownerID != map2->ownerID)
		{
			PERFINFO_AUTO_STOP();	
			return(false);
		}
		// Other map types are never owned
		//    ZMTYPE_STATIC 
		//    ZMTYPE_SHARED
	}

	
	if (!map1->pZoneMapInfo)
	{
		map1->pZoneMapInfo = worldGetZoneMapByPublicName(map1->mapDescription);
	}

	if(!map2->pZoneMapInfo)
	{
		map2->pZoneMapInfo = worldGetZoneMapByPublicName(map2->mapDescription);
	}

	if (map1->pZoneMapInfo && map1->pZoneMapInfo == map2->pZoneMapInfo)
	{
		PERFINFO_AUTO_STOP();
		return true;
	}

	if (map1->pZoneMapInfo && map2->pZoneMapInfo)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	//for ugc maps, just need to compare map descriptions, zoneMapInfos might not be available
	if (resNamespaceIsUGC(map1->mapDescription))
	{
		bool bMatch = (stricmp(map1->mapDescription, map2->mapDescription) == 0);
		PERFINFO_AUTO_STOP();
		return bMatch;
	}

	PERFINFO_AUTO_STOP();
	return false;
}




void CopySpawnInfo(MapDescription *pDest, MapDescription *pSrc)
{
	copyVec3(pSrc->spawnPos, pDest->spawnPos);
	copyVec3(pSrc->spawnPYR, pDest->spawnPYR);
	pDest->spawnPoint = allocAddString(pSrc->spawnPoint);
}




//
// Issue an error or alert if the passed in MapDescription contains
//  the wrong type for the map.
// Returns the actual map type.
//
// 
ZoneMapType MapCheckRequestedType(const char *pMapName, ZoneMapType eAssumedType, char **ppErrorString, char **ppOutAlertKey_NOT_AN_ESTRING)
{
	ZoneMapInfo *pZoneMap = worldGetZoneMapByPublicName(pMapName);

	
	ZoneMapType eActualType;
		
	//if we don't have a zonemap, this is a UGC or starcluster map, or something, and we just have to trust that
	//things are set up right

	if (!pZoneMap)
	{
		if (eAssumedType == ZMTYPE_UNSPECIFIED)
		{
			*ppOutAlertKey_NOT_AN_ESTRING = UNKNOWNMAPTYPE;
			estrPrintf(ppErrorString, "Map %s has UNSPECIFIED type. But we know nothing about that map, so can do nothing, defaulting to STATIC. This may be bad.",
				pMapName);

			if (resNamespaceIsUGC(pMapName))
				return ZMTYPE_MISSION;

			return ZMTYPE_STATIC;

		}

		return eAssumedType;
	}
		
	eActualType	= zmapInfoGetMapType(pZoneMap);

	if (eAssumedType != ZMTYPE_UNSPECIFIED && eAssumedType != eActualType)
	{
		*ppOutAlertKey_NOT_AN_ESTRING = "MAPTYPECORRUPTION";

		//NOTE NOTE NOTE NOTE do not change this string, the special mapTypeCorruption whitelist code depends on being
		//able to parse it from this identical format
		estrPrintf(ppErrorString, "Map %s thought to have type %s, someone requesting type %s",
			pMapName, StaticDefineIntRevLookup(ZoneMapTypeEnum, eActualType), StaticDefineIntRevLookup(ZoneMapTypeEnum, eAssumedType));
		//NOTE NOTE NOTE NOTE
	}

	return eActualType;
}

bool MapShouldOnlyBeReturnedToIfSameInstanceExists(MapDescription *pMap)
{
	if (pMap->eMapType == ZMTYPE_PVP || pMap->eMapType == ZMTYPE_QUEUED_PVE || pMap->eMapType == ZMTYPE_MISSION || pMap->eMapType == ZMTYPE_OWNED || pMap->eMapType == ZMTYPE_SHARED)
	{
		return true;
	}

	return false;
}

bool MapCanOnlyBeOnTopOfMapHistory(MapDescription *pMap)
{
	if (pMap->eMapType == ZMTYPE_PVP || pMap->eMapType == ZMTYPE_QUEUED_PVE)
	{
		return true;
	}

	return false;
}


bool DontModifyMapHistory(const Entity *pEnt)
{
	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		return true;
	}

	return false;
}

void MakeUgcMapName(char *pUUID, char *pAccountName, U32 iTime, char *buf, int buf_size)
{
	sprintf_s(SAFESTR2(buf), "%s_%s:%u", pAccountName, pUUID, iTime);
}

bool PossibleMapChoiceCanBeAutomaticallyChosen(PossibleMapChoice *pChoice)
{
	return true;
}

//
// Any saved map that is persisted on the character (map history) should have
//  the pitch and roll values set to zero to reduce database writes.
//
AUTO_TRANS_HELPER_SIMPLE;
void
SavedMapUpdateRotationForPersistence(NOCONST(SavedMapDescription) *pSavedMap)
{
	if ( pSavedMap )
	{
		pSavedMap->spawnPYR[0] = 0.f;
		pSavedMap->spawnPYR[2] = 0.f;
	}
}

//
// Copy the obsolete version of SavedMapDescription to the new version.
//  This is used to copy old stack map history to the new version.
//
NOCONST(SavedMapDescription) *
CopyOldSavedMapDescription(obsolete_SavedMapDescription *oldDesc)
{
	NOCONST(SavedMapDescription) *newDesc = StructCreateNoConst(parse_SavedMapDescription);
	ZoneMapInfo *zoneMap;

	zoneMap = zmapInfoGetByPublicName(oldDesc->mapDescription);
	
	if ( zoneMap != NULL )
	{
		// don't trust the old map history for the map type
		newDesc->eMapType = zmapInfoGetMapType(zoneMap);
	}
	else
	{
		newDesc->eMapType = oldDesc->eMapType;
	}
	newDesc->mapInstanceIndex = oldDesc->mapInstanceIndex;
	newDesc->ownerID = oldDesc->ownerID;
	newDesc->ownerType = oldDesc->ownerType;
	newDesc->containerID = oldDesc->containerID;

	// these four strings are all pooled, so it is ok to just copy pointers
	newDesc->mapDescription = (char *)oldDesc->mapDescription;
	newDesc->mapDetail = (char *)oldDesc->mapDetail;
	newDesc->mapVariables = (char *)oldDesc->mapVariables;
	newDesc->spawnPoint = (char *)oldDesc->spawnPoint;

	// copy spawn location
	copyVec3(oldDesc->spawnPos, newDesc->spawnPos);

	// convert rotation quaternion to PYR, with P and R forced to zero
	quatToPYR(oldDesc->spawnRot, newDesc->spawnPYR);
	SavedMapUpdateRotationForPersistence(newDesc);

	return newDesc;
}

bool MapCanBeClosedImmediatelyOnPlayerTransferOff(MapDescription* pDesc)
{
	if (pDesc->eMapType == ZMTYPE_STATIC || pDesc->eMapType == ZMTYPE_SHARED)
	{
		return false;
	}
	if (pDesc->eMapType == ZMTYPE_PVP || pDesc->eMapType == ZMTYPE_QUEUED_PVE)
	{
		return false;
	}
	return true;
}

PossibleMapChoice * ChooseBestMapChoice(PossibleMapChoice *** peaPossibleMapChoice)
{
	S32 iChosenIndex = -1;
	MapChoiceType eCurrentChoiceMapType = MAPCHOICETYPE_UNSPECIFIED;
	S32 iCurrentChoiceNumPlayers = -1;
	bool bChooseCurrentMap = false;

	if (peaPossibleMapChoice == NULL || eaSize(peaPossibleMapChoice) <= 0)
	{
		return NULL;
	}

	/*
	* Map choice types in order of priority
	*
	* Highest priority choices:
	*
	* MAPCHOICETYPE_BEST_FIT
	*
	* Normal priority choices:
	*
	* MAPCHOICETYPE_NEW_OR_EXISTING_OWNED
	* MAPCHOICETYPE_SPECIFIED_ONLY
	* MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT
	*
	* Lowest priority choices:
	*
	* MAPCHOICETYPE_FORCE_NEW
	* MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER
	*/

	FOR_EACH_IN_EARRAY((*peaPossibleMapChoice), PossibleMapChoice, pMapChoice)
	{
		// Make sure we're making a legal choice and it can be automatically chosen
		if(!pMapChoice->bNotALegalChoice && PossibleMapChoiceCanBeAutomaticallyChosen(pMapChoice))
		{
			bChooseCurrentMap = false;

			// If no choice made so far select this map regardless.
			if (iChosenIndex == -1)
			{
				bChooseCurrentMap = true;
			}
			// Do not ever try to choose MAPCHOICETYPE_FORCE_NEW or MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER.
			// The only way they get selected is if they are the only choice
			else if (pMapChoice->eChoiceType != MAPCHOICETYPE_FORCE_NEW && 
				pMapChoice->eChoiceType != MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER)
			{
				// Choose the current map because the current choice has the lowest priority map choice type
				if (eCurrentChoiceMapType == MAPCHOICETYPE_FORCE_NEW || eCurrentChoiceMapType == MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER)
				{
					bChooseCurrentMap = true;
				}
				// If the current map is a best fit choice, choose it unless the current choice is also best fit and has more players than this map.
				else if (pMapChoice->eChoiceType == MAPCHOICETYPE_BEST_FIT &&
					(eCurrentChoiceMapType != MAPCHOICETYPE_BEST_FIT || pMapChoice->iNumPlayers > iCurrentChoiceNumPlayers))
				{
					bChooseCurrentMap = true;
				}
				// At this point we know that the current choice and the current map has the same priority. Only update the choice if it has more players in it.
				else if (pMapChoice->iNumPlayers > iCurrentChoiceNumPlayers)
				{
					bChooseCurrentMap = true;
				}
			}
						
			if (bChooseCurrentMap)
			{
				iChosenIndex = FOR_EACH_IDX(g_pGameServerChoices->ppChoices, pMapChoice);
				eCurrentChoiceMapType = pMapChoice->eChoiceType;
				iCurrentChoiceNumPlayers = pMapChoice->iNumPlayers;
			}
		}
	}
	FOR_EACH_END

	if (iChosenIndex >= 0)
	{
		return (*peaPossibleMapChoice)[iChosenIndex];
	}
	
	return NULL;
}

bool MapDescription_MapTypeSupportsLockingWithoutKilling(ZoneMapType eType)
{
	switch (eType)
	{
	case ZMTYPE_STATIC:
	case ZMTYPE_MISSION:
	case ZMTYPE_SHARED:
		return true;
	}

	return false;

}
