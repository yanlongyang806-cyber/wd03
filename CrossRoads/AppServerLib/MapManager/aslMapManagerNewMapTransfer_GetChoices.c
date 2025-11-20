#include "aslMapManagerNewMapTransfer_InnerStructs.h"
#include "textParser.h"
#include "AslMapManagerNewMapTransfer_Private.h"
#include "textparserUtils.h"
#include "textParserEnums.h"
#include "mapDescription.h"
#include "AslMapManager.h"
#include "stashTable.h"
#include "aslMapManagerNewMapTransfer_Private_h_ast.h"
#include "resourceInfo.h"
#include "logging.h"
#include "staticworld/worldGridPrivate.h"
#include "mapdescription_h_ast.h"
#include "aslMapManagerConfig_h_ast.h"
#include "stringCache.h"
#include "alerts.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"
#include "aslMapManagerNewMapTransfer_GetAddress.h"
#include "aslMapManagerNewMapTransfer_GetChoices.h"
#include "aslMapManagerNewMapTransfer.h"
#include "StringUtil.h"
#include "aslPatching.h"
#include "allegiance.h"
#include "continuousBuilderSupport.h"
#include "aslUGCDataManagerProject.h"
#include "UGCProjectUtils.h"

//if true, then prevent people from transferring to banned maps even if they have AL (basically for testing)
static bool sbDontAllowAccessLevelExemptionForUGCBanning = false;
AUTO_CMD_INT(sbDontAllowAccessLevelExemptionForUGCBanning, DontAllowAccessLevelExemptionForUGCBanning) ACMD_CATEGORY(Ugc);

void MapSearch_Log(MapSearchInfo *pSearchInfo, bool bLogEntireStruct, FORMAT_STR const char *pFmt, ...)
{
	char *pStructString = NULL;
	char *pFullInString = NULL;

	estrStackCreate(&pFullInString);
	estrGetVarArgs(&pFullInString, pFmt);

	if (bLogEntireStruct)
	{
		estrStackCreate(&pStructString);
		ParserWriteText(&pStructString, parse_MapSearchInfo, pSearchInfo, 0, 0, 0);
		log_printf(LOG_LOGIN, "%s MapSearch: %s. (Full info: %s)", GlobalTypeAndIDToString(pSearchInfo->playerType, pSearchInfo->playerID),
			pFullInString, pStructString);
		estrDestroy(&pStructString);
	}
	else
	{
		log_printf(LOG_LOGIN, "%s MapSearch: %s.", GlobalTypeAndIDToString(pSearchInfo->playerType, pSearchInfo->playerID),
			pFullInString);
	}

	estrDestroy(&pFullInString);
}

void MapSearch_LogWithStruct(MapSearchInfo *pSearchInfo, ParseTable *pTPI, void *pStruct, FORMAT_STR const char *pFmt, ...)
{
		char *pStructString = NULL;
	char *pFullInString = NULL;

	estrStackCreate(&pFullInString);
	estrGetVarArgs(&pFullInString, pFmt);

	estrStackCreate(&pStructString);
	ParserWriteText(&pStructString, pTPI, pStruct, 0, 0, 0);
	log_printf(LOG_LOGIN, "%s MapSearch: %s %s", GlobalTypeAndIDToString(pSearchInfo->playerType, pSearchInfo->playerID),
		pFullInString, pStructString);

	estrDestroy(&pFullInString);
	estrDestroy(&pStructString);
}


static void GetChoicesForMapSearchInfo_AllForDebugging(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason)
{
	//all existing partitions
	FOR_EACH_IN_STASHTABLE(sGameServerListsByMapDescription, GameServerList, pList)
	{
		if(!zmapInfoIsAvailable(GET_REF(pList->hZoneMapInfo), isDevelopmentMode()))
			continue;
		if (pList->eType == LISTTYPE_NORMAL)
		{
			int iServerNum;

			FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
				if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, pSearchInfo->_bBypassSoftLimits, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, pSearchInfo->_bBypassSoftLimits, NULL, NULL))
				{
					if (pSearchInfo->bAllVirtualShards || pSearchInfo->baseMapDescription.iVirtualShardID == pPartition->iVirtualShardID)
					{
						PossibleMapChoice *pChoice = StructCreate(parse_PossibleMapChoice);
						pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
						pChoice->baseMapDescription.containerID = pServer->iContainerID;
						pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
						pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
						pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
						pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
						pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
						pChoice->iNumPlayers = pPartition->iNumPlayers;
						pChoice->eChoiceType = MAPCHOICETYPE_SPECIFIED_ONLY;

						eaPush(&pChoices->ppChoices, pChoice);

					}
				}
			FOR_EACH_PARTITION_END

			for (iServerNum=0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
			{
				TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];
	
				if (NewMapTransfer_ServerHasRoomForNewPartition(pServer))
				{
					PossibleMapChoice *pChoice = StructCreate(parse_PossibleMapChoice);
					pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
					pChoice->baseMapDescription.containerID = pServer->iContainerID;
					pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
					pChoice->baseMapDescription.iVirtualShardID = pSearchInfo->baseMapDescription.iVirtualShardID;
					pChoice->baseMapDescription.mapVariables = allocAddString(pSearchInfo->baseMapDescription.mapVariables);
					pChoice->eChoiceType = MAPCHOICETYPE_NEW_PARTITION_ON_SPECIFIED_SERVER;

					if (pChoice->baseMapDescription.eMapType == ZMTYPE_MISSION || pChoice->baseMapDescription.eMapType == ZMTYPE_OWNED)
					{
                        if ( zmapInfoGetIsGuildOwned( GET_REF(pList->hZoneMapInfo) ) )
                        {
                            pChoice->baseMapDescription.ownerID = pSearchInfo->iGuildID;
                            pChoice->baseMapDescription.ownerType = GLOBALTYPE_GUILD;
                        }
                        else
                        {
						    pChoice->baseMapDescription.ownerID = pSearchInfo->playerID;
						    pChoice->baseMapDescription.ownerType = GLOBALTYPE_ENTITYPLAYER;
                        }
					}

					eaPush(&pChoices->ppChoices, pChoice);
				}
			}
		}
	}
	FOR_EACH_END



	//all potential new maps
	FOR_EACH_IN_EARRAY_FORWARDS(s_eaGameServerListByMapDescription, GameServerList, pList)
	{
		if(!zmapInfoIsAvailable(GET_REF(pList->hZoneMapInfo), isDevelopmentMode()))
			continue;
		if (pList->eType == LISTTYPE_NORMAL)
		{
			PossibleMapChoice *pChoice = StructCreate(parse_PossibleMapChoice);
			pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
			pChoice->baseMapDescription.iVirtualShardID = pSearchInfo->baseMapDescription.iVirtualShardID;
			pChoice->eChoiceType = MAPCHOICETYPE_FORCE_NEW;
			pChoice->bNewMap = true;
			pChoice->baseMapDescription.eMapType = GetListZoneMapType(pList);

			if (pChoice->baseMapDescription.eMapType == ZMTYPE_MISSION || pChoice->baseMapDescription.eMapType == ZMTYPE_OWNED)
			{
				pChoice->baseMapDescription.ownerID = pSearchInfo->playerID;
				pChoice->baseMapDescription.ownerType = GLOBALTYPE_ENTITYPLAYER;
			}

			eaPush(&pChoices->ppChoices, pChoice);
		}
	}
	FOR_EACH_END

	
	
}


//in this case, the one choice is all the same, and all the heavy lifting is done by GetAddress
static void GetChoicesForMapSearchInfo_OwnedMap(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason)
{
	if (pSearchInfo->bNoNewOwned)
	{
		GameServerList *pList;
		int iServerNum, iPartitionNum;

		if (!stashFindPointer(sGameServerListsByMapDescription, pSearchInfo->baseMapDescription.mapDescription, &pList))
		{
			ErrorOrAlert("UNKNOWN_OWNED_MAP", "Player %u requested transfer to unknown non-new owned map %s, not allowed", pSearchInfo->playerID, pSearchInfo->baseMapDescription.mapDescription);
			return;
		}
			
		for (iServerNum = 0; iServerNum < eaSize(&pList->ppGameServers); iServerNum++)
		{
			TrackedGameServerExe *pServer = pList->ppGameServers[iServerNum];

			if (pServer->bToldToDie || pServer->bLocked)
			{
				continue;
			}

			for (iPartitionNum = 0; iPartitionNum < eaSize(&pServer->description.ppPartitions); iPartitionNum++)
			{
				MapPartitionSummary *pPartition = pServer->description.ppPartitions[iPartitionNum];

				if (pPartition->eOwnerType == pSearchInfo->baseMapDescription.ownerType 
					&& pPartition->iOwnerID == pSearchInfo->baseMapDescription.ownerID 
					&& pPartition->iVirtualShardID == pSearchInfo->baseMapDescription.iVirtualShardID
					&& stricmp_safe(pPartition->pMapVariables, pSearchInfo->baseMapDescription.mapVariables) == 0)
				{
					static char *pNoLoginsReason = NULL;
					PossibleMapChoice *pChoice;

					if (!NewMapTransfer_GameServerIsAcceptingLogins(pServer, false, &pNoLoginsReason, NULL))
					{
						continue;
					}

					if (!NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, false, &pNoLoginsReason, NULL))
					{
						continue;
					}

					pChoice = StructCreate(parse_PossibleMapChoice);
					pChoice->baseMapDescription.mapDescription = allocAddString(pServer->pList->pMapDescription);
					pChoice->baseMapDescription.containerID = pServer->iContainerID;
					pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
					pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
					pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
					pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
					pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
					pChoice->iNumPlayers = pPartition->iNumPlayers;
					pChoice->eChoiceType = MAPCHOICETYPE_SPECIFIED_ONLY;

					eaPush(&pChoices->ppChoices, pChoice);

					return;
				}
			}
		}
	}
	else
	{
		PossibleMapChoice *pChoice = StructCreate(parse_PossibleMapChoice);
	
		pChoice->eChoiceType = MAPCHOICETYPE_NEW_OR_EXISTING_OWNED;
		pChoice->baseMapDescription.mapDescription = allocAddString(pSearchInfo->baseMapDescription.mapDescription);
		pChoice->baseMapDescription.eMapType = pSearchInfo->baseMapDescription.eMapType;
		pChoice->baseMapDescription.ownerID = pSearchInfo->baseMapDescription.ownerID;
		pChoice->baseMapDescription.ownerType = pSearchInfo->baseMapDescription.ownerType;
		pChoice->baseMapDescription.mapVariables = allocAddString(pSearchInfo->baseMapDescription.mapVariables);

		eaPush(&pChoices->ppChoices, pChoice);
	}
}


static void GetChoicesForMapSearchInfo_OneMapName(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason)
{
	GameServerList *pList;

	PossibleMapChoice *pChoice;

	NewMapTransfer_WhyNotAcceptingLogins eServerWhyNot, ePartitionWhyNot;
	bool bServerAccepting, bPartitionAccepting, bIsHardFull, bIsSoftFull;

	if (!stashFindPointer(sGameServerListsByMapDescription, pSearchInfo->baseMapDescription.mapDescription, &pList)
		|| pList->eType != LISTTYPE_NORMAL)
	{
		ErrorOrAlert("UNKNOWN_SEARCHINFO_MAP", "Player %u trying to get choices for unknown map %s. Search type is OneMapName, reason is %s", 
			pSearchInfo->playerID, pSearchInfo->baseMapDescription.mapDescription, pReason);
		return;
	}

	//new is always a possibility
	pChoice = StructCreate(parse_PossibleMapChoice);
	pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
	pChoice->baseMapDescription.iVirtualShardID = pSearchInfo->baseMapDescription.iVirtualShardID;
	pChoice->baseMapDescription.mapVariables = allocAddString(pSearchInfo->baseMapDescription.mapVariables);
	pChoice->baseMapDescription.eMapType = GetListZoneMapType(pList);
	pChoice->eChoiceType = MAPCHOICETYPE_FORCE_NEW;
	pChoice->bNewMap = true;
	eaPush(&pChoices->ppChoices, pChoice);

	//bestfit is also always a possibility
	pChoice = StructCreate(parse_PossibleMapChoice);
	pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
	pChoice->baseMapDescription.iVirtualShardID = pSearchInfo->baseMapDescription.iVirtualShardID;
	pChoice->baseMapDescription.mapVariables = allocAddString(pSearchInfo->baseMapDescription.mapVariables);
	pChoice->baseMapDescription.eMapType = GetListZoneMapType(pList);
	pChoice->eChoiceType = MAPCHOICETYPE_BEST_FIT;
	pChoice->bNewMap = true;
	eaPush(&pChoices->ppChoices, pChoice);


	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)

		eServerWhyNot = WHYNOTACCEPTINGLOGINS_ALLOWED;
		ePartitionWhyNot = WHYNOTACCEPTINGLOGINS_ALLOWED;
		bServerAccepting = NewMapTransfer_GameServerIsAcceptingLogins(pServer, pSearchInfo->_bBypassSoftLimits, NULL, &eServerWhyNot);
		bPartitionAccepting = bServerAccepting && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, pSearchInfo->_bBypassSoftLimits, NULL, &ePartitionWhyNot);
		bIsHardFull = (eServerWhyNot == WHYNOTACCEPTINGLOGINS_SERVER_HARD_FULL) || (ePartitionWhyNot == WHYNOTACCEPTINGLOGINS_PARTITION_HARD_FULL);
		bIsSoftFull = (eServerWhyNot == WHYNOTACCEPTINGLOGINS_SERVER_SOFT_FULL) || (ePartitionWhyNot == WHYNOTACCEPTINGLOGINS_PARTITION_SOFT_FULL);

		if ( (bServerAccepting && bPartitionAccepting) || ((bIsHardFull || bIsSoftFull) && pSearchInfo->bShowFullMapsAsDisabled) )
		{
			if ((pSearchInfo->bAllVirtualShards || pSearchInfo->baseMapDescription.iVirtualShardID == pPartition->iVirtualShardID)
				&& stricmp_safe(pSearchInfo->baseMapDescription.mapVariables, pPartition->pMapVariables) == 0)
			{
				pChoice = StructCreate(parse_PossibleMapChoice);
				pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
				pChoice->baseMapDescription.containerID = pServer->iContainerID;
				pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
				pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
				pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
				pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
				pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
				pChoice->iNumPlayers = pPartition->iNumPlayers;
				pChoice->eChoiceType = MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT;
				if( bIsHardFull )
				{
					pChoice->bIsHardFull = true;
					pChoice->bNotALegalChoice = true;
				}
				else if( bIsSoftFull )
				{
					pChoice->bIsSoftFull = true;
					pChoice->bNotALegalChoice = false; // This will be updated when we set iNumTeamMembersThere
				}

				eaPush(&pChoices->ppChoices, pChoice);
			}
		}
	FOR_EACH_PARTITION_END
}

static void GetChoicesForMapSearchInfo_SpecificIDs(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason, bool bOthersOK)
{
	MapPartitionSummary *pPartition;
	TrackedGameServerExe *pServer;

	if (!(pSearchInfo->baseMapDescription.containerID && pSearchInfo->baseMapDescription.iPartitionID))
	{
		if (!bOthersOK)
		{
			ErrorOrAlert("NO_ID_FOR_MAP_SEARCH", "Player %u trying to search fora specific ID map because %s, but it doesn't have both container ID and partition ID",
				pSearchInfo->playerID, pReason);
		}
		return;
	}

	if ((pPartition = GetPartitionFromIDs(pSearchInfo->baseMapDescription.containerID, pSearchInfo->baseMapDescription.iPartitionID, &pServer)))
	{
		if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, pSearchInfo->_bBypassSoftLimits, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, pSearchInfo->_bBypassSoftLimits, NULL, NULL))
		{
			PossibleMapChoice *pChoice = StructCreate(parse_PossibleMapChoice);
			pChoice->baseMapDescription.mapDescription = allocAddString(pServer->pList->pMapDescription);
			pChoice->baseMapDescription.containerID = pServer->iContainerID;
			pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
			pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
			pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
			pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
			pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
			pChoice->iNumPlayers = pPartition->iNumPlayers;
			pChoice->eChoiceType = bOthersOK ? MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT : MAPCHOICETYPE_SPECIFIED_ONLY;

			eaPush(&pChoices->ppChoices, pChoice);
		}
	}
}

static void GetChoicesForMapSearchInfo_SpecificIndex(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason, bool bOthersOK)
{
	GameServerList *pList;

	PossibleMapChoice *pChoice;

	if (!stashFindPointer(sGameServerListsByMapDescription, pSearchInfo->baseMapDescription.mapDescription, &pList)
		|| pList->eType != LISTTYPE_NORMAL)
	{
		ErrorOrAlert("UNKNOWN_SEARCHINFO_MAP", "Player %u trying to get choices for unknown map %s. Search type is SpecificIndex (othersOK %d), reason is %s", 
			pSearchInfo->playerID, pSearchInfo->baseMapDescription.mapDescription, bOthersOK, pReason);
		return;
	}

	if (!(pSearchInfo->baseMapDescription.mapInstanceIndex))
	{
		ErrorOrAlert("NO_INDEX_FOR_MAP_SEARCH", "Player %u trying to search fora specific index, but hasn't provided one",
			pSearchInfo->playerID);
		return;
	}

	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
		if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, pSearchInfo->_bBypassSoftLimits, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, pSearchInfo->_bBypassSoftLimits, NULL, NULL))
		{
			if (pSearchInfo->bAllVirtualShards || pSearchInfo->baseMapDescription.iVirtualShardID == pPartition->iVirtualShardID)
			{
				if (pPartition->iPublicIndex == pSearchInfo->baseMapDescription.mapInstanceIndex)
				{
					pChoice = StructCreate(parse_PossibleMapChoice);
					pChoice->baseMapDescription.mapDescription = allocAddString(pServer->pList->pMapDescription);
					pChoice->baseMapDescription.containerID = pServer->iContainerID;
					pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
					pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
					pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
					pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
					pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
					pChoice->iNumPlayers = pPartition->iNumPlayers;
					pChoice->eChoiceType = bOthersOK ? MAPCHOICETYPE_SPECIFIED_OR_BEST_FIT : MAPCHOICETYPE_SPECIFIED_ONLY;

					eaPush(&pChoices->ppChoices, pChoice);
				}
			}
		}
	FOR_EACH_PARTITION_END
}


static void GetChoicesForMapSearchInfo_AllOwnedMaps(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason)
{
	GameServerList *pList;

	PossibleMapChoice *pChoice;

	if (!stashFindPointer(sGameServerListsByMapDescription, pSearchInfo->baseMapDescription.mapDescription, &pList)
		|| pList->eType != LISTTYPE_NORMAL)
	{
		ErrorOrAlert("UNKNOWN_SEARCHINFO_MAP", "Player %u trying to get choices for unknown map %s. Search type is AllOwnedMaps, reason is %s", 
			pSearchInfo->playerID, pSearchInfo->baseMapDescription.mapDescription, pReason);
		return;
	}


	FOR_EACH_PARTITION_IN_LIST(pList, pPartition, pServer)
		if (NewMapTransfer_GameServerIsAcceptingLogins(pServer, pSearchInfo->_bBypassSoftLimits, NULL, NULL) && NewMapTransfer_PartitionIsAcceptingLogins(pServer, pPartition, pSearchInfo->_bBypassSoftLimits, NULL, NULL))
		{
			if ((pSearchInfo->bAllVirtualShards || pSearchInfo->baseMapDescription.iVirtualShardID == pPartition->iVirtualShardID)
				&& stricmp_safe(pSearchInfo->baseMapDescription.mapVariables, pPartition->pMapVariables) == 0
				&& pSearchInfo->baseMapDescription.ownerID == pPartition->iOwnerID
				&& pSearchInfo->baseMapDescription.ownerType == pPartition->eOwnerType)
			{
				pChoice = StructCreate(parse_PossibleMapChoice);
				pChoice->baseMapDescription.mapDescription = allocAddString(pList->pMapDescription);
				pChoice->baseMapDescription.containerID = pServer->iContainerID;
				pChoice->baseMapDescription.eMapType = GetListZoneMapType(pServer->pList);
				pChoice->baseMapDescription.iPartitionID = pPartition->uPartitionID;
				pChoice->baseMapDescription.mapInstanceIndex = pPartition->iPublicIndex;
				pChoice->baseMapDescription.iVirtualShardID = pPartition->iVirtualShardID;
				pChoice->baseMapDescription.mapVariables = allocAddString(pPartition->pMapVariables);
				pChoice->baseMapDescription.ownerID = pPartition->iOwnerID;
				pChoice->baseMapDescription.ownerType = pPartition->eOwnerType;

				pChoice->iNumPlayers = pPartition->iNumPlayers;
				pChoice->eChoiceType = MAPCHOICETYPE_SPECIFIED_ONLY;

				eaPush(&pChoices->ppChoices, pChoice);
			}
		}
	FOR_EACH_PARTITION_END
}


static const char *FindMapNameForNewPlayer(MapSearchInfo *pSearchInfo, bool bSkipTutorial)
{
	static bool sbAlreadyAlerted = false;
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pSearchInfo->pAllegiance);
	const char **ppMaps;

	if (bSkipTutorial)
	{
		ppMaps = pAllegiance && pAllegiance->ppSkipTutorialMaps ? pAllegiance->ppSkipTutorialMaps : gMapManagerConfig.ppSkipTutorialMaps;
	}
	else
	{
		ppMaps = pAllegiance && pAllegiance->ppNewCharacterMaps ? pAllegiance->ppNewCharacterMaps : gMapManagerConfig.ppNewCharacterMaps;
	}

	if (eaSize(&ppMaps) < 1)
	{
		if (!sbAlreadyAlerted)
		{
			ErrorOrAlert("NO_NEWPLAYER_MAPS", "For allegiance %s, we don't seem to have a newplayer map defined (skiptutorial: %d). New player map transfers will fail",
				pSearchInfo->pAllegiance, bSkipTutorial);
			sbAlreadyAlerted = true;	
		}

		return NULL;		
	}
		
	if (eaSize(&ppMaps) > 1 && !sbAlreadyAlerted)
	{
		sbAlreadyAlerted = true;
		ErrorOrAlert("MULTIPLE_NEWPLAYER_MAPS", "For allegiance %s, we seem to have %d skipTutorial maps. This is unsupported, just using %s",
			pSearchInfo->pAllegiance, eaSize(&ppMaps), ppMaps[0]);
	}

	return allocAddString(ppMaps[0]);
}

static const char *FindMapNameForFallbackMap(MapSearchInfo *pSearchInfo)
{
	static bool sbAlreadyAlerted = false;
	AllegianceDef *pAllegiance = RefSystem_ReferentFromString("Allegiance", pSearchInfo->pAllegiance);

	const char *pFallBackMap = NULL;

	if(pAllegiance && pAllegiance->pFallbackStaticMap)
	{
		pFallBackMap = pAllegiance->pFallbackStaticMap;
	}
	else
	{
		pFallBackMap = gMapManagerConfig.pFallbackStaticMap;
	}

	if(!pFallBackMap)
	{
		if (!sbAlreadyAlerted)
		{
			ErrorOrAlert("NO_FALLBACK_MAPS", "Player with broken map transfers will fail");
			sbAlreadyAlerted = true;	
		}

		return NULL;		
	}

	return allocAddString(pFallBackMap);
}

static void FixupMapSearchInfoForFallbackMapSearch(MapSearchInfo *pSearchInfo)
{
	char *pErrorString = NULL;
	char *pAlertKey;

	pSearchInfo->baseMapDescription.mapDescription = FindMapNameForFallbackMap(pSearchInfo);

	pSearchInfo->baseMapDescription.eMapType = MapCheckRequestedType(pSearchInfo->baseMapDescription.mapDescription, pSearchInfo->baseMapDescription.eMapType, 
		&pErrorString, &pAlertKey);

	pSearchInfo->eSearchType = MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES;

	if (pErrorString)
	{
		if (pSearchInfo->iAccessLevel == 0)
		{
			char *pMapSearchString = NULL;
			ParserWriteText(&pMapSearchString, parse_MapSearchInfo, pSearchInfo, 0, 0, 0);

			ErrorOrAlert(allocAddString(pAlertKey), "FixupMapSearchInfoForFallbackMapSearch got an error from MapCheckRequestedType. Error: %s. MapSearchInfo: %s",
				pErrorString, pMapSearchString);

			estrDestroy(&pMapSearchString);
		}

		estrDestroy(&pErrorString);
	}
}

static void FixupMapSearchInfoForNewPlayerSearch(MapSearchInfo *pSearchInfo, bool bSkipTutorial)
{
	char *pErrorString = NULL;
	char *pAlertKey;

	pSearchInfo->baseMapDescription.mapDescription = FindMapNameForNewPlayer(pSearchInfo, bSkipTutorial);

	pSearchInfo->baseMapDescription.eMapType = MapCheckRequestedType(pSearchInfo->baseMapDescription.mapDescription, pSearchInfo->baseMapDescription.eMapType, 
		&pErrorString, &pAlertKey);

	if (pErrorString)
	{
		if (pSearchInfo->iAccessLevel == 0)
		{
			char *pMapSearchString = NULL;
			ParserWriteText(&pMapSearchString, parse_MapSearchInfo, pSearchInfo, 0, 0, 0);
					
			ErrorOrAlert(allocAddString(pAlertKey), "FixupMapSearchInfoForNewPlayerSearch got an error from MapCheckRequestedType. Error: %s. MapSearchInfo: %s",
				pErrorString, pMapSearchString);

			estrDestroy(&pMapSearchString);
		}

		estrDestroy(&pErrorString);
	}

	if (pSearchInfo->baseMapDescription.eMapType == ZMTYPE_MISSION)
	{
		pSearchInfo->eSearchType = MAPSEARCHTYPE_OWNED_MAP;
		if (pSearchInfo->teamID)
		{
			pSearchInfo->baseMapDescription.ownerType = GLOBALTYPE_TEAM;
			pSearchInfo->baseMapDescription.ownerID = pSearchInfo->teamID;
		}
		else
		{
			pSearchInfo->baseMapDescription.ownerType = GLOBALTYPE_ENTITYPLAYER;
			pSearchInfo->baseMapDescription.ownerID = pSearchInfo->playerID;
		}
	}
	else
	{
		pSearchInfo->eSearchType = MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES;
	}
}

//given a string, returns true if it's an unplayable ugc namespace, false otherwise
static bool IsResourceUnplayableUGCNameSpace(const char *pResourceString)
{
	char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBase[RESOURCE_NAME_MAX_SIZE];
	resExtractNameSpace(pResourceString, pchNameSpace, pchBase);
	if(!(pchNameSpace[0]))
	{
		return false;
	}

	if (!namespaceIsUGC(pchNameSpace))
	{
		return false;
	}

	return !aslMapManager_IsNameSpacePlayableHelper(pchNameSpace);
}

static bool SearchInfoViolatesUGCBanning(MapSearchInfo *pSearchInfo)
{
	if (pSearchInfo->iAccessLevel > 0 && !sbDontAllowAccessLevelExemptionForUGCBanning)
	{
		return false;
	}

	//if we're not searching by name, then we're certainly not violating ugc banning
	if (!(pSearchInfo->baseMapDescription.mapDescription && pSearchInfo->baseMapDescription.mapDescription[0]))
	{
		return false;
	}

	return IsResourceUnplayableUGCNameSpace(pSearchInfo->baseMapDescription.mapDescription);
}

static void GetChoicesForMapSearchInfo(MapSearchInfo *pSearchInfo, PossibleMapChoices *pChoices, char *pReason)
{
	if (SearchInfoViolatesUGCBanning(pSearchInfo))
	{
		return;
	}

	pSearchInfo->_bBypassSoftLimits = (pSearchInfo->iAccessLevel > 0) || pSearchInfo->overSoftCapOK;

	//we convert some types of searches into the more common types before even beginning the search
	switch (pSearchInfo->eSearchType)
	{
	xcase MAPSEARCHTYPE_NEWPLAYER:
		FixupMapSearchInfoForNewPlayerSearch(pSearchInfo, false);

	xcase MAPSEARCHTYPE_NEWPLAYER_SKIPTUTORIAL:
		FixupMapSearchInfoForNewPlayerSearch(pSearchInfo, true);

	xcase MAPSEARCHTYPE_FALLBACK_MAP:
		FixupMapSearchInfoForFallbackMapSearch(pSearchInfo);
	}

	switch (pSearchInfo->eSearchType)
	{
	xcase MAPSEARCHTYPE_UNSPECIFIED:
		AssertOrAlertWithStruct("UNSPEC_SEARCH_TYPE", parse_MapSearchInfo, pSearchInfo, "A map search has unspecified type, not allowed: %s", pReason);
		return;
	xcase MAPSEARCHTYPE_ALL_FOR_DEBUGGING:
		GetChoicesForMapSearchInfo_AllForDebugging(pSearchInfo, pChoices, pReason);
		return;

	xcase MAPSEARCHTYPE_OWNED_MAP:
		GetChoicesForMapSearchInfo_OwnedMap(pSearchInfo, pChoices, pReason);
		return;

	xcase MAPSEARCHTYPE_ONE_MAPNAME_ALL_CHOICES:
		GetChoicesForMapSearchInfo_OneMapName(pSearchInfo, pChoices, pReason);
		return;

	xcase MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_ONLY:
		GetChoicesForMapSearchInfo_SpecificIDs(pSearchInfo, pChoices, pReason, false);
		return;

	xcase MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_ONLY:
		GetChoicesForMapSearchInfo_SpecificIndex(pSearchInfo, pChoices, pReason, false);
		return;

	xcase MAPSEARCHTYPE_SPECIFIC_CONTAINER_AND_PARTITION_ID_OR_OTHER:
		GetChoicesForMapSearchInfo_SpecificIDs(pSearchInfo, pChoices, pReason, true);
		if (!eaSize(&pChoices->ppChoices))
		{
			GetChoicesForMapSearchInfo_OneMapName(pSearchInfo, pChoices, pReason);
		}
		return;

	xcase MAPSEARCHTYPE_SPECIFIC_PUBLIC_INDEX_OR_OTHER:
		GetChoicesForMapSearchInfo_SpecificIndex(pSearchInfo, pChoices, pReason, true);
		if (!eaSize(&pChoices->ppChoices))
		{
			GetChoicesForMapSearchInfo_OneMapName(pSearchInfo, pChoices, pReason);
		}		
		return;

	xcase MAPSEARCHTYPE_ALL_OWNED_MAPS:
		GetChoicesForMapSearchInfo_AllOwnedMaps(pSearchInfo, pChoices, pReason);
		return;
	}
}

static void NewMapTransfer_FixupPossibleMapChoice(MapSearchInfo *pSearchInfo, PossibleMapChoice *pChoice)
{
	char ns[1024], base[1024]; 
	if(resExtractNameSpace(pChoice->baseMapDescription.mapDescription, ns, base)) 
	{ 
		pChoice->patchInfo = StructCreate(parse_DynamicPatchInfo); 
		aslFillInPatchInfo(pChoice->patchInfo, ns, PATCHINFO_FOR_UGC_PLAYING);
	}
	if (pSearchInfo)
	{
		// Set the spawnpoint specified in the search info
		pChoice->baseMapDescription.spawnPoint = allocAddString(pSearchInfo->baseMapDescription.spawnPoint);
		pChoice->baseMapDescription.bSpawnPosSkipBeaconCheck = pSearchInfo->baseMapDescription.bSpawnPosSkipBeaconCheck;
		copyVec3(pSearchInfo->baseMapDescription.spawnPos, pChoice->baseMapDescription.spawnPos);
		copyVec3(pSearchInfo->baseMapDescription.spawnPYR, pChoice->baseMapDescription.spawnPYR);
	}
}

void RemoveBannedMapsFromChoices(PossibleMapChoices *pRetVal)
{
	int i;

	for (i = eaSize(&pRetVal->ppChoices) - 1; i >= 0; i--)
	{
		PossibleMapChoice *pChoice = pRetVal->ppChoices[i];
		const char *pName = pChoice->baseMapDescription.mapDescription;

		if (pName && pName[0] && eaFindString(&gMapManagerConfig.ppBannedMaps, pName) != -1)
		{
			eaRemove(&pRetVal->ppChoices, i);
			StructDestroy(parse_PossibleMapChoice, pChoice);
		}
	}
}

PossibleMapChoices *NewMapTransfer_GetPossibleMapChoices(MapSearchInfo *pSearchInfo, MapSearchInfo *pBackupSearchInfo, char *pReason)
{
	PossibleMapChoices *pRetVal = StructCreate(parse_PossibleMapChoices);
	int i; 
	MapSearchType eActualSearchType = pSearchInfo->eSearchType;

	MapSearch_Log(pSearchInfo, 1, "NewMapTransfer_GetPossibleMapChoices called");

	if (pBackupSearchInfo)
	{
		MapSearch_Log(pBackupSearchInfo, 1, "Backup search info for NewMapTransfer_GetPossibleMapChoices");
	}

	GetChoicesForMapSearchInfo(pSearchInfo, pRetVal, pReason);

	RemoveBannedMapsFromChoices(pRetVal);

	if (pBackupSearchInfo && !eaSize(&pRetVal->ppChoices))
	{
		GetChoicesForMapSearchInfo(pBackupSearchInfo, pRetVal, pReason);
		RemoveBannedMapsFromChoices(pRetVal);	
	}

	

	//special case... continuous builder shouldn't fail if there is no starting map
	if (g_isContinuousBuilder 
		&& (eActualSearchType == MAPSEARCHTYPE_NEWPLAYER || eActualSearchType == MAPSEARCHTYPE_NEWPLAYER_SKIPTUTORIAL)
		&& eaSize(&pRetVal->ppChoices) == 0)
	{
		pSearchInfo->eSearchType = MAPSEARCHTYPE_ALL_FOR_DEBUGGING;
		GetChoicesForMapSearchInfo(pSearchInfo, pRetVal, pReason);
	}

	for (i=0; i < eaSize(&pRetVal->ppChoices); i++)
	{
		NewMapTransfer_FixupPossibleMapChoice(pSearchInfo, pRetVal->ppChoices[i]);
	}

	MapSearch_LogWithStruct(pSearchInfo, parse_PossibleMapChoices, pRetVal, "Returning from NewMapTransfer_GetPossibleMapChoices. Choices are:");

	return pRetVal;
}
