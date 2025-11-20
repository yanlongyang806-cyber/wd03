#define GENESIS_ALLOW_OLD_HEADERS
#include "StringUtil.h"
#include "wlGenesisMissions.h"
#include "wlGenesis.h"
#include "wlGenesisMissions_h_ast.h"
#include "wlUGC.h"

#include "group.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping("wlGenesisMissionsGameStructs.h", BUDGET_World););

#ifndef NO_EDITORS

/// Return the procedural params for the specified mission/room.
GenesisProceduralObjectParams* genesisFindMissionRoomProceduralParams(GenesisMissionRequirements* missionReq, const char* layout_name, const char* room_name)
{
	GenesisMissionRoomRequirements* roomReq = NULL;

	if( !missionReq ) {
		return NULL;
	}

	{
		int it;
		for( it = 0; it != eaSize( &missionReq->roomRequirements ); ++it ) {
			GenesisMissionRoomRequirements* roomReqIt = missionReq->roomRequirements[ it ];
			if(   stricmp( roomReqIt->roomName, room_name ) == 0
				  && stricmp( roomReqIt->layoutName, layout_name ) == 0 ) {
				roomReq = roomReqIt;
				break;
			}
		}
	}
	if( !roomReq ) {
		return NULL;
	}

	return roomReq->params;
}

/// Return the instanced params for the specified mission/challenge.
GenesisInstancedObjectParams* genesisFindMissionChallengeInstanceParams(GenesisMissionRequirements* missionReq, const char* challenge_name)
{
	GenesisMissionChallengeRequirements* challengeReq = NULL;

	if( !missionReq ) {
		return NULL;
	}

	{
		int it;
		for( it = 0; it != eaSize( &missionReq->challengeRequirements ); ++it ) {
			if( stricmp( missionReq->challengeRequirements[ it ]->challengeName, challenge_name ) == 0 ) {
				challengeReq = missionReq->challengeRequirements[ it ];
				break;
			}
		}
	}
	if( !challengeReq ) {
		return NULL;
	}

	return challengeReq->params;
}

/// Return the interact params for the specified mission/challenge.
GenesisInteractObjectParams* genesisFindMissionChallengeInteractParams(GenesisMissionRequirements* missionReq, const char* challenge_name)
{
	GenesisMissionChallengeRequirements* challengeReq = NULL;

	if( !missionReq ) {
		return NULL;
	}

	{
		int it;
		for( it = 0; it != eaSize( &missionReq->challengeRequirements ); ++it ) {
			if( stricmp( missionReq->challengeRequirements[ it ]->challengeName, challenge_name ) == 0 ) {
				challengeReq = missionReq->challengeRequirements[ it ];
				break;
			}
		}
	}
	if( !challengeReq ) {
		return NULL;
	}

	return challengeReq->interactParams;
}


/// Return the volume params for the specified mission/challenge.
GenesisProceduralObjectParams* genesisFindMissionChallengeVolumeParams(GenesisMissionRequirements* missionReq, const char* challenge_name)
{
	GenesisMissionChallengeRequirements* challengeReq = NULL;

	if( !missionReq ) {
		return NULL;
	}

	{
		int it;
		for( it = 0; it != eaSize( &missionReq->challengeRequirements ); ++it ) {
			if( stricmp( missionReq->challengeRequirements[ it ]->challengeName, challenge_name ) == 0 ) {
				challengeReq = missionReq->challengeRequirements[ it ];
				break;
			}
		}
	}
	if( !challengeReq ) {
		return NULL;
	}

	return challengeReq->volumeParams;
}
void genesisCalcMissionVolumePointsInto(GenesisMissionVolumePoints ***peaVolumePointsList, GenesisMissionRequirements* pReq, GenesisToPlaceState *pToPlace)
{
	int extraVolumeIt;
	int objIt;
	int volObjIt;
	for( extraVolumeIt = 0; extraVolumeIt != eaSize( &pReq->extraVolumes ); ++extraVolumeIt ) {
		GenesisMissionExtraVolume* extraVolume = pReq->extraVolumes[ extraVolumeIt ];
		
		GenesisMissionVolumePoints* pVolPoints = StructCreate( parse_GenesisMissionVolumePoints );
		pVolPoints->volume_name = StructAllocString( extraVolume->volumeName );
		
		for( objIt = 0; objIt != eaSize( &pToPlace->objects ); ++objIt ) {
			GenesisToPlaceObject* object = pToPlace->objects[ objIt ];
			if( object->challenge_name ) {
				for( volObjIt = 0; volObjIt != eaSize( &extraVolume->objects ); ++volObjIt ) {
					if( eaFindString( &extraVolume->objects, object->challenge_name ) != -1 ) {
						Vec3 pos;
						float posRadius = 0;
						genesisObjectGetAbsolutePos( object, pos );

						if( object->params && object->params->volume_properties && object->params->volume_properties->eShape == GVS_Box) {
							Vec3 bounds[2];
							copyVec3(object->params->volume_properties->vBoxMin, bounds[0]);
							copyVec3(object->params->volume_properties->vBoxMax, bounds[1]);
							posRadius = MAX( posRadius, sqrtf( SQR( bounds[0][0]) + SQR( bounds[0][2] )));
							posRadius = MAX( posRadius, sqrtf( SQR( bounds[0][0]) + SQR( bounds[1][2] )));
							posRadius = MAX( posRadius, sqrtf( SQR( bounds[1][0]) + SQR( bounds[0][2] )));
							posRadius = MAX( posRadius, sqrtf( SQR( bounds[1][0]) + SQR( bounds[1][2] )));
						} else if( object->params && object->params->volume_properties && object->params->volume_properties->eShape == GVS_Sphere) {
							posRadius = object->params->volume_properties->fSphereRadius;
						}

						if( posRadius > 0 ) {
							Vec3 temp;
							setVec3( temp, pos[0] - posRadius, pos[1], pos[2] - posRadius );
							eafPush3( &pVolPoints->positions, temp );
							setVec3( temp, pos[0] - posRadius, pos[1], pos[2] + posRadius );
							eafPush3( &pVolPoints->positions, temp );
							setVec3( temp, pos[0] + posRadius, pos[1], pos[2] - posRadius );
							eafPush3( &pVolPoints->positions, temp );
							setVec3( temp, pos[0] + posRadius, pos[1], pos[2] + posRadius );
							eafPush3( &pVolPoints->positions, temp );
						} else {
							eafPush3( &pVolPoints->positions, pos );
						}
					}
				}
			}
		}

		eaPush( peaVolumePointsList, pVolPoints );
	}
}

const char* genesisMissionPortalSpawnTargetName_s(char* buffer, int buffer_size, GenesisMissionPortal* portal, bool isTargetStart, const char* mission_name, const char *layout_name)
{
	const char* layoutName;
	const char* roomName;
	const char* spawnName;
	
	if( isTargetStart ) {
		layoutName = portal->pcStartLayout;
		roomName = portal->pcStartRoom;
		spawnName = portal->pcStartSpawn;
	} else {
		if( portal->eType == GenesisMissionPortal_OneWayOutOfMap ) {
			layoutName = NULL;
			roomName = NULL;
			spawnName = portal->pcEndRoom;
		} else {
			if( portal->eType == GenesisMissionPortal_BetweenLayouts ) {
				layoutName = portal->pcEndLayout;
			} else {
				layoutName = portal->pcStartLayout;
			}
			roomName = portal->pcEndRoom;
			spawnName = portal->pcEndSpawn;
		}
	}

	if( spawnName ) {
		return spawnName;
	} else {
		if(portal->eUseType == GenesisMissionPortal_Door) {
			if(isTargetStart && portal->pcStartDoor && portal->pcStartDoor[0])
				sprintf_s(SAFESTR2(buffer), "DoorCap_%s_%s_%s_SPAWN", portal->pcStartDoor, roomName, layout_name);
			else if (!isTargetStart && portal->pcEndDoor && portal->pcEndDoor[0])
				sprintf_s(SAFESTR2(buffer), "DoorCap_%s_%s_%s_SPAWN", portal->pcEndDoor, roomName, layout_name);
			else
				sprintf_s(SAFESTR2(buffer), UGC_PREFIX_DOOR"%s_%s_%s_%s_%s", mission_name, portal->pcName, (isTargetStart ? "Start": "End"), roomName, layout_name );
		} else {
			sprintf_s(SAFESTR2(buffer), "%s_%s", layoutName, roomName );
		}
		return buffer;
	}
}

//sfenton TODO: Need to figure out how this is going to work when we have space and ground together
bool genesisMissionReturnIsAutogenerated(GenesisMissionZoneDescription* pMissionDesc, bool has_solar)
{
	return (pMissionDesc->startDescription.eExitFrom == GenesisMissionExitFrom_Anywhere || has_solar);
}

bool genesisMissionContinueIsAutogenerated(GenesisMissionZoneDescription* pMissionDesc, bool has_solar, bool has_exterior)
{
	if (!pMissionDesc->startDescription.bContinue) {
		return false;
	}

	if (pMissionDesc->startDescription.eContinueFrom == GenesisMissionExitFrom_Anywhere)
		return true;
	if (has_solar)
		return true;
	if (has_exterior)
		return (pMissionDesc->startDescription.eContinueFrom == GenesisMissionExitFrom_Entrance
				|| pMissionDesc->startDescription.eContinueFrom == GenesisMissionExitFrom_DoorInRoom);

	return false;
}

#endif

/// Find the challenge for the challenge named CHALLENGE-NAME.
GenesisMissionChallenge* genesisFindChallenge(GenesisMapDescription* map_desc, GenesisMissionDescription* mission_desc, const char* challenge_name, bool* outIsShared)
{
	int it;

	if( nullStr( challenge_name )) {
		SAFE_ASSIGN( outIsShared, false );
		return NULL;
	}

	if( mission_desc ) {
		for( it = 0; it != eaSize( &mission_desc->eaChallenges ); ++it ) {
			GenesisMissionChallenge* challenge = mission_desc->eaChallenges[ it ];
			if( stricmp( challenge->pcName, challenge_name ) == 0 ) {
				SAFE_ASSIGN( outIsShared, false );
				return challenge;
			}
		}
	}
	for( it = 0; it != eaSize( &map_desc->shared_challenges ); ++it ) {
		GenesisMissionChallenge* challenge = map_desc->shared_challenges[ it ];
		if( stricmp( challenge->pcName, challenge_name ) == 0 ) {
			SAFE_ASSIGN( outIsShared, true );
			return challenge;
		}
	}

	SAFE_ASSIGN( outIsShared, false );
	return NULL;
}

TextParserResult fixupGenesisProceduralEncounterProperties(GenesisProceduralEncounterProperties *pPep, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old challenge format to the new format
			{
				if( eaSize( &pPep->when_challenges ) != 0 && eaSize( &pPep->spawn_when.eaChallengeNames ) == 0 ) {
					int it;
					for( it = 0; it != eaSize( &pPep->when_challenges ); ++it ) {
						GenesisMissionZoneChallenge* challenge = pPep->when_challenges[ it ];
						eaPush( &pPep->spawn_when.eaChallengeNames, StructAllocString( challenge->pcName ));
					}
				}
			}

			// Fixup the old prompt format to the new format
			{
				if( eaSize( &pPep->when_prompts ) != 0 ) {
					int it;
					for( it = 0; it != eaSize( &pPep->when_prompts ); ++it ) {
						GenesisProceduralPromptInfo* prompt = pPep->when_prompts[ it ];
						eaPush( &pPep->spawn_when.eaPromptNames, StructAllocString( prompt->dialogName ));
					}

					eaDestroyStruct( &pPep->when_prompts, parse_GenesisProceduralPromptInfo );
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

/// Fixup function for GenesisMissonChallenge
TextParserResult fixupGenesisMissionChallenge(GenesisMissionChallenge *pChallenge, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old encounter format to the new format
			{
				GenesisMissionChallengeEncounter encounter = { 0 };
				if( memcmp( &encounter, &pChallenge->oldEncounter, sizeof( encounter )) != 0 ) {
					StructDestroySafe( parse_GenesisMissionChallengeEncounter, &pChallenge->pEncounter );
					pChallenge->pEncounter = StructClone( parse_GenesisMissionChallengeEncounter, &pChallenge->oldEncounter );
					StructDeInit( parse_GenesisMissionChallengeEncounter, &pChallenge->oldEncounter );
					memset( &pChallenge->oldEncounter, 0, sizeof( pChallenge->oldEncounter ));
				}
			}

			// Fixup tags into new format
			{
				if( pChallenge->pchOldChallengeTags ) {
					eaDestroyEx( &pChallenge->eaChallengeTags, StructFreeString );
					DivideString( pChallenge->pchOldChallengeTags, ",", &pChallenge->eaChallengeTags,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pChallenge->pchOldChallengeTags );
				}
			}

			// Fixup side trail names.
			{
				int i;
				bool uses_side_trail = false;
				bool has_side_trail_name = false;
				for (i = eaSize(&pChallenge->eaRoomNames)-1; i >= 0 ; i--) {
					char *room_name = pChallenge->eaRoomNames[i];
					if(utils_stricmp(room_name, GENESIS_SIDE_TRAIL_NAME)==0) {
						has_side_trail_name = true;
					} else if(strStartsWith(room_name, "AutoSideTrailRoom_")) {
						uses_side_trail = true;
						eaRemove(&pChallenge->eaRoomNames, i);
						StructFreeString(room_name);
					}
				}
				if(uses_side_trail && !has_side_trail_name)
					eaPush(&pChallenge->eaRoomNames, StructAllocString(GENESIS_SIDE_TRAIL_NAME));
			}

		}
	}
	
	return PARSERESULT_SUCCESS;
}

/// Fixup function for GenesisMissonChallenge
TextParserResult fixupGenesisMissionObjective(GenesisMissionObjective *pObjective, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup the old succeesWhen format to the new format
			{
				if( pObjective->succeedWhen.type == 0 ) {
					GenesisWhen* succeedWhen = &pObjective->succeedWhen;
					GenesisMissionObjectiveOld* succeedWhenOld = &pObjective->succeedWhenOld;
					succeedWhen->type = succeedWhenOld->eType;

					switch( succeedWhen->type ) {
						case GenesisWhen_ChallengeComplete:
							if( succeedWhenOld->pCompleteChallenge ) {
								eaCopyEx( &succeedWhenOld->pCompleteChallenge->pcChallengeNames,
										  &succeedWhen->eaChallengeNames,
										  strdupFunc, strFreeFunc );
								succeedWhen->iChallengeNumToComplete = succeedWhenOld->pCompleteChallenge->iCount;
							}
							
						xcase GenesisWhen_RoomEntry:
							if( succeedWhenOld->pReachLocation ) {
								GenesisWhenRoom* room = StructCreate( parse_GenesisWhenRoom );
								room->roomName = StructAllocString( succeedWhenOld->pReachLocation->pcRoomName );
								eaPush( &succeedWhen->eaRooms, room );
							}
							
						xcase GenesisWhen_CritterKill:
							succeedWhen->iCritterNumToComplete = 0;
							if( succeedWhenOld->pKillCritter ) {
								const char* critterName = REF_STRING_FROM_HANDLE( succeedWhenOld->pKillCritter->hCritter );
								if( critterName ) {
									eaPush( &succeedWhen->eaCritterDefNames, StructAllocString( critterName ));
									succeedWhen->iCritterNumToComplete += succeedWhenOld->pKillCritter->iCount;
								}
							}
							if( succeedWhenOld->pKillCritterGroup ) {
								const char* critterGroupName = REF_STRING_FROM_HANDLE( succeedWhenOld->pKillCritterGroup->hCritterGroup );
								if( critterGroupName ) {
									eaPush( &succeedWhen->eaCritterGroupNames, StructAllocString( critterGroupName ));
									succeedWhen->iCritterNumToComplete += succeedWhenOld->pKillCritterGroup->iCount;
								}
							}
							
						xcase GenesisWhen_ContactComplete:
							if( succeedWhenOld->pTalkToContact ) {
								eaPush( &succeedWhen->eaContactNames, StructAllocString( succeedWhenOld->pTalkToContact->pcContactName ));
							}
							
						xcase GenesisWhen_ItemCount:
							if( succeedWhenOld->pCollectItems ) {
								const char* itemName = REF_STRING_FROM_HANDLE( succeedWhenOld->pCollectItems->hItemDef );
								if( itemName ) {
									eaPush( &succeedWhen->eaItemDefNames, StructAllocString( itemName ));
								}
							}
							
						xcase GenesisWhen_PromptComplete:
							if( succeedWhenOld->pPromptComplete ) {
								eaPush( &succeedWhen->eaPromptNames, StructAllocString( succeedWhenOld->pPromptComplete->pcPromptName ));
							}
					}
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}


#include "wlGenesisMissions_h_ast.c"
#include "wlGenesisMissionsGameStructs_h_ast.c"
