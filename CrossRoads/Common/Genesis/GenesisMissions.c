#define GENESIS_ALLOW_OLD_HEADERS 1

#include "GenesisMissions.h"

#include "EString.h"
#include "Expression.h"
#include "GameEvent.h"
#include "NotifyCommon.h"
#include "ResourceInfo.h"
#include "ResourceSystem_Internal.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UGCProjectUtils.h"
#include "WorldGrid.h"
#include "contact_common.h"
#include "encounter_common.h"
#include "error.h"
#include "file.h"
#include "interaction_common.h"
#include "mission_common.h"
#include "mission_enums.h"
#include "tokenstore.h"
#include "wlGenesis.h"
#include "wlGenesisExterior.h"
#include "wlGenesisInterior.h"
#include "wlGenesisRoom.h"
#include "wlGenesisSolarSystem.h"
#include "wlGenesisMissions.h"
#include "wlUGC.h"

#include "Autogen/NotifyEnum_h_ast.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

/// A structure containing the global context needed when
/// transmogrifying info
typedef struct GenesisTransmogrifyMissionContext {
	ZoneMapInfo* zmap_info;
	GenesisMapDescription* map_desc;
	GenesisMissionDescription* mission_desc;

	// accumulating data
	GenesisZoneMission* zone_mission_accum;
} GenesisTransmogrifyMissionContext;

typedef struct ObjectFSMTempData {
	GenesisFSM *fsm;
	FSMState *state;
	const char *objName;
} ObjectFSMTempData;

typedef struct ObjectFSMData {
	char* challengeName;
	ObjectFSMTempData **fsmsAndStates;
	GenesisFSM **fsms;
} ObjectFSMData;

AUTO_STRUCT;
typedef struct GenesisMissionExtraMessages {
	const char* filename;						AST(CURRENTFILE)
	DisplayMessage** messages;					AST(NAME("ExtraMessage"))
} GenesisMissionExtraMessages;
extern ParseTable parse_GenesisMissionExtraMessages[];
#define TYPE_parse_GenesisMissionExtraMessages GenesisMissionExtraMessages

/// A structure containing the global context needed when generating
/// info
typedef struct GenesisMissionContext {
	ZoneMapInfo* zmap_info;
	GenesisZoneMapData* genesis_data;
	GenesisZoneMission* zone_mission;
	int mission_num;
	GenesisMissionPrompt** extra_prompts;
	GenesisConfig* config;
	bool is_ugc;
	const char *project_prefix;

	// accumulating data
	MissionDef*** optional_objectives_accum;
	MissionDef* root_mission_accum;
	ContactDef*** contacts_accum;
	FSM*** fsm_accum;
	GenesisMissionExtraMessages* extra_messages_accum;
	GenesisMissionRequirements* req_accum;
} GenesisMissionContext;

#ifndef NO_EDITORS

#define langCreateMessage(...) youShouldCall_genesisCreateMessage

/// Objectives
static void genesisTransmogrifyObjectiveFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionObjective* objective_desc );

static void genesisCreateObjective( MissionDef** outStartObjective, MissionDef** outEndObjective, GenesisMissionContext* context, MissionDef* grantingObjective, GenesisMissionObjective* objective_desc );
static MissionDef* genesisCreateObjectiveOptional( GenesisMissionContext* context, GameEvent** completeEvents, int count, char* debug_name );
static MissionDef* genesisCreateObjectiveOptionalExpr( GenesisMissionContext* context, char* exprText, char* debug_name );
static void genesisAccumObjectivesInOrder( SA_PARAM_NN_VALID MissionDef *accum, SA_PARAM_NN_VALID GenesisMissionContext* context, SA_PARAM_OP_VALID MissionDef* grantingObjective, GenesisMissionObjective** objective_descs );
static void genesisAccumObjectivesAllOf( SA_PARAM_NN_VALID MissionDef *accum, SA_PARAM_NN_VALID GenesisMissionContext* context, SA_PARAM_OP_VALID MissionDef* grantingObjective, GenesisMissionObjective** objective_descs );
static void genesisAccumObjectivesBranch( SA_PARAM_NN_VALID MissionDef *accum, SA_PARAM_NN_VALID GenesisMissionContext* context, SA_PARAM_OP_VALID MissionDef* grantingObjective, GenesisMissionObjective** objective_descs );
static void genesisAccumFailureExpr( MissionDef* accum, const char* exprText );
static WorldInteractionPropertyEntry* genesisClickieMakeInteractionEntry( GenesisMissionContext* context, GenesisRuntimeErrorContext* error_context, GenesisMissionChallengeClickie* clickie, GenesisCheckedAttrib* checked_attrib );

/// Prompts
static void genesisTransmogrifyPromptFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionPrompt* prompt );
static void genesisTransmogrifyPortalFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionPortal* portal );
static void genesisCreatePrompt( GenesisMissionContext* context, GenesisMissionPrompt* prompt );
static SpecialDialogBlock* genesisCreatePromptBlock( GenesisMissionContext* context, GenesisMissionPrompt* prompt, int blockIndex );
static WorldGameActionProperties* genesisCreatePromptAction( GenesisMissionContext* context, GenesisMissionPrompt* prompt );
static GenesisMissionPrompt* genesisTransmogrifyFindPrompt( GenesisTransmogrifyMissionContext* context, char* prompt_name );
static GenesisMissionPrompt* genesisFindPrompt( GenesisMissionContext* context, char* prompt_name );
static GenesisMissionPromptBlock* genesisFindPromptBlock( GenesisMissionContext* context, GenesisMissionPrompt* prompt, char* block_name );
static char** genesisPromptBlockNames( GenesisMissionContext* context, GenesisMissionPrompt* prompt, bool isComplete );
static char* genesisSpecialDialogBlockNameTemp( const char* promptName, const char* blockName );

static ContactDef* genesisInternContactDef( GenesisMissionContext* context, const char* contact_name );
static void genesisCreatePromptOptionalAction( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr );
static WorldOptionalActionVolumeEntry* genesisCreatePromptOptionalActionEntry( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr );
SA_RET_NN_VALID static WorldInteractionPropertyEntry* genesisCreatePromptInteractionEntry( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr );

/// FSMs
static void genesisTransmogrifyFSMFixup( GenesisTransmogrifyMissionContext* context, GenesisFSM* gfsm);
static void genesisBucketFSM(GenesisMissionContext *context, ObjectFSMData ***dataAray, GenesisFSM *fsm);
static void genesisCreateFSM(GenesisMissionContext *context, ObjectFSMData *data);
const char* genesisExternVarGetMsgKey(GenesisMissionContext *context, const char* varPrefix, WorldVariableDef* def);

/// Challenges
static GenesisMissionZoneChallenge* genesisTransmogrifyChallenge( GenesisTransmogrifyMissionContext* context, GenesisMissionChallenge* challenge );
static void genesisCreateChallenge( GenesisMissionContext* context, GenesisMissionZoneChallenge* challenge );
static void genesisChallengeNameFixup( GenesisTransmogrifyMissionContext* context, char** challengeName );

/// Portals
static void genesisCreatePortal( GenesisMissionContext* context, GenesisMissionPortal* portal, bool isReversed );

/// When conditions
static char* genesisWhenExprText( GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isEncounter );
static char* genesisWhenExprTextRaw(
		GenesisMissionContext* context, const char* overrideZmapName, GenesisMissionGenerationType overrideGenerationType,
		const char* overrideMissionName, GenesisMissionZoneChallenge** overrideChallenges,
		GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isEncounter );
static void genesisWhenMissionExprTextAndEvents( char** outEstr, GameEvent*** outEvents, bool* outShowCount, GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName );
static void genesisWhenMissionWaypointObjects( char*** out_makeVolumeObjects, MissionWaypoint*** out_waypoints, GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName );
MissionWaypoint* genesisCreateMissionWaypointForExternalChallenge( GenesisWhenExternalChallenge* challenge );

/// Checked Attribs
static char* genesisCheckedAttribText( GenesisMissionContext* context, GenesisCheckedAttrib* attrib, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isTeam );

/// Event fillers
///
/// TomY ENCOUNTER_HACK these functions can be greatly cleaned up one
/// the encounter hack is done.
static void genesisWriteText( char** estr, GameEvent* event, bool escaped );
static GameEvent* genesisCompleteChallengeEvent( GenesisChallengeType challengeType, const char* challengeName, bool useGroup, const char* zmapName );
static GameEvent* genesisReachLocationEvent( const char* layoutName, const char* roomOrChallengeName, const char* missionName, const char* zmapName );
static GameEvent* genesisReachLocationEventRaw( const char* zmapName, const char* volumeName );
static GameEvent* genesisKillCritterEvent( const char* critterDefName, const char* zmapName );
static GameEvent* genesisKillCritterGroupEvent( const char* critterGroupName, const char* zmapName );
static GameEvent* genesisTalkToContactEvent( char* contactName );
static GameEvent* genesisPromptEvent( char* dialogName, char* blockName, bool isComplete, const char* missionName, const char* zmapName, const char* challengeName );
static GameEvent* genesisExternalPromptEvent( char* dialogName, char* contactName, bool isComplete );
static GameEvent* genesisCompleteObjectiveEvent( GenesisMissionObjective *obj, const char* zmapName );

/// Misc utils
static const void genesisMissionUpdateFilename( GenesisMissionContext* context, MissionDef* mission );
static bool genesisTransmogrifyMissionValidate( GenesisTransmogrifyMissionContext* context );
static bool genesisTransmogrifySharedChallengesValidate( GenesisTransmogrifyMissionContext* context );
static bool genesisTransmogrifyChallengeValidate( GenesisTransmogrifyMissionContext* context, GenesisMissionChallenge* challenge );
static bool genesisGenerateMissionValidate( GenesisMissionContext* context );
static void genesisGenerateMissionValidateObjective( GenesisMissionContext* context, StashTable table, bool* fatalAccum, GenesisMissionObjective* objective );
static void genesisGenerateMissionPlayerData( GenesisMissionContext* context, MissionDef* accum );
static void genesisMessageFillKeys( GenesisMissionContext* context );
static void genesisContactMessageFillKeys( ContactDef * accum );
static void genesisParamsMessageFillKeys( GenesisMissionContext* context, GenesisProceduralObjectParams* params, int* messageIt );
static void genesisInteractParamsMessageFillKeys( GenesisMissionContext* context, const char* challengeName, GenesisInteractObjectParams* params, int* messageIt );
static void genesisInstancedParamsMessageFillKeys( GenesisMissionContext* context, const char* challengeName, GenesisInstancedObjectParams* params, int* messageIt );
static ContactMissionOffer* genesisInternMissionOffer( GenesisMissionContext* context, const char* mission_name, bool isReturnOnly );
static GenesisMissionRequirements* genesisInternRequirement( GenesisMissionContext* context );
static GenesisMissionExtraVolume* genesisInternExtraVolume( GenesisMissionContext* context, const char* volume_name );
static GenesisProceduralObjectParams* genesisInternRoomRequirementParams( GenesisMissionContext* context, const char* layout_name, const char* room_name );
static GenesisMissionRoomRequirements* genesisInternRoomRequirements( GenesisMissionContext* context, const char* layout_name, const char* room_name );
static GenesisProceduralObjectParams* genesisInternStartRoomRequirementParams( GenesisMissionContext* context );
static GenesisInstancedObjectParams* genesisInternChallengeRequirementParams( GenesisMissionContext* context, const char* challenge_name );
static GenesisInteractObjectParams* genesisInternInteractRequirementParams( GenesisMissionContext* context, const char* challenge_name );
static GenesisProceduralObjectParams* genesisInternVolumeRequirementParams( GenesisMissionContext* context, const char* challenge_name );
static WorldInteractionPropertyEntry* genesisCreateInteractableChallengeRequirement( GenesisMissionContext* context, const char* challenge_name );
static WorldInteractionPropertyEntry* genesisCreateInteractableChallengeVolumeRequirement( GenesisMissionContext* context, const char* challenge_name );
static DialogBlock* genesisCreateDialogBlock( GenesisMissionContext* context, char* dialogText, const char* astrAnimList );
static void genesisRefSystemUpdate( DictionaryHandleOrName dict, const char* key, void* obj );
static void genesisRunValidate( DictionaryHandleOrName dict, const char* key, void* obj );
static void genesisWriteTextFileFromDictionary( const char* filename, DictionaryHandleOrName dict );
static void genesisCreateMessage( GenesisMissionContext* context, DisplayMessage* dispMessage, const char* defaultString );
static void genesisPushAllRoomNames( GenesisMissionContext* context, char*** nameList );

/// name generation
static const char* genesisMissionName( GenesisMissionContext* context, bool playerSpecific );
//In header file: const char* genesisMissionNameRaw( const char* zmapName, const char* genesisMissionName, bool isOpenMission );
static const char* genesisContactName( GenesisMissionContext* context, GenesisMissionPrompt* prompt );

#endif

/// TomY ENCOUNTER_HACK -- these functions are used by encounter-hack
/// functions
static void genesisWriteText( char** estr, GameEvent* event, bool escaped );
static GameEvent* genesisCompleteChallengeEvent( GenesisChallengeType challengeType, const char* challengeName, bool useGroup, const char* zmapName );
static GameEvent* genesisReachLocationEvent( const char* layoutName, const char* roomOrChallengeName, const char* missionName, const char* zmapName );
static GameEvent* genesisReachLocationEventRaw( const char* zmapName, const char* volumeName );
static GameEvent* genesisKillCritterEvent( const char* critterDefName, const char* zmapName );
static GameEvent* genesisKillCritterGroupEvent( const char* critterGroupName, const char* zmapName );
static GameEvent* genesisTalkToContactEvent( char* contactName );
static GameEvent* genesisPromptEvent( char* dialogName, char* blockName, bool isComplete, const char* missionName, const char* zmapName, const char* challengeName );
static GameEvent* genesisExternalPromptEvent( char* dialogName, char* contactName, bool isComplete );

static char* genesisWhenExprTextRaw(
		GenesisMissionContext* context, const char* overrideZmapName, GenesisMissionGenerationType overrideGenerationType,
		const char* overrideMissionName, GenesisMissionZoneChallenge** overrideChallenges,
		GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isEncounter );

#ifndef NO_EDITORS

/// Convert MISSION, which describes a single mission, into a
/// more concrete form.
///
/// Returns a list of extra requirements to the map.
GenesisZoneMission* genesisTransmogrifyMission(ZoneMapInfo* zmap_info, GenesisMapDescription* map_desc, int mission_num)
{
	GenesisTransmogrifyMissionContext context = { zmap_info, map_desc, map_desc->missions[ mission_num ]};

	context.zone_mission_accum = StructCreate( parse_GenesisZoneMission );

	if( !genesisTransmogrifyMissionValidate( &context )) {
		return NULL;
	}

	StructCopyAll( parse_GenesisMissionZoneDescription, &context.mission_desc->zoneDesc, &context.zone_mission_accum->desc );
	context.zone_mission_accum->bTrackingEnabled = map_desc->is_tracking_enabled;

	// Misc. fixup
	{
		GenesisMissionStartDescription* startDesc = &context.zone_mission_accum->desc.startDescription;
		
		if( !nullStr( startDesc->pcExitChallenge )) {
			genesisChallengeNameFixup( &context, &startDesc->pcExitChallenge );
		}
		if( !nullStr( startDesc->pcContinueChallenge )) {
			genesisChallengeNameFixup( &context, &startDesc->pcContinueChallenge );
		}
	}

	// Mission transmogrification
	{
		int it;
		for( it = 0; it != eaSize( &context.zone_mission_accum->desc.eaObjectives ); ++it ) {
			genesisTransmogrifyObjectiveFixup( &context, context.zone_mission_accum->desc.eaObjectives[ it ]);
		}
		for( it = 0; it != eaSize( &context.zone_mission_accum->desc.dropChallengeNames ); ++it ) {
			char** challengeName = &context.zone_mission_accum->desc.dropChallengeNames[ it ];
			if( !nullStr( *challengeName )) {
				genesisChallengeNameFixup( &context, challengeName );
			}
		}
	}
	
	// Contact transmogrification
	{
		int it;
		for( it = 0; it != eaSize( &context.zone_mission_accum->desc.eaPrompts ); ++it ) {
			genesisTransmogrifyPromptFixup( &context, context.zone_mission_accum->desc.eaPrompts[ it ]);
		}
	}

	// FSM transmogrification
	{
		FOR_EACH_IN_EARRAY(context.zone_mission_accum->desc.eaFSMs, GenesisFSM, gfsm)
		{
			genesisTransmogrifyFSMFixup(&context, gfsm);
		}
		FOR_EACH_END
	}

	FOR_EACH_IN_EARRAY(context.zone_mission_accum->desc.eaPortals, GenesisMissionPortal, portal)
	{
		genesisTransmogrifyPortalFixup( &context, portal );
	}
	FOR_EACH_END;

	// challenge transmogrification
	{
		int it;
		for( it = 0; it != eaSize( &context.mission_desc->eaChallenges ); ++it ) {
			GenesisMissionChallenge* challenge = context.mission_desc->eaChallenges[ it ];
			GenesisMissionZoneChallenge* zoneChallenge = genesisTransmogrifyChallenge( &context, challenge );
			eaPush( &context.zone_mission_accum->eaChallenges, zoneChallenge );
		}
	}
	
	return context.zone_mission_accum;
}

/// Transmoform all the non-mission specific challenges into a more
/// concrete form.
GenesisMissionZoneChallenge** genesisTransmogrifySharedChallenges(ZoneMapInfo* zmap_info, GenesisMapDescription* map_desc)
{
	GenesisTransmogrifyMissionContext context = { zmap_info, map_desc, NULL };
	GenesisMissionZoneChallenge** accum = NULL;

	if( !genesisTransmogrifySharedChallengesValidate( &context )) {
		return NULL;
	}
	
	{
		int it;
		for( it = 0; it != eaSize( &context.map_desc->shared_challenges ); ++it ) {
			GenesisMissionChallenge* challenge = context.map_desc->shared_challenges[ it ];
			GenesisMissionZoneChallenge* zoneChallenge = genesisTransmogrifyChallenge( &context, challenge );

			if( zoneChallenge ) {
				eaPush( &accum, zoneChallenge );
			}
		}
	}

	return accum;
}

/// Add a ProceduralEncounterProperties for CHALLENGE to PEP-LIST.
///
/// Needed by TomY ENCOUNTER_HACK.
void genesisTransmogrifyChallengePEPHack( GenesisMapDescription* map_desc, int mission_num, GenesisMissionChallenge* challenge, GenesisProceduralEncounterProperties*** outPepList )
{
	GenesisMissionDescription* mission;
	int encounterIt;

	if( mission_num >= 0 ) {
		mission = map_desc->missions[ mission_num ];
	} else {
		mission = NULL;
	}

	for( encounterIt = 0; encounterIt < challenge->iCount; ++encounterIt ) {
		GenesisProceduralEncounterProperties* encounter_property = StructCreate( parse_GenesisProceduralEncounterProperties );
		char encounterName[ 256 ];

		if( !mission ) {
			sprintf( encounterName, "Shared_%s_%02d", challenge->pcName, encounterIt );
		} else {
			sprintf( encounterName, "%s_%s_%02d", mission->zoneDesc.pcName, challenge->pcName, encounterIt );
		}
		encounter_property->encounter_name = allocAddString( encounterName );
		if( mission ) {
			encounter_property->genesis_mission_name = StructAllocString( mission->zoneDesc.pcName );
			encounter_property->genesis_open_mission = (mission->zoneDesc.generationType != GenesisMissionGenerationType_PlayerMission);
		}
		encounter_property->genesis_mission_num = mission_num;
		encounter_property->has_patrol = (SAFE_MEMBER(challenge->pEncounter, ePatrolType) != GENESIS_PATROL_None);

		StructCopyAll(parse_GenesisWhen, &challenge->spawnWhen, &encounter_property->spawn_when);
		{
			int challengeIt = 0;
			for( challengeIt = 0; challengeIt != eaSize( &encounter_property->spawn_when.eaChallengeNames ); ++challengeIt ) {
				bool challengeIsShared;
				GenesisMissionChallenge* spawnWhenChallenge = genesisFindChallenge( map_desc, mission, encounter_property->spawn_when.eaChallengeNames[ challengeIt ], &challengeIsShared );
				if( spawnWhenChallenge ) {
					GenesisMissionZoneChallenge* spawnWhenChallengeInfo = StructCreate( parse_GenesisMissionZoneChallenge );
					char buffer[ 256 ];

					assert( mission || challengeIsShared );
					if( challengeIsShared ) {
						sprintf( buffer, "Shared_%s", spawnWhenChallenge->pcName );
					} else {
						sprintf( buffer, "%s_%s", mission->zoneDesc.pcName, spawnWhenChallenge->pcName );
					}
					spawnWhenChallengeInfo->pcName = StructAllocString( buffer );
								
					spawnWhenChallengeInfo->eType = spawnWhenChallenge->eType;
					if( spawnWhenChallenge->iNumToSpawn <= 0 ) {
						spawnWhenChallengeInfo->iNumToComplete = spawnWhenChallenge->iCount;
					} else {
						spawnWhenChallengeInfo->iNumToComplete = spawnWhenChallenge->iNumToSpawn;
					}
					
					eaPush( &encounter_property->when_challenges, spawnWhenChallengeInfo );
					StructCopyString(&encounter_property->spawn_when.eaChallengeNames[ challengeIt ], buffer);
				} else {
					genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge(challenge->pcName, SAFE_MEMBER(mission, zoneDesc.pcName), challenge->pcLayoutName),
									   "SpawnWhen references challenge \"%s\", but it does not exist.",
									   challenge->spawnWhen.eaChallengeNames[ challengeIt ]);
				}
			}
		}
						
		eaPush( outPepList, encounter_property );
	}
}

/// Return an earray of all reference keys currently in DICT that
/// belong to ZMAP.
///
/// Do not free the strings in the earray.  They are pooled!
static const char** genesisListReferences( const char* zmap_filename, DictionaryHandleOrName dict )
{
	const char** accum = NULL;
	ResourceDictionaryInfo* dictInfo = resDictGetInfo( dict );
	char path[ MAX_PATH ];
	strcpy( path, zmap_filename );
	getDirectoryName( path );
	strcat( path, "/" );

	{
		int it;
		for( it = 0; it != eaSize( &dictInfo->ppInfos ); ++it ) {
			ResourceInfo* info = dictInfo->ppInfos[ it ];

			if( strStartsWith( info->resourceLocation, path )) {
				eaPush( &accum, info->resourceName );
			}
		}
	}

	return accum;
}

/// Delete all missions associated with zmap
void genesisDeleteMissions(const char* zmap_filename)
{
	ResourceActionList actions = { 0 };
	const char** oldMissionNames = genesisListReferences( zmap_filename, g_MissionDictionary );
	const char** oldContactNames = genesisListReferences( zmap_filename, g_ContactDictionary );

	resSetDictionaryEditMode( g_MissionDictionary, true );
	resSetDictionaryEditMode( g_ContactDictionary, true );
	resSetDictionaryEditMode( gMessageDict, true );

	{
		int it;
		for( it = 0; it != eaSize( &oldContactNames ); ++it ) {
			resAddRequestLockResource( &actions, g_ContactDictionary, oldContactNames[ it ], NULL );
			resAddRequestSaveResource( &actions, g_ContactDictionary, oldContactNames[ it ], NULL );
		}
		for( it = 0; it != eaSize( &oldMissionNames ); ++it ) {
			resAddRequestLockResource( &actions, g_MissionDictionary, oldMissionNames[ it ], NULL );
			resAddRequestSaveResource( &actions, g_MissionDictionary, oldMissionNames[ it ], NULL );
		}
	}

	actions.bDisableValidation = true;
	resRequestResourceActions( &actions );

	if( actions.eResult != kResResult_Success ) {
		int it;
		for( it = 0; it != eaSize( &actions.ppActions ); ++it ) {
			if( actions.ppActions[ it ]->eResult == kResResult_Success ) {
				continue;
			}

			genesisRaiseErrorInternalCode( GENESIS_ERROR, "%s Resource: %s -- %s",
										   actions.ppActions[ it ]->pDictName,
										   actions.ppActions[ it ]->pResourceName,
										   actions.ppActions[ it ]->estrResultString );
		}
	}

	StructDeInit( parse_ResourceActionList, &actions );
	eaDestroy( &oldMissionNames );
	eaDestroy( &oldContactNames );
}

void genesisDestroyObjectFSMData(ObjectFSMData *data)
{
	free( data->challengeName );
	eaDestroyEx(&data->fsmsAndStates, NULL);
	eaDestroy(&data->fsms);

	free(data);
}

/// Generate mission/contact information for ZONE-MISSION.
GenesisMissionRequirements* genesisGenerateMission(
		ZoneMapInfo* zmap_info, int mission_num, GenesisZoneMission* mission, GenesisMissionAdditionalParams* additionalParams,
		bool is_ugc, const char *project_prefix, bool write_mission)
{
	GenesisZoneMapData* genesis_data = zmap_info ? zmapInfoGetGenesisData( zmap_info ) : NULL;
	GenesisMissionContext context = { zmap_info, genesis_data, mission, mission_num };
	ContactDef** contactsAccum = NULL;
	MissionDef* missionAccum = NULL;
	MissionDef* playerSpecificMissionAccum = NULL;
	MissionDef** optionalObjectivesAccum = NULL;
	FSM** fsmAccum = NULL;

	context.is_ugc = is_ugc;
	context.config = genesisConfig();
	context.contacts_accum = &contactsAccum;
	context.fsm_accum = &fsmAccum;
	context.extra_messages_accum = StructCreate( parse_GenesisMissionExtraMessages );
	{
		char buffer[ MAX_PATH ];
		char zmapDirectory[ MAX_PATH ];
		
		strcpy( zmapDirectory, zmapInfoGetFilename( zmap_info ));
		getDirectoryName( zmapDirectory );
		sprintf( buffer, "%s/messages/%sExtra", zmapDirectory, mission->desc.pcName );
		context.extra_messages_accum->filename = StructAllocString( buffer );
	}
	
	context.req_accum = StructCreate( parse_GenesisMissionRequirements );
	context.req_accum->missionName = StructAllocString( context.zone_mission->desc.pcName );
	context.optional_objectives_accum = &optionalObjectivesAccum;
	context.project_prefix = project_prefix;

	if( !genesisGenerateMissionValidate( &context )) {
		return NULL;
	}

	#ifndef GAMESERVER
	{
		assert( !write_mission );
	}
	#endif

	// Door generation
	{
		GenesisMissionStartDescription* startDesc = &context.zone_mission->desc.startDescription;
		
		char missionSucceeded[ 1024 ];
		char missionNotSucceeded[ 1024 ];
		if( context.zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission ) {
			sprintf( missionSucceeded, "OpenMissionStateSucceeded(\"%s\")", genesisMissionName( &context, false ));
		} else {
			sprintf( missionSucceeded, "HasCompletedMission(\"%s\") or MissionStateSucceeded(\"%s\")",
					 genesisMissionName( &context, false ), genesisMissionName( &context, false ));
		}
		sprintf( missionNotSucceeded, "not (%s)", missionSucceeded );

		{
			GenesisMissionPrompt* missionReturnPrompt = genesisFindPrompt( &context, "MissionReturn" );

			switch( startDesc->eExitFrom ) {
				case GenesisMissionExitFrom_Anywhere:
					// nothing to do

				xcase GenesisMissionExitFrom_Challenge: {
					WorldInteractionPropertyEntry* entry = genesisCreateInteractableChallengeRequirement( &context, startDesc->pcExitChallenge );
					if(   startDesc->pcExitChallenge && startDesc->pcContinueChallenge
						  && stricmp( startDesc->pcExitChallenge, startDesc->pcContinueChallenge ) == 0 ) {
						entry->pInteractCond = exprCreateFromString( missionNotSucceeded, NULL );
					}
				
					if( missionReturnPrompt ) {
						entry->pcInteractionClass = allocAddString( "CLICKABLE" );
						entry->pActionProperties = StructCreate( parse_WorldActionInteractionProperties );
						eaPush( &entry->pActionProperties->successActions.eaActions, genesisCreatePromptAction( &context, missionReturnPrompt ));
					} else {
						entry->pcInteractionClass = allocAddString( "DOOR" );
						entry->pDoorProperties = StructCreate( parse_WorldDoorInteractionProperties );
						StructReset( parse_WorldVariableDef, &entry->pDoorProperties->doorDest );
						entry->pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
						entry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
						entry->pDoorProperties->doorDest.pSpecificValue = StructCreate(parse_WorldVariable);
						entry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
						entry->pDoorProperties->doorDest.pSpecificValue->pcStringVal = StructAllocString( "MissionReturn" );
						COPY_HANDLE( entry->pDoorProperties->hTransSequence, startDesc->hExitTransitionOverride );
					}
				}

				xcase GenesisMissionExitFrom_DoorInRoom: case GenesisMissionExitFrom_Entrance:
					// handled elsewhere, nothing to do here
					;
			}
		}

		if( startDesc->bContinue ) {
			GenesisMissionPrompt* missionContinuePrompt = genesisFindPrompt( &context, "MissionContinue" );

			switch( startDesc->eContinueFrom ) {
				case GenesisMissionExitFrom_Anywhere:
					// nothing to do

				xcase GenesisMissionExitFrom_Challenge: {
					WorldInteractionPropertyEntry* entry = genesisCreateInteractableChallengeRequirement( &context, startDesc->pcContinueChallenge );
					entry->pInteractCond = exprCreateFromString( missionSucceeded, NULL );

					if( missionContinuePrompt ) {
						entry->pcInteractionClass = allocAddString( "CLICKABLE" );
						entry->pActionProperties = StructCreate( parse_WorldActionInteractionProperties );
						eaPush( &entry->pActionProperties->successActions.eaActions, genesisCreatePromptAction( &context, missionContinuePrompt ));
					} else {
						entry->pcInteractionClass = allocAddString( "DOOR" );
						entry->pDoorProperties = StructCreate( parse_WorldDoorInteractionProperties );
						entry->pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
						entry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
						entry->pDoorProperties->doorDest.pSpecificValue = StructCreate(parse_WorldVariable);
						entry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
						entry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap = StructAllocString( context.zone_mission->desc.startDescription.pcContinueMap );
						COPY_HANDLE(entry->pDoorProperties->hTransSequence, startDesc->hContinueTransitionOverride );

						{
							int i;
							for( i = 0; i != eaSize( &context.zone_mission->desc.startDescription.eaContinueVariables ); ++i ) {
								WorldVariable* continueVar = context.zone_mission->desc.startDescription.eaContinueVariables[ i ];
								WorldVariableDef* continueVarDef = StructCreate( parse_WorldVariableDef );

								continueVarDef->pcName = continueVar->pcName;
								continueVarDef->eType = continueVar->eType;
								continueVarDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
								continueVarDef->pSpecificValue = StructClone( parse_WorldVariable, continueVar );
								eaPush( &entry->pDoorProperties->eaVariableDefs, continueVarDef );
							}
						}
					}
				}

				xcase GenesisMissionExitFrom_DoorInRoom:
					if( missionContinuePrompt ) {
						GenesisProceduralObjectParams* params = genesisInternRoomRequirementParams( &context, startDesc->pcContinueLayout, startDesc->pcContinueRoom );
						genesisProceduralObjectSetOptionalActionVolume( params );
						eaPush( &params->optionalaction_volume_properties->entries,
								genesisCreatePromptOptionalActionEntry( &context, missionContinuePrompt, missionSucceeded ));
					} else {
						genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
											 "ContinueFrom door (as clickie) not supported yet." );
					}

				xcase GenesisMissionExitFrom_Entrance:
					if( missionContinuePrompt ) {
						GenesisProceduralObjectParams* params = genesisInternStartRoomRequirementParams( &context );
						genesisProceduralObjectSetOptionalActionVolume( params );
						eaPush( &params->optionalaction_volume_properties->entries,
								genesisCreatePromptOptionalActionEntry( &context, missionContinuePrompt, missionSucceeded ));
					} else {
						genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
											 "ContinueFrom door (as clickie) not supported yet." );
					}
			}
		}
	}

	// Portal generation
	{
		int it;
		for( it = 0; it != eaSize( &context.zone_mission->desc.eaPortals ); ++it ) {
			GenesisMissionPortal* portal = context.zone_mission->desc.eaPortals[ it ];

			genesisCreatePortal( &context, portal, false );
			if( portal->eType != GenesisMissionPortal_OneWayOutOfMap ) {
				genesisCreatePortal( &context, portal, true );
			}
		}
	}
	
	// Mission generation
	{
		// base mission info
		missionAccum = StructCreate( parse_MissionDef );
		missionAccum->name = genesisMissionName( &context, false );
		if( context.zmap_info ) {
			missionAccum->genesisZonemap = StructAllocString( zmapInfoGetPublicName( context.zmap_info ));
		}
		COPY_HANDLE( missionAccum->hCategory, context.zone_mission->desc.hCategory );
		missionAccum->version = 1;
		missionAccum->comments = StructAllocString( "Autogenerated Genesis mission." );

		StructCopyAll( parse_MissionLevelDef, &context.zone_mission->desc.levelDef, &missionAccum->levelDef );

		if( context.zone_mission->desc.pcDisplayName ) {
			genesisCreateMessage( &context, &missionAccum->displayNameMsg, context.zone_mission->desc.pcDisplayName );
		}
		genesisCreateMessage( &context, &missionAccum->uiStringMsg, context.zone_mission->desc.pcShortText );
		missionAccum->eReturnType = MissionReturnType_Message;
		genesisCreateMessage( &context, &missionAccum->msgReturnStringMsg, context.zone_mission->desc.strReturnText );

		// open mission info
		if( context.zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission ) {
			GenesisMissionRequirements* missionReq = genesisInternRequirement( &context );

			missionAccum->missionType = MissionType_OpenMission;

			// Push all layout names onto the open mission volume
			{
				int it;
				for( it = 0; it != eaSize( &genesis_data->solar_systems ); ++it ) {
					eaPush( &missionAccum->eaOpenMissionVolumes, StructAllocString(
									genesisMissionVolumeName( genesis_data->solar_systems[ it ]->layout_name, missionReq->missionName )));
				}
				for( it = 0; it != eaSize( &genesis_data->genesis_interiors ); ++it ) {
					eaPush( &missionAccum->eaOpenMissionVolumes, StructAllocString(
									genesisMissionVolumeName( genesis_data->genesis_interiors[ it ]->layout_name, missionReq->missionName )));
				}
				if( genesis_data->genesis_exterior ) {
					eaPush( &missionAccum->eaOpenMissionVolumes, StructAllocString(
									genesisMissionVolumeName( genesis_data->genesis_exterior->layout_name, missionReq->missionName )));
				}
				if( genesis_data->genesis_exterior_nodes ) {
					eaPush( &missionAccum->eaOpenMissionVolumes, StructAllocString(
									genesisMissionVolumeName( genesis_data->genesis_exterior_nodes->layout_name, missionReq->missionName )));
				}
			}
			
			if( !missionReq->params ) {
				missionReq->params = StructCreate( parse_GenesisProceduralObjectParams );
			}
			genesisProceduralObjectSetEventVolume(missionReq->params);
			missionAccum->autoGrantOnMap = StructAllocString( zmapInfoGetPublicName( context.zmap_info ));
			
			missionAccum->needsReturn = false;

			if( context.zone_mission->desc.generationType != GenesisMissionGenerationType_OpenMission_NoPlayerMission ) {
				// And create the minimal player specific mission
				playerSpecificMissionAccum = StructCreate( parse_MissionDef );
				playerSpecificMissionAccum->name = genesisMissionName( &context, true );
				playerSpecificMissionAccum->genesisZonemap = StructAllocString( zmapInfoGetPublicName( context.zmap_info ));
				COPY_HANDLE( playerSpecificMissionAccum->hCategory, context.zone_mission->desc.hCategory );
				playerSpecificMissionAccum->version = 1;
				playerSpecificMissionAccum->comments = StructAllocString( "Autogenerated Genesis mission." );

				StructCopyAll( parse_MissionLevelDef, &context.zone_mission->desc.levelDef, &playerSpecificMissionAccum->levelDef );

				if( context.zone_mission->desc.pOpenMissionDescription->pcPlayerSpecificDisplayName ) {
					genesisCreateMessage( &context, &playerSpecificMissionAccum->displayNameMsg, context.zone_mission->desc.pOpenMissionDescription->pcPlayerSpecificDisplayName );
				}
				genesisCreateMessage( &context, &playerSpecificMissionAccum->uiStringMsg, context.zone_mission->desc.pOpenMissionDescription->pcPlayerSpecificShortText );

				// listen for the open mission completing
				playerSpecificMissionAccum->doNotUncomplete = true;
				{
					char buffer[ 256 ];
					playerSpecificMissionAccum->meSuccessCond = StructCreate( parse_MissionEditCond );
					playerSpecificMissionAccum->meSuccessCond->type = MissionCondType_Expression;
					sprintf( buffer, "OpenMissionMapCredit(\"%s\")", genesisMissionName( &context, false ));
					playerSpecificMissionAccum->meSuccessCond->valStr = StructAllocString( buffer );
				}

				genesisGenerateMissionPlayerData( &context, playerSpecificMissionAccum );
			}
		} else {
			genesisGenerateMissionPlayerData( &context, missionAccum );
		}
		
		missionAccum->ePlayType = context.zone_mission->desc.ePlayType;
		missionAccum->ugcProjectID = context.zone_mission->desc.ugcProjectID;
		missionAccum->eAuthorSource = context.zone_mission->desc.eAuthorSource;
		if( playerSpecificMissionAccum ) {
			playerSpecificMissionAccum->ePlayType = context.zone_mission->desc.ePlayType;
			playerSpecificMissionAccum->ugcProjectID = context.zone_mission->desc.ugcProjectID;
			playerSpecificMissionAccum->eAuthorSource = context.zone_mission->desc.eAuthorSource;
		}

		context.root_mission_accum = missionAccum;
		genesisAccumObjectivesInOrder( missionAccum, &context, NULL, context.zone_mission->desc.eaObjectives );

		if( additionalParams ) {
			// add the extra properties
			{
				int it;
				for( it = 0; it != eaSize( &additionalParams->eaInteractableOverrides ); ++it ) {
					eaPush( &missionAccum->ppInteractableOverrides,
							StructClone( parse_InteractableOverride, additionalParams->eaInteractableOverrides[ it ]));
				}
			}

			// add the mission offer overrides
			{
				int it;
				for( it = 0; it != eaSize( &additionalParams->eaMissionOfferOverrides ); ++it ) {
					eaPush( &missionAccum->ppMissionOfferOverrides,
							StructClone( parse_MissionOfferOverride, additionalParams->eaMissionOfferOverrides[ it ]));
				}
			}
			// add ImageMenuItemOverrides
			{
				int i;
				for( i = 0; i != eaSize( &additionalParams->eaImageMenuItemOverrides ); ++i ) {
					eaPush( &missionAccum->ppImageMenuItemOverrides,
							StructClone( parse_ImageMenuItemOverride, additionalParams->eaImageMenuItemOverrides[ i ]));
				}
			}
			// add success actions
			{
				int i;
				for( i = 0; i != eaSize( &additionalParams->eaSuccessActions ); ++i ) {
					eaPush( &missionAccum->ppSuccessActions,
							StructClone( parse_WorldGameActionProperties, additionalParams->eaSuccessActions[ i ]));
				}
			}
		}
		
		genesisMissionUpdateFilename( &context, missionAccum );

		if( playerSpecificMissionAccum ) {
			genesisMissionUpdateFilename( &context, playerSpecificMissionAccum );
		}
	}

	// In-Mission drop generation
	if( IS_HANDLE_ACTIVE( context.zone_mission->desc.dropRewardTable )) {
		int it;
		for( it = 0; it != eaSize( &context.zone_mission->desc.dropChallengeNames ); ++it ) {
			const char* challengeName = context.zone_mission->desc.dropChallengeNames[ it ];
			GenesisMissionZoneChallenge* challenge = genesisFindZoneChallenge(
					context.genesis_data, context.zone_mission, challengeName );
			MissionDrop* accum;
			
			if( !challenge ) {
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
									 "Drop specifies challenge \"%s\", but it does not exist.", challengeName );
				continue;
			}
			if( challenge->eType != GenesisChallenge_Encounter2 ) {
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
									 "Drop specifies challenge \"%s\", but it is not Encounter2.", challengeName );
				continue;
			}
			
			accum = StructCreate( parse_MissionDrop );
			accum->type = MissionDropTargetType_EncounterGroup;
			accum->whenType = MissionDropWhenType_DuringMission;
			accum->value = allocAddString( challengeName );
			accum->RewardTableName = REF_STRING_FROM_HANDLE( context.zone_mission->desc.dropRewardTable );
			accum->pchMapName = zmapInfoGetPublicName( context.zmap_info );
			if( !missionAccum->params ) {
				missionAccum->params = StructCreate( parse_MissionDefParams );
			}
			eaPush( &missionAccum->params->missionDrops, accum );
		}

		if( eaSize( &context.zone_mission->desc.dropChallengeNames ) == 0 ) {
			genesisRaiseError( GENESIS_WARNING, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
								 "Drop has a reward table, but it specifies no Drop challenges." );
		}
	} else {
		if( eaSize( &context.zone_mission->desc.dropChallengeNames )) {
			genesisRaiseError( GENESIS_WARNING, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName),
								 "Drop has no reward table, but it specifies Drop challenges." );
		}
	}
	
	// Contact generation
	{
		int it;
		for( it = 0; it != eaSize( &context.zone_mission->desc.eaPrompts ); ++it ) {
			GenesisMissionPrompt* prompt = context.zone_mission->desc.eaPrompts[ it ];
			genesisCreatePrompt( &context, prompt );
		}
		for( it = 0; it != eaSize( &context.extra_prompts ); ++it ) {
			GenesisMissionPrompt* prompt = context.extra_prompts[ it ];
			genesisCreatePrompt( &context, prompt );
		}

		for( it = 0; it != eaSize( &contactsAccum ); ++it ) {
			ContactDef* def = contactsAccum[ it ];
			genesisContactMessageFillKeys( def );
		}
	}

	// FSM generation
	{
		int i;
		ObjectFSMData **fsmData = NULL;
		// Figure out all the FSMs placed on each actor
		for(i=0; i<eaSize(&context.zone_mission->desc.eaFSMs); i++)
		{
			GenesisFSM *fsm = context.zone_mission->desc.eaFSMs[i];
			genesisBucketFSM(&context, &fsmData, fsm);
		}

		// Build an FSM from all the parts (may not generate an FSM in simple cases)
		for(i=0; i<eaSize(&fsmData); i++)
		{
			ObjectFSMData *data = fsmData[i];
			genesisCreateFSM(&context, data);
		}
		eaDestroyEx(&fsmData, genesisDestroyObjectFSMData);
	}

	// Challenge generation
	{
		int it;
		for( it = 0; it != eaSize( &context.zone_mission->eaChallenges ); ++it ) {
			GenesisMissionZoneChallenge* challenge = context.zone_mission->eaChallenges[ it ];
			genesisCreateChallenge( &context, challenge );
		}
	}

	eaPushEArray( &missionAccum->subMissions, &optionalObjectivesAccum );
	{
		int it;
		for( it = 0; it != eaSize( &optionalObjectivesAccum ); ++it ) {
			WorldGameActionProperties* grantObjective = StructCreate( parse_WorldGameActionProperties );
			grantObjective->eActionType = WorldGameActionType_GrantSubMission;
			grantObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
			grantObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( optionalObjectivesAccum[ it ]->name );
			eaPush( &missionAccum->ppOnStartActions, grantObjective );
		}
	}
	eaDestroy( &optionalObjectivesAccum );

	if( context.zmap_info ) {
		genesisMessageFillKeys( &context );
	}
	genesisMissionMessageFillKeys( missionAccum, NULL );
	if( playerSpecificMissionAccum ) {
		genesisMissionMessageFillKeys( playerSpecificMissionAccum, NULL );
	}

	/// EVERYTHING IS FULLY GENERATED

	// This has to happen even on the client, as the layers point to these keys (Not needed in UGC since we commit to layers immediately)
	if( context.zmap_info ) {
		if( !context.is_ugc ) {
			langApplyEditorCopySingleFile( parse_GenesisMissionRequirements, context.req_accum, true, !write_mission );
		}
		langApplyEditorCopySingleFile( parse_GenesisMissionExtraMessages, context.extra_messages_accum, true, !write_mission );
	} else {
		if(   context.req_accum->roomRequirements || context.req_accum->challengeRequirements
			  || context.req_accum->extraVolumes || context.req_accum->params ) {
			genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission( context.zone_mission->desc.pcName ),
							   "Mission generated map requirements, but there is no associated map.  Likely the mission will not work." );
		}	
	}

	if( write_mission ) 
	{
		ResourceActionList tempList = {0};
		int it;
		resSetDictionaryEditMode( gFSMDict, true );
		resSetDictionaryEditMode( g_MissionDictionary, true );
		resSetDictionaryEditMode( g_ContactDictionary, true );
		resSetDictionaryEditMode( gMessageDict, true );

		for( it = 0; it != eaSize( &contactsAccum ); ++it ) 
		{
			resAddRequestLockResource(&tempList, g_ContactDictionary, contactsAccum[ it ]->name, contactsAccum[ it ]);
			resAddRequestSaveResource(&tempList, g_ContactDictionary, contactsAccum[ it ]->name, contactsAccum[ it ]);
		}

		for( it = 0; it <eaSize(&fsmAccum); it++)
		{
			if(!fsmGenerate(fsmAccum[it]))
			{
				genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMission(context.zone_mission->desc.pcName), 
								"FSM %s failed to generate", fsmAccum[it]->name);
			}

			resAddRequestLockResource(&tempList, gFSMDict, fsmAccum[ it ]->name, fsmAccum[ it ]);
			resAddRequestSaveResource(&tempList, gFSMDict, fsmAccum[ it ]->name, fsmAccum[ it ]);
		}

		resAddRequestLockResource(&tempList, g_MissionDictionary, missionAccum->name, missionAccum);
		resAddRequestSaveResource(&tempList, g_MissionDictionary, missionAccum->name, missionAccum);
		
		if (playerSpecificMissionAccum)
		{
			resAddRequestLockResource(&tempList, g_MissionDictionary, playerSpecificMissionAccum->name, playerSpecificMissionAccum);
			resAddRequestSaveResource(&tempList, g_MissionDictionary, playerSpecificMissionAccum->name, playerSpecificMissionAccum);
		}

		tempList.bDisableValidation = true;
		resRequestResourceActions(&tempList);

		if (tempList.eResult != kResResult_Success)
		{
			for (it = 0; it < eaSize(&tempList.ppActions); it++)
			{
				if (tempList.ppActions[it]->eResult == kResResult_Success)
				{
					continue;
				}
				genesisRaiseErrorInternalCode( GENESIS_ERROR, "%s Resource: %s -- %s",
											   tempList.ppActions[it]->pDictName,
											   tempList.ppActions[it]->pResourceName,
											   tempList.ppActions[it]->estrResultString);
			}
		}

		StructDeInit(parse_ResourceActionList, &tempList);
	}

	eaDestroyStruct( &contactsAccum, parse_ContactDef );
	eaDestroyStruct( &fsmAccum, parse_FSM );
	StructDestroy( parse_MissionDef, missionAccum );
	StructDestroy( parse_MissionDef, playerSpecificMissionAccum );
	StructDestroySafe( parse_GenesisMissionExtraMessages, &context.extra_messages_accum );
	
	return context.req_accum;
}

/// Generate a mission for EPISODE in EPISODE-ROOT.
void genesisGenerateEpisodeMission(const char* episode_root, GenesisEpisode* episode)
{
	MissionDef* missionAccum = NULL;
	char episodeMissionPath[ MAX_PATH ];
	sprintf( episodeMissionPath, "%s/%s.mission", episode_root, episode->name );

	missionAccum = StructCreate( parse_MissionDef );

	// generate boilerplate mission data
	missionAccum->name = StructAllocString( episode->name );
	missionAccum->version = 1;
	missionAccum->missionType = MissionType_Episode;
	
	missionAccum->comments = StructAllocString( "Autogenerated Genesis Episode mission." );

	StructCopyAll( parse_MissionLevelDef, &episode->levelDef, &missionAccum->levelDef );
	missionAccum->missionReqs = StructClone( parse_Expression, episode->missionReqs );

	genesisCreateMessage( NULL, &missionAccum->displayNameMsg, episode->pcDisplayName );
	genesisCreateMessage( NULL, &missionAccum->uiStringMsg, episode->pcShortText );
	COPY_HANDLE( missionAccum->hCategory, episode->hCategory );
	genesisCreateMessage( NULL, &missionAccum->summaryMsg, episode->pcSummaryText );
	genesisCreateMessage( NULL, &missionAccum->detailStringMsg, episode->pcDescriptionText );

	missionAccum->needsReturn = episode->grantDescription.bNeedsReturn;
	missionAccum->eReturnType = MissionReturnType_Message;
	genesisCreateMessage( NULL, &missionAccum->msgReturnStringMsg, episode->grantDescription.pcEpisodeReturnText );

	missionAccum->params = StructCreate( parse_MissionDefParams );
	missionAccum->params->NumericRewardScale = episode->fNumericRewardScale;
	missionAccum->params->OnsuccessRewardTableName = REF_STRING_FROM_HANDLE( episode->hReward );

	// generate mission linking
	{
		const char* nextPartName = NULL;
		int partIt;
		for( partIt = eaSize( &episode->parts ) - 1; partIt >= 0; --partIt ) {
			GenesisEpisodePart* epPart = episode->parts[ partIt ];
			const char* partMapName = genesisEpisodePartMapName( episode, REF_STRING_FROM_HANDLE( epPart->map_desc ));
			GenesisMapDescription* partMapDesc = GET_REF( epPart->map_desc );
			int partMissionDescIndex = genesisFindMission( partMapDesc, epPart->mission_name );

			if( partMapDesc && partMissionDescIndex >= 0 ) {
				GenesisMissionDescription* partMissionDesc = partMapDesc->missions[ partMissionDescIndex ];
				MissionDef* accum = StructCreate( parse_MissionDef );
				const char* partName;

				eaPush( &missionAccum->subMissions, accum );
				{
					char partNameBuffer[ 1024 ];
					sprintf( partNameBuffer, "%s_%s", partMapName, epPart->mission_name );
					partName = allocAddString( partNameBuffer );
				}

				// boiler plate data
				accum->name = partName;
				genesisCreateMessage( NULL, &accum->uiStringMsg, partMissionDesc->zoneDesc.pcDisplayName );
				accum->doNotUncomplete = true;
				
				accum->meSuccessCond = StructCreate( parse_MissionEditCond );
				accum->meSuccessCond->type = MissionCondType_Expression;
				{
					char expr[ 1024 ];
					sprintf( expr, "OpenMissionMapCredit(\"%s\")",
							 genesisMissionNameRaw( partMapName, epPart->mission_name, true ));
					accum->meSuccessCond->valStr = StructAllocString( expr );
				}
			
				if( nextPartName ) {
					WorldGameActionProperties* grantNextPart = StructCreate( parse_WorldGameActionProperties );
					grantNextPart->eActionType = WorldGameActionType_GrantSubMission;
					grantNextPart->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
					grantNextPart->pGrantSubMissionProperties->pcSubMissionName = nextPartName;
					eaPush( &accum->ppSuccessActions, grantNextPart );
				}

				nextPartName = partName;
			} else {
				char buffer[32];
				sprintf(buffer, "%d", partIt + 1);
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextEpisodePart(buffer),
									 "Part references mission \"%s\" in MapDesc \"%s\", but no such mission exists.",
									 epPart->mission_name, REF_STRING_FROM_HANDLE( epPart->map_desc ));
			}
		}

		// generate first link
		if( nextPartName ) {
			WorldGameActionProperties* grantFirstPart = StructCreate( parse_WorldGameActionProperties );
			grantFirstPart->eActionType = WorldGameActionType_GrantSubMission;
			grantFirstPart->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
			grantFirstPart->pGrantSubMissionProperties->pcSubMissionName = nextPartName;
			eaPush( &missionAccum->ppOnStartActions, grantFirstPart );
		}
	}

	genesisMissionMessageFillKeys( missionAccum, NULL );
	{
		char path[ MAX_PATH ];
		strcpy( path, episode_root );
		missionAccum->scope = allocAddFilename( path );

		strcatf( path, "/%s.mission", missionAccum->name );
		missionAccum->filename = allocAddFilename( path );
	}
	
	/// EVERYTHING IS FULLY GENERATED
	{
		ResourceActionList tempList = { 0 };

		resSetDictionaryEditMode( g_MissionDictionary, true );
		resSetDictionaryEditMode( gMessageDict, true );

		resAddRequestLockResource( &tempList, g_MissionDictionary, missionAccum->name, missionAccum );
		resAddRequestSaveResource( &tempList, g_MissionDictionary, missionAccum->name, missionAccum );

		resRequestResourceActions( &tempList );
		
		if( tempList.eResult != kResResult_Success ) {
			int it;
			for( it = 0; it != eaSize( &tempList.ppActions ); ++it ) {
				if( tempList.ppActions[ it ]->eResult == kResResult_Success ) {
					continue;
				}

				genesisRaiseErrorInternalCode( GENESIS_ERROR, "%s Resource: %s -- Error while saving: %s",
											   tempList.ppActions[ it ]->pDictName,
											   tempList.ppActions[ it ]->pResourceName,
											   tempList.ppActions[ it ]->estrResultString );
			}
		}

		StructDeInit( parse_ResourceActionList, &tempList );
	}

	StructDestroy( parse_MissionDef, missionAccum );
}

/// Return the zonemap name for an episode part.
const char* genesisEpisodePartMapName( GenesisEpisode* episode, const char* mapDescName )
{
	char buffer[ 256 ];
	sprintf( buffer, "%s_%s", episode->name, mapDescName );
	return allocAddString( buffer );
}

/// Transmogrify a generic objective
void genesisTransmogrifyObjectiveFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionObjective* objective_desc )
{
	char** challengeNames = SAFE_MEMBER( objective_desc, succeedWhen.eaChallengeNames );
	int it;

	if( !objective_desc ) {
		return;
	}

	for( it = 0; it != eaSize( &challengeNames ); ++it ) {
		GenesisMissionChallenge* challenge = genesisFindChallenge( context->map_desc, context->mission_desc, challengeNames[ it ], NULL );
		
		if( !challenge ) {
			genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextObjective( objective_desc->pcName, SAFE_MEMBER(context->mission_desc, zoneDesc.pcName ) ),
							   "Objective references challenge \"%s\", but it does not exist.",
							   challengeNames[ it ]);
			return;
		}
		
		genesisChallengeNameFixup( context, &challengeNames[ it ]);
	}

	if( objective_desc->succeedWhen.pcPromptChallengeName ) {
		genesisChallengeNameFixup( context, &objective_desc->succeedWhen.pcPromptChallengeName );
	}

	for( it = 0; it != eaSize( &objective_desc->eaChildren ); ++it ) {
		genesisTransmogrifyObjectiveFixup( context, objective_desc->eaChildren[ it ]);
	}
}

/// Create a new objective, stash in OUT-START-OBJECTIVE.  Stash the
/// final objective in the chain in OUT-END-OBJECTIVE.
///
/// If there is not a seperate ending objective,
/// OUT-START-OBJECTIVE == OUT-END-OBJECTIVE
void genesisCreateObjective( MissionDef** outStartObjective, MissionDef** outEndObjective, GenesisMissionContext* context, MissionDef* grantingObjective, GenesisMissionObjective* objective_desc )
{
	MissionDef* accum = StructCreate( parse_MissionDef );
	
	accum->name = allocAddString( objective_desc->pcName );
	genesisCreateMessage( context, &accum->uiStringMsg, objective_desc->pcShortText );
	eaCopyStructs( &objective_desc->eaOnStartActions, &accum->ppOnStartActions, parse_WorldGameActionProperties );

	if( !nullStr( objective_desc->pcSuccessFloaterText )) {
		WorldGameActionProperties* successFloater = StructCreate( parse_WorldGameActionProperties );
		eaPush( &accum->ppSuccessActions, successFloater );

		successFloater->eActionType = WorldGameActionType_SendFloaterMsg;
		successFloater->pSendFloaterProperties = StructCreate( parse_WorldSendFloaterActionProperties );
		genesisCreateMessage( context, &successFloater->pSendFloaterProperties->floaterMsg, objective_desc->pcSuccessFloaterText );
		setVec3( successFloater->pSendFloaterProperties->vColor, 0, 0, 0.886275 );  //< Color copied from gvProgressColor
	}

	// Only ItemCount objectives right now can be uncompleted.
	if( objective_desc->succeedWhen.type != GenesisWhen_ItemCount ) {
		accum->doNotUncomplete = true;
	} else {
		accum->doNotUncomplete = false;
	}

	// Optional reward table on success
	accum->params = StructCreate( parse_MissionDefParams );
	accum->params->OnsuccessRewardTableName = REF_STRING_FROM_HANDLE( objective_desc->hReward );

	{
		GenesisRuntimeErrorContext* debugContext = genesisMakeTempErrorContextObjective( objective_desc->pcName, SAFE_MEMBER(context->zone_mission, desc.pcName) );

		// Handle success when
		if( objective_desc->succeedWhen.type == GenesisWhen_AllOf ) {
			genesisAccumObjectivesAllOf( accum, context, grantingObjective, objective_desc->eaChildren );
		} else if( objective_desc->succeedWhen.type == GenesisWhen_InOrder ) {
			genesisAccumObjectivesInOrder( accum, context, grantingObjective, objective_desc->eaChildren );
		} else if( objective_desc->succeedWhen.type == GenesisWhen_Branch ) {
			genesisAccumObjectivesBranch( accum, context, grantingObjective, objective_desc->eaChildren );
		} else {
			bool showCount = false;
			char* expr = NULL;
			genesisWhenMissionExprTextAndEvents(
					&expr, &accum->eaTrackedEvents, &showCount,
					context, &objective_desc->succeedWhen, debugContext, "SucceedWhen" );
			accum->meSuccessCond = StructCreate( parse_MissionEditCond );
			accum->meSuccessCond->type = MissionCondType_Expression;
			accum->meSuccessCond->valStr = StructAllocString( expr );
			accum->meSuccessCond->showCount = showCount ? MDEShowCount_Show_Count : MDEShowCount_Normal;
			estrDestroy( &expr );
		}
		
		// If this completes a on a clickie and it is scoped to this
		// objective, we create optional subobjectives per clickie to
		// scope each one.
		if( objective_desc->succeedWhen.type == GenesisWhen_ExternalChallengeComplete ) {
			int it;
			for( it = 0; it != eaSize( &objective_desc->succeedWhen.eaExternalChallenges ); ++it ) {
				GenesisWhenExternalChallenge* externalChallenge = objective_desc->succeedWhen.eaExternalChallenges[ it ];
				
				if( externalChallenge->eType == GenesisChallenge_Clickie && externalChallenge->pClickie && IS_HANDLE_ACTIVE( externalChallenge->pClickie->hInteractionDef )) {
					MissionDef* challengeGateAccum = StructCreate( parse_MissionDef );
					GenesisWhen when = { 0 };
					char* expr = NULL;

					{
						char buffer[ 256 ];
						sprintf( buffer, "%s_Clickie%d", objective_desc->pcName, it + 1 );
						challengeGateAccum->name = allocAddString( buffer );
					}
					challengeGateAccum->doNotUncomplete = true;
					when.type = GenesisWhen_ExternalChallengeComplete;
					eaPush( &when.eaExternalChallenges, externalChallenge );
					genesisWhenMissionExprTextAndEvents(
							&expr, &challengeGateAccum->eaTrackedEvents, NULL,
							context, &when, debugContext, "SucceedWhen" );
					estrInsert( &expr, 0, "(", 1 );
					estrConcatf( &expr, ") or MissionStateSucceeded(\"%s::%s\")",
								 genesisMissionName( context, true ), accum->name );
					
					challengeGateAccum->meSuccessCond = StructCreate( parse_MissionEditCond );
					challengeGateAccum->meSuccessCond->type = MissionCondType_Expression;
					challengeGateAccum->meSuccessCond->valStr = StructAllocString( expr );
					estrDestroy( &expr );
					eaDestroy( &when.eaExternalChallenges );

					if( objective_desc->bShowWaypoints ) {
						// MJF Aug/16/2012: This is done here, which
						// is outside the normal location in
						// genesisWhenMissionWaypointObjects, because
						// otherwise the waypoint won't go away as you
						// interact with things.
						eaPush( &challengeGateAccum->eaWaypoints, genesisCreateMissionWaypointForExternalChallenge( externalChallenge ));
					}

					eaPush( &context->root_mission_accum->subMissions, challengeGateAccum );

					{
						WorldGameActionProperties* grantGateAccum = StructCreate( parse_WorldGameActionProperties );
						grantGateAccum->eActionType = WorldGameActionType_GrantSubMission;
						grantGateAccum->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
						grantGateAccum->pGrantSubMissionProperties->pcSubMissionName = allocAddString( challengeGateAccum->name );

						eaPush( &accum->ppOnStartActions, grantGateAccum );
					}

					// interactable overrides
					{
						InteractableOverride* interactAccum = StructCreate( parse_InteractableOverride );
						char gateExpr[ 1024 ];
						interactAccum->pcMapName = allocAddString( externalChallenge->pcMapName );
						interactAccum->pcInteractableName = allocAddString( externalChallenge->pcName );
						interactAccum->pPropertyEntry = genesisClickieMakeInteractionEntry( context, debugContext, externalChallenge->pClickie, NULL );

						interactAccum->pPropertyEntry->bOverrideInteract = true;
						sprintf( gateExpr, "MissionStateInProgress(\"%s::%s\")", genesisMissionName( context, true ), challengeGateAccum->name );
						interactAccum->pPropertyEntry->pInteractCond = exprCreateFromString( gateExpr, NULL );
						eaPush( &context->root_mission_accum->ppInteractableOverrides, interactAccum );
					}
				}
			}
		}

		// Handle waypoints
		if( objective_desc->bShowWaypoints ) {
			char** objectNames = NULL;
			MissionWaypoint** waypoints = NULL;

			genesisWhenMissionWaypointObjects( &objectNames, &waypoints, context, &objective_desc->succeedWhen,
											   debugContext, "SucceedWhen" );
			if( objectNames ) {
				char volumeName[ 256 ];
				GenesisMissionExtraVolume* extraVolumeAccum;
				MissionWaypoint* waypointAccum;

				sprintf( volumeName, "%s_%s_Waypoint", SAFE_MEMBER( context->zone_mission, desc.pcName ), objective_desc->pcName );
				extraVolumeAccum = genesisInternExtraVolume( context, volumeName );
				extraVolumeAccum->objects = objectNames;
				objectNames = NULL;

				waypointAccum = StructCreate( parse_MissionWaypoint );
				waypointAccum->type = MissionWaypointType_AreaVolume;
				waypointAccum->name = StructAllocString( volumeName );
				waypointAccum->mapName = allocAddString( zmapInfoGetPublicName( context->zmap_info ));
				eaPush(&accum->eaWaypoints, waypointAccum);
			}

			eaPushEArray( &accum->eaWaypoints, &waypoints );
			eaDestroy( &waypoints );

			// Only personal missions should have map waypoints
			// because the open mission is not active on other maps!
			if(   context->zone_mission->desc.generationType == GenesisMissionGenerationType_PlayerMission
				  && (objective_desc->succeedWhen.type == GenesisWhen_ExternalOpenMissionComplete
					  || objective_desc->succeedWhen.type == GenesisWhen_ExternalMapStart) ) {
				int it;
				for( it = 0; it != eaSize( &objective_desc->succeedWhen.eaExternalMapNames ); ++it ) {
					MissionMap* missionMapAccum = StructCreate( parse_MissionMap );
					WorldVariable* varAccum = StructCreate( parse_WorldVariable );
					missionMapAccum->pchMapName = objective_desc->succeedWhen.eaExternalMapNames[ it ];
						
					eaPush( &missionMapAccum->eaWorldVars, varAccum );
					eaPush( &accum->eaObjectiveMaps, missionMapAccum );
				}
			} else {
				const char** mapNames = NULL;
				int it;
				for( it = 0; it != eaSize( &accum->eaWaypoints ); ++it ) {
					eaPushUnique( &mapNames, allocAddString( accum->eaWaypoints[ it ]->mapName ));
				}
				for( it = 0; it != eaSize( &mapNames ); ++it ) {
					MissionMap* missionMapAccum = StructCreate( parse_MissionMap );
					missionMapAccum->pchMapName = mapNames[ it ];
					eaPush( &accum->eaObjectiveMaps, missionMapAccum );
				}
				eaDestroy( &mapNames );
			}
		}
		eaPushStructs( &accum->eaWaypoints, &objective_desc->eaExtraWaypoints, parse_MissionWaypoint );
	}
	
	// Handle timeout
	accum->uTimeout = objective_desc->uTimeout;
	if( objective_desc->uTimeout ) {
		if( accum->meSuccessCond && accum->meSuccessCond->type == MissionCondType_And ) {
			genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextObjective( objective_desc->pcName, SAFE_MEMBER( context->zone_mission, desc.pcName ) ),
							   "Objective type does not support auto-succeed on timer." );
		} else {
			MissionEditCond* timerCond = StructCreate( parse_MissionEditCond );

			timerCond->type = MissionCondType_Expression;
			timerCond->valStr = StructAllocString( "TimeExpired()" );

			if( !accum->meSuccessCond ) {
				accum->meSuccessCond = timerCond;
			} else {
				MissionEditCond* otherCond = accum->meSuccessCond;
				MissionEditCond* metaCond = StructCreate( parse_MissionEditCond );

				metaCond->type = MissionCondType_Or;
				eaPush( &metaCond->subConds, timerCond );
				eaPush( &metaCond->subConds, otherCond );
				accum->meSuccessCond = metaCond;
			}
		}
	}

	// Return and cleanup
	*outStartObjective = accum;
	eaPush( &context->root_mission_accum->subMissions, accum );

	// If there are any prompts that follow this objective, don't
	// start the next objective until those prompts are done.
	{
		const char** completePromptNames = NULL;
		int it;
		assert(context->zone_mission);
		for( it = 0; it != eaSize( &context->zone_mission->desc.eaPrompts ); ++it ) {
			GenesisMissionPrompt* prompt = context->zone_mission->desc.eaPrompts[ it ];

			if( prompt->showWhen.type == GenesisWhen_ObjectiveComplete ) {
				if( eaFindString( &prompt->showWhen.eaObjectiveNames, objective_desc->pcName ) != -1 ) {
					eaPush( &completePromptNames, prompt->pcName );
				}
			}
		}
		
		if( !eaSize( &completePromptNames )) {
			*outEndObjective = accum;
		} else {
			MissionDef* endAccum = StructCreate( parse_MissionDef );
			char endAccumName[ 256 ];

			sprintf( endAccumName, "AfterPrompt_%s", objective_desc->pcName );
			endAccum->name = allocAddString( endAccumName );
			endAccum->doNotUncomplete = true;
			{
				GenesisWhen whenAfterPrompt = { 0 };
				GenesisRuntimeErrorContext* debugContext = genesisMakeTempErrorContextObjective( objective_desc->pcName, SAFE_MEMBER( context->zone_mission, desc.pcName ) );
				char* expr = NULL;

				whenAfterPrompt.type = GenesisWhen_PromptCompleteAll;
				whenAfterPrompt.eaPromptNames = (char**)completePromptNames;
				genesisWhenMissionExprTextAndEvents(
						&expr, &endAccum->eaTrackedEvents, NULL,
						context, &whenAfterPrompt, debugContext, "SucceedWhen" );
				endAccum->meSuccessCond = StructCreate( parse_MissionEditCond );
				endAccum->meSuccessCond->type = MissionCondType_Expression;
				endAccum->meSuccessCond->valStr = StructAllocString( expr );
				estrDestroy( &expr );
			}

			{
				WorldGameActionProperties* grantEndObjective = StructCreate( parse_WorldGameActionProperties );
				grantEndObjective->eActionType = WorldGameActionType_GrantSubMission;
				grantEndObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
				grantEndObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( endAccumName );
				eaPush( &accum->ppSuccessActions, grantEndObjective );
			}
			
			*outEndObjective = endAccum;
			eaPush( &context->root_mission_accum->subMissions, endAccum );
		}
	}
}

/// Create an optional objective that completes when COMPLETE-EVENT.
///
/// This is useful for FSM-like behavior.
MissionDef* genesisCreateObjectiveOptional( GenesisMissionContext* context, GameEvent** completeEvents, int count, char* debug_name )
{
	MissionDef* accum = StructCreate( parse_MissionDef );
	char buffer[ 1024 ];

	sprintf( buffer, "Optional_%d_%s",
			 eaSize( context->optional_objectives_accum ),
			 debug_name );
	
	accum->name = allocAddString( buffer );
	accum->doNotUncomplete = true;
	accum->meSuccessCond = StructCreate( parse_MissionEditCond );
	accum->meSuccessCond->type = MissionCondType_Expression;
	{
		char* exprAccum = NULL;
		int it;
		for( it = 0; it != eaSize( &completeEvents ); ++it ) {
			GameEvent* missionEvent = StructClone( parse_GameEvent, completeEvents[ it ]);
			
			sprintf( buffer, "Optional_%d", it );
			missionEvent->pchEventName = allocAddString( buffer );
			estrConcatf( &exprAccum, "%sMissionEventCount(\"%s\")",
						 (exprAccum ? " + " : ""),
						 buffer);
			
			eaPush( &accum->eaTrackedEvents, missionEvent );
		}
		estrConcatf( &exprAccum, " >= %d", count );
		
		accum->meSuccessCond->valStr = StructAllocString( exprAccum );
		estrDestroy( &exprAccum );
	}

	eaPush( context->optional_objectives_accum, accum );

	return accum;
}

/// Create an optional objective that completes when EXPR-TEXT is true.
///
/// This is useful for FSM-like behavior.
MissionDef* genesisCreateObjectiveOptionalExpr( GenesisMissionContext* context, char* exprText, char* debug_name )
{
	MissionDef* accum = StructCreate( parse_MissionDef );
	char buffer[ 1024 ];

	sprintf( buffer, "Optional_%d_%s",
			 eaSize( context->optional_objectives_accum ),
			 debug_name );
	
	accum->name = allocAddString( buffer );
	accum->doNotUncomplete = true;
	accum->meSuccessCond = StructCreate( parse_MissionEditCond );
	accum->meSuccessCond->type = MissionCondType_Expression;
	accum->meSuccessCond->valStr = StructAllocString( exprText );

	eaPush( context->optional_objectives_accum, accum );

	return accum;
}

/// Add to ACCUM all the objectives in OBJECTIVE-DESCS.  Each one
/// grants the next one.
void genesisAccumObjectivesInOrder( MissionDef* accum, GenesisMissionContext* context, MissionDef* grantingObjective, GenesisMissionObjective** objective_descs )
{
	const char* completeObjectiveName = NULL;
	int numObjectives = eaSize( &objective_descs );
	if( numObjectives != 0 ) {
		MissionDef** startObjectives = NULL;
		MissionDef** endObjectives = NULL;
		MissionDef** nextObjectives = NULL;
		MissionDef* grantingObjectiveIt = grantingObjective;

		int it;
		for( it = 0; it != numObjectives; ++it ) {
			MissionDef* startObjective;
			MissionDef* endObjective;

			genesisCreateObjective( &startObjective, &endObjective, context, grantingObjectiveIt, objective_descs[ it ]);
			if( !objective_descs[ it ]->bOptional ) {
				grantingObjectiveIt = endObjective;
			}
			eaPush( &startObjectives, startObjective );
			eaPush( &endObjectives, endObjective );
		}
		for( it = numObjectives - 1; it >= 0; --it ) {
			MissionDef* startObjective = startObjectives[ it ];
			MissionDef* endObjective = endObjectives[ it ];

			if( !objective_descs[ it ]->bOptional ) {
				int nextIt;
				for( nextIt = 0; nextIt != eaSize( &nextObjectives ); ++nextIt ) {
					MissionDef* nextObjective = nextObjectives[ nextIt ];
						
					WorldGameActionProperties* grantObjective = StructCreate( parse_WorldGameActionProperties );
					grantObjective->eActionType = WorldGameActionType_GrantSubMission;
					grantObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
					grantObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( nextObjective->name );
					eaPush( &endObjective->ppSuccessActions, grantObjective );
				}

				eaClear( &nextObjectives );

				if( !completeObjectiveName ) {
					completeObjectiveName = endObjective->name;
				}
			}

			eaPush( &nextObjectives, startObjective );
		}

		eaDestroy( &startObjectives );
		eaDestroy( &endObjectives );

		// grant the first ones
		{
			int nextIt;
			for( nextIt = 0; nextIt != eaSize( &nextObjectives ); ++nextIt ) {
				MissionDef* nextObjective = nextObjectives[ nextIt ];
						
				WorldGameActionProperties* grantObjective = StructCreate( parse_WorldGameActionProperties );
				grantObjective->eActionType = WorldGameActionType_GrantSubMission;
				grantObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
				grantObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( nextObjective->name );

				if( accum->uiStringMsg.pEditorCopy ) {
					eaPush( &accum->ppOnStartActions, grantObjective );
				} else if( grantingObjective ) {
					eaPush( &grantingObjective->ppSuccessActions, grantObjective );
				} else {
					eaPush( &context->root_mission_accum->ppOnStartActions, grantObjective );
				}
			}
		}

		eaDestroy( &nextObjectives );
	}

	// can't use the default of "all" objectives, since I will be
	// adding optional sub-missions.
	accum->meSuccessCond = StructCreate( parse_MissionEditCond );
	if( completeObjectiveName ) {
		accum->meSuccessCond->type = MissionCondType_Objective;
		accum->meSuccessCond->valStr = StructAllocString( completeObjectiveName );
	} else {
		accum->meSuccessCond->type = MissionCondType_And;
	}
}

/// Add to ACCUM all the objectives in OBJECTIVE-DESCS. All of them
/// get granted at once
void genesisAccumObjectivesAllOf( MissionDef *accum, GenesisMissionContext* context, MissionDef* grantingObjective, GenesisMissionObjective** objective_descs )
{
	int numObjectives = eaSize( &objective_descs );
	
	if( numObjectives > 0 ) {
		const char** endObjectiveNames = NULL;
		eaSetSize( &endObjectiveNames, numObjectives );
	
		{
			int it;
			for( it = 0; it != numObjectives; ++it ) {
				MissionDef* startObjective;
				MissionDef* endObjective;
				WorldGameActionProperties* grantObjective = StructCreate( parse_WorldGameActionProperties );

				genesisCreateObjective( &startObjective, &endObjective, context, grantingObjective, objective_descs[ it ]);

				grantObjective->eActionType = WorldGameActionType_GrantSubMission;
				grantObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
				grantObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( objective_descs[ it ]->pcName );

				if( accum->uiStringMsg.pEditorCopy ) {
					eaPush( &accum->ppOnStartActions, grantObjective );
				} else if( grantingObjective ) {
					eaPush( &grantingObjective->ppSuccessActions, grantObjective );
				} else {
					eaPush( &context->root_mission_accum->ppOnStartActions, grantObjective );
				}

				endObjectiveNames[ it ] = endObjective->name;
			}
		}

		
		// can't use the default of "all" objectives, since I will be
		// adding optional sub-missions.
		if( accum ) {
			accum->meSuccessCond = StructCreate( parse_MissionEditCond );
			accum->meSuccessCond->type = MissionCondType_And;
			{
				int it;
				for( it = 0; it != numObjectives; ++it ) {
					if( !objective_descs[ it ]->bOptional ) {
						MissionEditCond* objectiveCond = StructCreate( parse_MissionEditCond );
						objectiveCond->type = MissionCondType_Objective;

						assert( it < eaSize( &endObjectiveNames ));
						objectiveCond->valStr = StructAllocString( endObjectiveNames[ it ]);
					
						eaPush( &accum->meSuccessCond->subConds, objectiveCond );
					}
				}
			}
		}

		eaDestroy( &endObjectiveNames );
	}
}

/// Add to ACCUM all the objectives in OBJECTIVE-DESCS. All of them
/// get granted at once, only one needs to be completed for this to
/// succeed.  If ever one of the objectives succeeds, then all the
/// other objectives fail.
void genesisAccumObjectivesBranch( MissionDef *accum, GenesisMissionContext* context, MissionDef* grantingObjective, GenesisMissionObjective** objective_descs )
{
	int numObjectives = eaSize( &objective_descs );

	// MJF TODO: Remove me when infrastructure for branching missions is
	// better.
	//
	// MJF TODO: add per-branch reward tables
	genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextObjective( accum->name, SAFE_MEMBER( context->zone_mission, desc.pcName ) ),
					   "Branching objectives are not yet supported." );
	return;

	/*
	if( numObjectives > 0 ) {
		MissionDef** startObjectives = NULL;
		const char** objectiveAdvanceNames = NULL;
		const char** endObjectiveNames = NULL;
		eaSetSize( &startObjectives, numObjectives );
		eaSetSize( &objectiveAdvanceNames, numObjectives );
		eaSetSize( &endObjectiveNames, numObjectives );
	
		{
			int it;
			for( it = 0; it != numObjectives; ++it ) {
				GenesisMissionObjective* objectiveDesc = objective_descs[ it ];
				
				MissionDef* startObjective;
				MissionDef* endObjective;
				WorldGameActionProperties* grantObjective = StructCreate( parse_WorldGameActionProperties );

				genesisCreateObjective( &startObjective, &endObjective, context, accum, objectiveDesc );

				grantObjective->eActionType = WorldGameActionType_GrantSubMission;
				grantObjective->pGrantSubMissionProperties = StructCreate( parse_WorldGrantSubMissionActionProperties );
				grantObjective->pGrantSubMissionProperties->pcSubMissionName = allocAddString( objective_descs[ it ]->pcName );
				eaPush( &accum->ppOnStartActions, grantObjective );

				startObjectives[ it ] = startObjective;
				if( objectiveDesc->succeedWhen.type == GenesisWhen_InOrder ) {
					if( eaSize( &objectiveDesc->eaChildren ) > 0) {
						if( SAFE_MEMBER( objectiveDesc->eaChildren[ 0 ], pcName )) {
							objectiveAdvanceNames[ it ] = objectiveDesc->eaChildren[ 0 ]->pcName;
						}
					}
				} else {
					genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextObjective( accum->name, SAFE_MEMBER( context->zone_mission, desc.pcName ) ),
									   "Branch objectives can only have InOrder children, one for each branch." );
				}				
				
				endObjectiveNames[ it ] = endObjective->name;
			}
		}

		// Add failures if any of the other objetives advanced
		{
			int it;
			int otherIt;
			for( it = 0; it != numObjectives; ++it ) {
				MissionDef* startObjective = startObjectives[ it ];
				char* exprText = NULL;
				for( otherIt = 0; otherIt != numObjectives; ++otherIt ) {
					if( it == otherIt ) {
						continue;
					}

					estrConcatf( &exprText, "%sMissionStateSucceeded(\"%s::%s\")",
								 (exprText ? " or " : ""),
								 genesisMissionName( context, false ), objectiveAdvanceNames[ otherIt ]);
				}

				genesisAccumFailureExpr( startObjective, exprText );
				estrDestroy( &exprText );
			}
		}

		accum->meSuccessCond = StructCreate( parse_MissionEditCond );
		accum->meSuccessCond->type = MissionCondType_Or;
		{
			int it;
			for( it = 0; it != numObjectives; ++it ) {
				if( !objective_descs[ it ]->bOptional ) {
					MissionEditCond* objectiveCond = StructCreate( parse_MissionEditCond );
					objectiveCond->type = MissionCondType_Objective;

					assert( it < eaSize( &endObjectiveNames ));
					objectiveCond->valStr = StructAllocString( endObjectiveNames[ it ]);
					
					eaPush( &accum->meSuccessCond->subConds, objectiveCond );
				}
			}
		}

		eaDestroy( &startObjectives );
		eaDestroy( &objectiveAdvanceNames );
		eaDestroy( &endObjectiveNames );
	}
	*/
}

/// Add to ACCUM an expression that when true causes the objective to
/// fail.
///
/// If there is already a failure expression, it will get or'd with this one.
void genesisAccumFailureExpr( MissionDef* accum, const char* exprText )
{
	MissionEditCond* newCond = StructCreate( parse_MissionEditCond );
	newCond->type = MissionCondType_Expression;
	newCond->valStr = StructAllocString( exprText );
		
	if( !accum->meFailureCond ) {
		accum->meFailureCond = newCond;
	} else if( accum->meFailureCond->type == MissionCondType_Or ) {
		eaPush( &accum->meFailureCond->subConds, newCond );
	} else {
		MissionEditCond* orCond = StructCreate( parse_MissionEditCond );
		orCond->type = MissionCondType_Or;
		eaPush( &orCond->subConds, accum->meFailureCond );
		eaPush( &orCond->subConds, newCond );
		accum->meFailureCond = orCond;
	} 
}

/// Generate a WorldInteractionPropertyEntry for the clickie
/// properties.
///
/// This is used for creating InteractOverrides as well as
/// requirements.
WorldInteractionPropertyEntry* genesisClickieMakeInteractionEntry( GenesisMissionContext* context, GenesisRuntimeErrorContext* error_context, GenesisMissionChallengeClickie* clickie, GenesisCheckedAttrib* checked_attrib )
{
	WorldInteractionPropertyEntry* interactionEntry = StructCreate( parse_WorldInteractionPropertyEntry );

	assert(clickie);

	interactionEntry->pcInteractionClass = allocAddString( "FROMDEFINITION" );
	COPY_HANDLE( interactionEntry->hInteractionDef, clickie->hInteractionDef );

	if( clickie->pcInteractText || clickie->pcSuccessText || clickie->pcFailureText ) {
		interactionEntry->pTextProperties = StructCreate( parse_WorldTextInteractionProperties );
		if( clickie->pcInteractText ) {
			genesisCreateMessage( context, &interactionEntry->pTextProperties->interactOptionText,
								  clickie->pcInteractText );
		}
		if( clickie->pcSuccessText ) {
			genesisCreateMessage( context, &interactionEntry->pTextProperties->successConsoleText,
								  clickie->pcSuccessText );
		}
		if( clickie->pcFailureText ) {
			genesisCreateMessage( context, &interactionEntry->pTextProperties->failureConsoleText,
								  clickie->pcFailureText );
		}
	}

	if( IS_HANDLE_ACTIVE( clickie->hInteractAnim )) {
		interactionEntry->pAnimationProperties = StructCreate( parse_WorldAnimationInteractionProperties );
		COPY_HANDLE( interactionEntry->pAnimationProperties->hInteractAnim,
					 clickie->hInteractAnim );
	} else {
		interactionEntry->pAnimationProperties = StructCreate( parse_WorldAnimationInteractionProperties );
	}

	if (IS_HANDLE_ACTIVE( clickie->hRewardTable )) {
		interactionEntry->pRewardProperties = StructCreate( parse_WorldRewardInteractionProperties );
		COPY_HANDLE( interactionEntry->pRewardProperties->hRewardTable, clickie->hRewardTable);
	}

	if (clickie->bConsumeSuccessItem)
	{
		if (checked_attrib && checked_attrib->name == allocAddString("PlayerHasItem"))
		{
			WorldGameActionProperties *action = StructCreate(parse_WorldGameActionProperties);

			if (!interactionEntry->pActionProperties)
				interactionEntry->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);

			action->eActionType = WorldGameActionType_TakeItem;
			action->pTakeItemProperties = StructCreate(parse_WorldTakeItemActionProperties);
			action->pTakeItemProperties->iCount = 1;
			SET_HANDLE_FROM_STRING("ItemDef", checked_attrib->astrItemName, action->pTakeItemProperties->hItemDef);

			eaPush(&interactionEntry->pActionProperties->successActions.eaActions, action);
		}
		else
		{
			genesisRaiseError( GENESIS_ERROR, error_context,
							   "Challenge consumes required item on interact success, "
							   "but no required item has been specified." );
		}
	}

	return interactionEntry;
}

/// Fixup all the messages in the mission requirements, replacing the
/// key, description, and scope
void genesisMessageFillKeys( GenesisMissionContext* context )
{
	int messageIt = 0;
	
	if( context->req_accum->params ) {
		genesisParamsMessageFillKeys( context, context->req_accum->params, &messageIt );
	}

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->roomRequirements ); ++it ) {
			if( context->req_accum->roomRequirements[ it ]->params ) {
				genesisParamsMessageFillKeys( context, context->req_accum->roomRequirements[ it ]->params, &messageIt );
			}
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->challengeRequirements ); ++it ) {
			GenesisMissionChallengeRequirements* req = context->req_accum->challengeRequirements[ it ];
			if( req->params ) {
				genesisInstancedParamsMessageFillKeys( context, req->challengeName, req->params, &messageIt );
			}
			if( req->interactParams ) {
				genesisInteractParamsMessageFillKeys( context, req->challengeName, req->interactParams, &messageIt );
			}
			if( req->volumeParams ) {
				genesisParamsMessageFillKeys( context, req->volumeParams, &messageIt );
			}
		}
	}

	{
		char buffer[ MAX_PATH ];
		char zmapDirectory[ MAX_PATH ];
		
		strcpy( zmapDirectory, zmapInfoGetFilename( context->zmap_info ));
		getDirectoryName( zmapDirectory );
		sprintf( buffer, "%s/messages/%s", zmapDirectory, context->zone_mission->desc.pcName );
		context->req_accum->messageFilename = StructAllocString( buffer );
	}
}


/// Fixup all the messages in ACCUM, replacing the key, description,
/// and scope.
void genesisMissionMessageFillKeys( MissionDef * accum, const char* root_mission_name )
{
	char missionName[256];
	char missionNameWithSubmission[256];
	char keyBuffer[256];
	char scopeBuffer[256];
	char nsPrefix[256];
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];

	// Format for keys is Missiondef.<ROOT_MISSION_NAME>[::<SUBMISSION_NAME>].<KEY>
	// Format for scopse is Missiondef/<ROOT_MISSION_NAME>
	if( !root_mission_name )
	{
		if (resExtractNameSpace(accum->name, ns, base))
		{
			strcpy( missionName, base );
			strcpy( missionNameWithSubmission, base );
			sprintf( nsPrefix, "%s:", ns );
		}
		else
		{
			strcpy( missionName, accum->name );
			strcpy( missionNameWithSubmission, accum->name );
			strcpy( nsPrefix, "" );
		}
	}
	else
	{
		if (resExtractNameSpace(root_mission_name, ns, base))
		{
			strcpy( missionName, accum->name );
			sprintf( missionNameWithSubmission, "%s::%s", base, accum->name );
			sprintf( nsPrefix, "%s:", ns );
		}
		else
		{
			sprintf( missionName, "%s", accum->name );
			sprintf( missionNameWithSubmission, "%s::%s", root_mission_name, accum->name );
			strcpy( nsPrefix, "" );
		}
	}
	strchrReplace(missionName, '/', '_');
	strchrReplace(missionName, '\\', '_');
	strchrReplace(missionName, '.', '_');
	strchrReplace(missionNameWithSubmission, '/', '_');
	strchrReplace(missionNameWithSubmission, '\\', '_');
	strchrReplace(missionNameWithSubmission, '.', '_');
	{
		char* resFix = NULL;
		if( resFixName( missionName, &resFix )) {
			strcpy( missionName, resFix );
			estrDestroy( &resFix );
		}
		if( resFixName( missionNameWithSubmission, &resFix )) {
			strcpy( missionNameWithSubmission, resFix );
			estrDestroy( &resFix );
		}
	}

	sprintf( scopeBuffer, "Missiondef/%s", missionName );
	if( accum->displayNameMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%sMissiondef.%s.%s", nsPrefix, missionNameWithSubmission, "Displayname" );
		langFixupMessageWithTerseKey( accum->displayNameMsg.pEditorCopy,
			MKP_MISSIONNAME,
						  keyBuffer,
						  "This is the display name for a MissionDef.",
						  scopeBuffer );

	}
	if( accum->uiStringMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%sMissiondef.%s.%s", nsPrefix, missionNameWithSubmission, "Uistring" );
		langFixupMessageWithTerseKey( accum->uiStringMsg.pEditorCopy,
			MKP_MISSIONUISTR,
						  keyBuffer,
						  "This is the UI String for a MissionDef.",
						  scopeBuffer );
	}
	if( accum->summaryMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%sMissiondef.%s.%s", nsPrefix, missionNameWithSubmission, "Summary" );
		langFixupMessageWithTerseKey( accum->summaryMsg.pEditorCopy,
			MKP_MISSIONSUMMARY,
						  keyBuffer,
						  "This is the Mission Summary string for a MissionDef.",
						  scopeBuffer );
	}
	if( accum->detailStringMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%sMissiondef.%s.%s", nsPrefix, missionNameWithSubmission, "Detailstring" );
		langFixupMessageWithTerseKey( accum->detailStringMsg.pEditorCopy,
			MKP_MISSIONDETAIL,
						  keyBuffer,
						  "This is the detail string for a MissionDef.",
						  scopeBuffer );
	}
	if( accum->msgReturnStringMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%sMissiondef.%s.%s", nsPrefix, missionNameWithSubmission, "Returnstring" );
		langFixupMessageWithTerseKey( accum->msgReturnStringMsg.pEditorCopy,
			MKP_MISSIONRETURN,
						  keyBuffer,
						  "This is a string on a MissionDef that describes how to turn in the Mission.",
						  scopeBuffer );
	}

	{
		int actionIt = 0;		
		int it;
		for( it = 0; it != eaSize( &accum->ppOnStartActions ); ++it, ++actionIt ) {
			if( SAFE_MEMBER( accum->ppOnStartActions[ it ]->pSendFloaterProperties, floaterMsg.pEditorCopy )) {
				sprintf( keyBuffer, "%sMissiondef.%s.Action_%d.Floater", nsPrefix,
						 missionNameWithSubmission, actionIt );
				sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
				langFixupMessage( accum->ppOnStartActions[ it ]->pSendFloaterProperties->floaterMsg.pEditorCopy,
								  keyBuffer,
								  "This is a parameter for a \"SendFloaterMsg\" action that occurs for a MissionDef.",
								  scopeBuffer );
			}
		}
		for( it = 0; it != eaSize( &accum->ppSuccessActions ); ++it, ++actionIt ) {
			if( SAFE_MEMBER( accum->ppSuccessActions[ it ]->pSendFloaterProperties, floaterMsg.pEditorCopy )) {
				sprintf( keyBuffer, "%sMissiondef.%s.Action_%d.Floater", nsPrefix,
						 missionNameWithSubmission, actionIt );
				sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
				langFixupMessage( accum->ppSuccessActions[ it ]->pSendFloaterProperties->floaterMsg.pEditorCopy,
								  keyBuffer,
								  "This is a parameter for a \"SendFloaterMsg\" action that occurs for a MissionDef.",
								  scopeBuffer );
			}
		}
		for( it = 0; it != eaSize( &accum->ppFailureActions ); ++it, ++actionIt ) {
			if( SAFE_MEMBER( accum->ppFailureActions[ it ]->pSendFloaterProperties, floaterMsg.pEditorCopy )) {
				sprintf( keyBuffer, "%sMissiondef.%s.Action_%d.Floater", nsPrefix,
						 missionNameWithSubmission, actionIt );
				sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
				langFixupMessage( accum->ppFailureActions[ it ]->pSendFloaterProperties->floaterMsg.pEditorCopy,
								  keyBuffer,
								  "This is a parameter for a \"SendFloaterMsg\" action that occurs for a MissionDef.",
								  scopeBuffer );
			}
		}
		for( it = 0; it != eaSize( &accum->ppInteractableOverrides ); ++it ) {
			InteractableOverride* override = accum->ppInteractableOverrides[ it ];

			sprintf( keyBuffer, "%sMissiondef.%s.%s",
					 nsPrefix, missionNameWithSubmission,
					 (override->pcMapName ? override->pcMapName : "NO_MAP") );
			sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
			interaction_FixupMessages( accum->ppInteractableOverrides[ it ]->pPropertyEntry, scopeBuffer, keyBuffer, override->pcInteractableName, it );
		}
		for( it = 0; it != eaSize( &accum->ppSpecialDialogOverrides ); ++it ) {
			SpecialDialogOverride* override = accum->ppSpecialDialogOverrides[ it ];
			int blockIt;
			int gameActionIt;
				
			if( !override->pSpecialDialog ) {
				continue;
			}

			sprintf( keyBuffer, "%sMissiondef.%s.SpecialDialog.%d",
					 nsPrefix, missionNameWithSubmission, it );
			sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
			langFixupMessage( override->pSpecialDialog->displayNameMesg.pEditorCopy, keyBuffer, "Description for contact special dialog action.", scopeBuffer );

			for( blockIt = 0; blockIt != eaSize( &override->pSpecialDialog->dialogBlock ); ++blockIt ) {
				sprintf( keyBuffer, "%sMissiondef.%s.SpecialDialog.%d.%d",
						 nsPrefix, missionNameWithSubmission, it, blockIt );
				sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
				langFixupMessage( override->pSpecialDialog->dialogBlock[ blockIt ]->displayTextMesg.pEditorCopy, keyBuffer, "Mission-specific dialog for contact.", scopeBuffer );
				
			}

			for( actionIt = 0; actionIt != eaSize( &override->pSpecialDialog->dialogActions ); ++actionIt ) {
				SpecialDialogAction* dialogAction = override->pSpecialDialog->dialogActions[ actionIt ];

				if( dialogAction->displayNameMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sMissionDef.%s.Specialdialogaction.%d.%d",
							 nsPrefix, missionNameWithSubmission, it, actionIt );
					sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
					langFixupMessage( dialogAction->displayNameMesg.pEditorCopy,
									  keyBuffer,
									  "Mission-specific dialog for contact",
									  scopeBuffer );
				}

				for( gameActionIt = 0; gameActionIt != eaSize( &dialogAction->actionBlock.eaActions ); ++gameActionIt ) {
					WorldGameActionProperties* gameAction = dialogAction->actionBlock.eaActions[ gameActionIt ];
					if( gameAction->pSendNotificationProperties ) {
						sprintf( keyBuffer, "%sMissionDef.%s.Specialdialogname.%d.%d.Action_%d.Notify",
								 nsPrefix, missionNameWithSubmission, it, actionIt, gameActionIt );
						sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
						langFixupMessage( gameAction->pSendNotificationProperties->notifyMsg.pEditorCopy,
										  keyBuffer,
										  "Dummy Message generated by Genesis so the the notification is sent to the client.",
										  scopeBuffer );
					}
				}
			}
		}
		for( it = 0; it != eaSize( &accum->ppImageMenuItemOverrides ); ++it ) {
			ImageMenuItemOverride* override = accum->ppImageMenuItemOverrides[ it ];

			if( !override->pImageMenuItem ) {
				continue;
			}

			sprintf( keyBuffer, "%sMissionDef.%s.ImageMenuItem.%d.Name",
					 nsPrefix, missionNameWithSubmission, it );
			sprintf( scopeBuffer, "Missiondef/%s", missionNameWithSubmission );
			langFixupMessage( override->pImageMenuItem->name.pEditorCopy,
							  keyBuffer,
							  "Dummy Message generated by Genesis",
							  scopeBuffer );
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &accum->subMissions ); ++it ) {
			genesisMissionMessageFillKeys(
					accum->subMissions[ it ],
					root_mission_name ? root_mission_name : accum->name );
		}
	}
}

/// Fixup all the messages in ACCUM, replacing the key, description
/// and scope.
void genesisContactMessageFillKeys( ContactDef* accum )
{
	char keyBuffer[256];
	char nsPrefix[256];
	char contactName[256];
	resExtractNameSpace(accum->name, nsPrefix, contactName);
	if(nsPrefix[0])
		strcat(nsPrefix, ":");

	{
		int dialogIt;
		int actionIt;
		int subDialogIt;
		int gameActionIt;
		for( dialogIt = 0; dialogIt != eaSize( &accum->specialDialog ); ++dialogIt ) {
			SpecialDialogBlock* dialogBlock = accum->specialDialog[ dialogIt ];

			if( dialogBlock->displayNameMesg.pEditorCopy ) {
				sprintf( keyBuffer, "%sContactdef.%s.Specialdialogname.%d",
						 nsPrefix, contactName, dialogIt );
				langFixupMessage( dialogBlock->displayNameMesg.pEditorCopy,
								  keyBuffer,
								  "Mission-specific dialog for contact",
								  "Contactdef" );
			}
			for( subDialogIt = 0; subDialogIt != eaSize( &dialogBlock->dialogBlock); subDialogIt++)
			{
				if( dialogBlock->dialogBlock[subDialogIt]->displayTextMesg.pEditorCopy ) {
					if(subDialogIt == 0) {
						sprintf( keyBuffer, "%sContactdef.%s.Specialdialog.%d",
							nsPrefix, contactName, dialogIt);
					} else {
						sprintf( keyBuffer, "%sContactdef.%s.Specialdialog.%d.%d",
							 nsPrefix, contactName, dialogIt , subDialogIt);
					}
					langFixupMessage( dialogBlock->dialogBlock[subDialogIt]->displayTextMesg.pEditorCopy,
									  keyBuffer,
									  "Mission-specific dialog for contact",
									  "Contactdef" );
				}
			}

			for( actionIt = 0; actionIt != eaSize( &dialogBlock->dialogActions ); ++actionIt ) {
				SpecialDialogAction* dialogAction = dialogBlock->dialogActions[ actionIt ];

				if( dialogAction->displayNameMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sContactdef.%s.Specialdialogaction.%d.%d",
							 nsPrefix, contactName, dialogIt, actionIt );
					langFixupMessage( dialogAction->displayNameMesg.pEditorCopy,
									  keyBuffer,
									  "Mission-specific dialog for contact",
									  "Contactdef" );
				}

				for( gameActionIt = 0; gameActionIt != eaSize( &dialogAction->actionBlock.eaActions ); ++gameActionIt ) {
					WorldGameActionProperties* gameAction = dialogAction->actionBlock.eaActions[ gameActionIt ];
					if( gameAction->pSendNotificationProperties ) {
						sprintf( keyBuffer, "%sContactdef.%s.Specialdialogname.%d.%d.Action_%d.Notify",
								 nsPrefix, contactName, dialogIt, actionIt, gameActionIt );
						langFixupMessage( gameAction->pSendNotificationProperties->notifyMsg.pEditorCopy,
										  keyBuffer,
										  "Dummy Message generated by Genesis so the the notification is sent to the client.",
										  "Contactdef" );
					}
				}
			}
		}
	}
	{
		int offerIt;
		int dialogIt;

		for( offerIt = 0; offerIt != eaSize( &accum->offerList ); ++offerIt ) {
			ContactMissionOffer* offer = accum->offerList[ offerIt ];

			for( dialogIt = 0; dialogIt != eaSize( &offer->offerDialog ); ++dialogIt ) {
				DialogBlock* dialog = offer->offerDialog[ dialogIt ];
				if( dialog->displayTextMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sContactdef.%s.Missionoffer.%d.Offer.%d",
							 nsPrefix, contactName, offerIt, dialogIt );
					langFixupMessage( dialog->displayTextMesg.pEditorCopy,
									  keyBuffer,
									  "Contact's dialog when offering mission to the player",
									  "Contactdef" );
				}
			}
			for( dialogIt = 0; dialogIt != eaSize( &offer->inProgressDialog ); ++dialogIt ) {
				DialogBlock* dialog = offer->inProgressDialog[ dialogIt ];
				if( dialog->displayTextMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sContactdef.%s.Missionoffer.%d.Inprogress.%d",
							 nsPrefix, contactName, offerIt, dialogIt );
					langFixupMessage( dialog->displayTextMesg.pEditorCopy,
									  keyBuffer,
									  "Contact's dialog if player returns while mission is still in progress",
									  "Contactdef" );
				}
			}
			for( dialogIt = 0; dialogIt != eaSize( &offer->completedDialog ); ++dialogIt ) {
				DialogBlock* dialog = offer->completedDialog[ dialogIt ];
				if( dialog->displayTextMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sContactdef.%s.Missionoffer.%d.Completeddialog.%d",
							 nsPrefix, contactName, offerIt, dialogIt );
					langFixupMessage( dialog->displayTextMesg.pEditorCopy,
									  keyBuffer,
									  "Contact's dialog when player returns after completing mission",
									  "Contactdef" );
				}
			}
			for( dialogIt = 0; dialogIt != eaSize( &offer->failureDialog ); ++dialogIt ) {
				DialogBlock* dialog = offer->failureDialog[ dialogIt ];
				if( dialog->displayTextMesg.pEditorCopy ) {
					sprintf( keyBuffer, "%sContactdef.%s.Missionoffer.%d.Failuredialog.%d",
							 nsPrefix, contactName, offerIt, dialogIt );
					langFixupMessage( dialog->displayTextMesg.pEditorCopy,
									  keyBuffer,
									  "Contact's dialog when player returns after failing mission",
									  "Contactdef" );
				}
			}
		}
	}
}

/// Fixup all the messages in PARAMS, replacing the key, description
/// and scope.
void genesisParamsMessageFillKeys( GenesisMissionContext* context, GenesisProceduralObjectParams* params, int* messageIt )
{
	char keyBuffer[4096];
	char zmapName[RESOURCE_NAME_MAX_SIZE];
	const char* zmapPublicname = zmapInfoGetPublicName( context->zmap_info );
	int it;
	char scopeBuffer[256];

	strcpy(zmapName, zmapPublicname);
	strchrReplace(zmapName, '/', '_');
	strchrReplace(zmapName, '\\', '_');
	strchrReplace(zmapName, '.', '_');
			
	if( params->optionalaction_volume_properties ) {
		int entryIt;
		for( entryIt = 0; entryIt != eaSize( &params->optionalaction_volume_properties->entries ); ++entryIt ) {
			WorldOptionalActionVolumeEntry* entry = params->optionalaction_volume_properties->entries[ entryIt ];
			
			if( entry->display_name_msg.pEditorCopy ) {
				sprintf( keyBuffer, "%s.OptionalAction.Autogen_%s.%d", zmapName, context->zone_mission->desc.pcName, (*messageIt)++ );
				langFixupMessage( entry->display_name_msg.pEditorCopy,
								  keyBuffer,
								  "Optional action button text",
								  "OptionalAction" );
			}
		}
	}
	it = 0;
	if (params->interaction_properties)
	{
		FOR_EACH_IN_EARRAY(params->interaction_properties->eaEntries, WorldInteractionPropertyEntry, entry)
		{
			WorldTextInteractionProperties* textProperties = entry->pTextProperties;

			if( SAFE_MEMBER( textProperties, interactOptionText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Interactoptiontext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Interactoptiontext%d", it );
				langFixupMessage( textProperties->interactOptionText.pEditorCopy,
								  keyBuffer,
								  "Interact text for a clickie",
								  scopeBuffer );
			}
			if( SAFE_MEMBER( textProperties, successConsoleText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Successconsoletext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Successconsoletext%d", it );
				langFixupMessage( textProperties->successConsoleText.pEditorCopy,
								  keyBuffer,
								  "Success console text for a clickie",
								  scopeBuffer );
			}
			if( SAFE_MEMBER( textProperties, failureConsoleText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Failureconsoletext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Failureconsoletext%d", it );
				langFixupMessage( textProperties->failureConsoleText.pEditorCopy,
								  keyBuffer,
								  "Failure console text for a clickie",
								  scopeBuffer );
			}
			it++;
		}
		FOR_EACH_END;
	}
}

/// Fixup all the messages in PARAMS, replacing the key, description
/// and scope.
void genesisInteractParamsMessageFillKeys( GenesisMissionContext* context, const char* challengeName, GenesisInteractObjectParams* params, int* messageIt )
{
	char keyBuffer[4096];
	char scopeBuffer[256];
	char zmapName[RESOURCE_NAME_MAX_SIZE];
	const char* zmapPublicname = zmapInfoGetPublicName( context->zmap_info );

	strcpy(zmapName, zmapPublicname);
	strchrReplace(zmapName, '/', '_');
	strchrReplace(zmapName, '\\', '_');
	strchrReplace(zmapName, '.', '_');

	if( params->displayNameMsg.pEditorCopy ) {
		sprintf( keyBuffer, "%s.Displaynamebasic.Autogen_%s", zmapName, context->zone_mission->desc.pcName );
		langFixupMessage( params->displayNameMsg.pEditorCopy,
						  keyBuffer,
						  NULL,
						  "Displaynamebasic" );
	}

	{
		int it;
		for( it = 0; it != eaSize( &params->eaInteractionEntries ); ++it ) {
			WorldInteractionPropertyEntry* entry = params->eaInteractionEntries[ it ];
			WorldTextInteractionProperties* textProperties = entry->pTextProperties;

			if( SAFE_MEMBER( textProperties, interactOptionText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Interactoptiontext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Interactoptiontext%d", it );
				langFixupMessage( textProperties->interactOptionText.pEditorCopy,
								  keyBuffer,
								  "Interact text for a clickie",
								  scopeBuffer );
			}
			if( SAFE_MEMBER( textProperties, successConsoleText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Successconsoletext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Successconsoletext%d", it );
				langFixupMessage( textProperties->successConsoleText.pEditorCopy,
								  keyBuffer,
								  "Success console text for a clickie",
								  scopeBuffer );
			}
			if( SAFE_MEMBER( textProperties, failureConsoleText.pEditorCopy )) {
				sprintf( keyBuffer, "%s.Failureconsoletext%d.Autogen_%s.%d", zmapName, it, context->zone_mission->desc.pcName, (*messageIt)++ );
				sprintf( scopeBuffer, "Failureconsoletext%d", it );
				langFixupMessage( textProperties->failureConsoleText.pEditorCopy,
								  keyBuffer,
								  "Failure console text for a clickie",
								  scopeBuffer );
			}
		}
	}
}

/// Fixup all the messages in PARAMS, replacing the key, description
/// and scope.
void genesisInstancedParamsMessageFillKeys( GenesisMissionContext* context, const char* challengeName, GenesisInstancedObjectParams* params, int* messageIt )
{
	char keyBuffer[4096];
	char scopeBuffer[256];
	char zmapName[RESOURCE_NAME_MAX_SIZE];
	const char* zmapPublicname = zmapInfoGetPublicName( context->zmap_info );

	strcpy(zmapName, zmapPublicname);
	strchrReplace(zmapName, '/', '_');
	strchrReplace(zmapName, '\\', '_');
	strchrReplace(zmapName, '.', '_');
	
	if (params->pContact)
	{
		sprintf( keyBuffer, "%s.Contact_Displayname.Autogen_%s.%d", zmapName, context->zone_mission->desc.pcName, (*messageIt)++ );
		sprintf( scopeBuffer, "Contact_Displayname");
		langFixupMessage( params->pContact->contactName.pEditorCopy,
							keyBuffer,
							"Contact name",
							scopeBuffer );
	}
	if(eaSize(&params->eaChildParams))
	{
		FOR_EACH_IN_EARRAY(params->eaChildParams, GenesisInstancedChildParams, child)
		{
			if(!child)
				continue;

			sprintf( keyBuffer, "%s.Actor_Displayname.Autogen_%s.%d", zmapName, context->zone_mission->desc.pcName, (*messageIt)++ );
			sprintf( scopeBuffer, "Actor Displayname");
			langFixupMessage( child->displayNameMsg.pEditorCopy,
								keyBuffer,
								"Actor Display Name",
								scopeBuffer );
		}
		FOR_EACH_END
	}
}

/// Find the challenge for the challenge named CHALLENGE-NAME.
GenesisMissionZoneChallenge* genesisFindZoneChallenge( GenesisZoneMapData* zmap_data, GenesisZoneMission* zone_mission, const char* challenge_name )
{
	return genesisFindZoneChallengeRaw( zmap_data, zone_mission, NULL, challenge_name );
}

/// Find the challenge for the challenge named CHALLENGE-NAME.
///
/// This function exists for TomY ENCOUNTER_HACK.
GenesisMissionZoneChallenge* genesisFindZoneChallengeRaw( GenesisZoneMapData* zmap_data, GenesisZoneMission* zone_mission, GenesisMissionZoneChallenge** override_challenges, const char* challenge_name )
{
	if( override_challenges ) {
		int it;
		for( it = 0; it != eaSize( &override_challenges ); ++it ) {
			GenesisMissionZoneChallenge* challenge = override_challenges[ it ];
			if( stricmp( challenge->pcName, challenge_name ) == 0 ) {
				return challenge;
			}
		}
	} else {
		int it;
		for( it = 0; it != eaSize( &zone_mission->eaChallenges ); ++it ) {
			GenesisMissionZoneChallenge* challenge = zone_mission->eaChallenges[ it ];
			if( stricmp( challenge->pcName, challenge_name ) == 0 ) {
				return challenge;
			}
		}
		if (zmap_data)
		{
			for( it = 0; it != eaSize( &zmap_data->genesis_shared_challenges ); ++it ) {
				GenesisMissionZoneChallenge* challenge = zmap_data->genesis_shared_challenges[ it ];
				if( stricmp( challenge->pcName, challenge_name ) == 0 ) {
					return challenge;
				}
			}
		}
	}
	
	return NULL;
}

/// Find a mission prompt with the specified name.
GenesisMissionPrompt* genesisTransmogrifyFindPrompt( GenesisTransmogrifyMissionContext* context, char* prompt_name )
{
	if( context->mission_desc ) {
		int it;
		for( it = 0; it != eaSize( &context->mission_desc->zoneDesc.eaPrompts ); ++it ) {
			GenesisMissionPrompt* prompt = context->mission_desc->zoneDesc.eaPrompts[ it ];
			if( stricmp( prompt->pcName, prompt_name ) == 0 ) {
				return prompt;
			}
		}
	}

	return NULL;
}

/// Find a mission prompt with the specified name
GenesisMissionPrompt* genesisFindPrompt( GenesisMissionContext* context, char* prompt_name )
{
	int it;
	for( it = 0; it != eaSize( &context->zone_mission->desc.eaPrompts ); ++it ) {
		GenesisMissionPrompt* prompt = context->zone_mission->desc.eaPrompts[ it ];
		if( stricmp( prompt->pcName, prompt_name ) == 0 ) {
			return prompt;
		}
	}
	for( it = 0; it != eaSize( &context->extra_prompts ); ++it ) {
		GenesisMissionPrompt* prompt = context->extra_prompts[ it ];
		if( stricmp( prompt->pcName, prompt_name ) == 0 ) {
			return prompt;
		}
	}

	return NULL;
}

GenesisMissionPromptBlock* genesisFindPromptBlock( GenesisMissionContext* context, GenesisMissionPrompt* prompt, char* block_name )
{
	if( prompt ) {
		if( nullStr( block_name )) {
			return &prompt->sPrimaryBlock;
		} else {
			int it;
			for( it = 0; it != eaSize( &prompt->namedBlocks ); ++it ) {
				GenesisMissionPromptBlock* block = prompt->namedBlocks[ it ];
				if( stricmp( block->name, block_name ) == 0 ) {
					return block;
				}
			}
		}
	}

	return NULL;
}

static bool genesisPromptBlockHasComplete( GenesisMissionPromptBlock* block )
{
	int it;
	for( it = 0; it != eaSize( &block->eaActions ); ++it ) {
		if( nullStr( block->eaActions[ it ]->pcNextBlockName )) {
			return true;
		}
	}
	if( eaSize( &block->eaActions ) == 0 ) {
		return true;
	}

	return false;
}

char** genesisPromptBlockNames( GenesisMissionContext* context, GenesisMissionPrompt* prompt, bool isComplete )
{
	char** accum = NULL;
	int it;

	if( isComplete ) {
		if( genesisPromptBlockHasComplete( &prompt->sPrimaryBlock )) {
			eaPush( &accum, NULL );
		}
		for( it = 0; it != eaSize( &prompt->namedBlocks ); ++it ) {
			GenesisMissionPromptBlock* block = prompt->namedBlocks[ it ];

			if( block->name && genesisPromptBlockHasComplete( block )) {
				eaPush( &accum, prompt->namedBlocks[ it ]->name );
			}
		}
	} else {
		eaPush( &accum, NULL );
	}
	
	return accum;
}

char* genesisSpecialDialogBlockNameTemp( const char* promptName, const char* blockName )
{
	static char buffer[ 512 ];

	if( blockName ) {
		sprintf( buffer, "%s_%s", promptName, blockName );
	} else {
		sprintf( buffer, "%s", promptName );
	}

	return buffer;
}

static char* StructAllocStringFunc( const char* str ) { return StructAllocString( str ); }

/// Return a cannonical copy of a ZoneChallenge for CHALLENGE.
GenesisMissionZoneChallenge* genesisTransmogrifyChallenge( GenesisTransmogrifyMissionContext* context, GenesisMissionChallenge* challenge )
{
	char fullChallengeName[ 256 ];
	if( context->mission_desc ) {
		sprintf( fullChallengeName, "%s_%s",
				 context->mission_desc->zoneDesc.pcName, challenge->pcName );
	} else {
		sprintf( fullChallengeName, "Shared_%s", challenge->pcName );
	}
	
	{
		GenesisMissionZoneChallenge* zoneChallenge = StructCreate( parse_GenesisMissionZoneChallenge );
		zoneChallenge->pcName = StructAllocString( fullChallengeName );
		zoneChallenge->pcLayoutName = StructAllocString(challenge->pcLayoutName);
		zoneChallenge->eType = challenge->eType;
		zoneChallenge->pSourceContext = genesisMakeErrorContextChallenge( challenge->pcName, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), challenge->pcLayoutName );
		zoneChallenge->bForceNamedObject = challenge->bForceNamedObject;
		
		if( challenge->iNumToSpawn <= 0 ) {
			zoneChallenge->iNumToComplete = challenge->iCount;
		} else {
			zoneChallenge->iNumToComplete = challenge->iNumToSpawn;
		}

		eaCopyStructs(&challenge->eaChildren, &zoneChallenge->eaChildren, parse_GenesisPlacementChildParams);
		zoneChallenge->bChildrenAreGroupDefs = challenge->bChildrenAreGroupDefs;

		eaCopyStructs(&challenge->eaTraps, &zoneChallenge->eaTraps, parse_GenesisMissionTrap);
		zoneChallenge->pcTrapObjective = StructAllocString(challenge->pcTrapObjective);

		StructCopyAll( parse_GenesisWhen, &challenge->spawnWhen, &zoneChallenge->spawnWhen );
		StructCopyAll( parse_GenesisWhen, &challenge->clickieVisibleWhen, &zoneChallenge->clickieVisibleWhen );
		zoneChallenge->succeedCheckedAttrib = StructClone( parse_GenesisCheckedAttrib, challenge->succeedCheckedAttrib );
		{
			int it;
			for( it = eaSize( &zoneChallenge->spawnWhen.eaChallengeNames ) - 1; it >= 0; --it ) {
				char** challengeName = &zoneChallenge->spawnWhen.eaChallengeNames[ it ];
				bool isShared;

				if( !genesisFindChallenge( context->map_desc, context->mission_desc, *challengeName, &isShared )) {
					genesisRaiseError( GENESIS_ERROR, zoneChallenge->pSourceContext,
									   "Challenge references challenge "
									   "%s in spawn when, but can not find it.  (If %s is "
									   "shared, then it can only reference other shared "
									   "challenges.)",
									   *challengeName, challenge->pcName );
					StructFreeString( *challengeName );
					eaRemove( &zoneChallenge->spawnWhen.eaChallengeNames, it );
				} else {
					genesisChallengeNameFixup( context, challengeName );
				}
			}

			if( zoneChallenge->spawnWhen.pcPromptChallengeName ) {
				genesisChallengeNameFixup( context, &zoneChallenge->spawnWhen.pcPromptChallengeName );
			}
		}
		zoneChallenge->pClickie = StructClone( parse_GenesisMissionChallengeClickie, challenge->pClickie );
		zoneChallenge->pContact = StructClone( parse_GenesisContactParams, challenge->pContact );
		zoneChallenge->bIsVolume = (challenge->pVolume != NULL);

		return zoneChallenge;
	}
}

/// Create requirements for each challenge. Does not actually place a
/// challenge since that is handled by the GenerateGeometry step.
void genesisCreateChallenge( GenesisMissionContext* context, GenesisMissionZoneChallenge* challenge )
{
	bool is_spawn_object = (challenge->eType == GenesisChallenge_Encounter2 || challenge->eType == GenesisChallenge_Contact);

	int roomIt;
	for( roomIt = 0; roomIt != eaSize( &challenge->spawnWhen.eaRooms ); ++roomIt ) {
		GenesisProceduralObjectParams* params = genesisInternRoomRequirementParams( context, challenge->spawnWhen.eaRooms[ roomIt ]->layoutName, challenge->spawnWhen.eaRooms[ roomIt ]->roomName );
		genesisProceduralObjectSetEventVolume( params );
	}

	// Generate expressions
	if( challenge->spawnWhen.type != GenesisWhen_MapStart || (is_spawn_object && context->mission_num >= 0) ) {
		if( challenge->eType == GenesisChallenge_Encounter ) {
			// nothing to do -- handled via TomY ENCOUNTER_HACK
			return;
		} else {
			GenesisProceduralEncounterProperties pep = { 0 };

			pep.encounter_name = NULL;
			pep.genesis_mission_name = context->zone_mission->desc.pcName;
			pep.genesis_open_mission = (context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission);
			pep.genesis_mission_num = context->mission_num;
			StructCopyAll(parse_GenesisWhen, &challenge->spawnWhen, &pep.spawn_when);
			{
				int it = 0;
				for( it = 0; it != eaSize( &pep.spawn_when.eaChallengeNames ); ++it ) {
					GenesisMissionZoneChallenge* zoneChallenge = genesisFindZoneChallenge( context->genesis_data, context->zone_mission, challenge->spawnWhen.eaChallengeNames[ it ]);
					if( zoneChallenge ) {
						eaPush( &pep.when_challenges, zoneChallenge );
					}
				}
			}

			if( is_spawn_object ) {
				GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, challenge->pcName );
					
				// This should only happen if there's a duplicate
				// challenge name, but that should have been
				// checked for earlier.
				assert( !params->encounterSpawnCond );
				
				params->encounterSpawnCond = genesisCreateEncounterSpawnCond( context, zmapInfoGetPublicName( context->zmap_info ), &pep);
				params->encounterDespawnCond = genesisCreateEncounterDespawnCond( context, zmapInfoGetPublicName( context->zmap_info ), &pep );
			} else {
				GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );
				assert( !params->interactWhenCond );
				params->interactWhenCond = genesisCreateChallengeSpawnCond( context, zmapInfoGetPublicName( context->zmap_info ), &pep);
			}

			eaDestroy( &pep.when_challenges );
		}
	}
	if( challenge->clickieVisibleWhen.type != GenesisWhen_MapStart || challenge->clickieVisibleWhen.checkedAttrib  ) {
		Expression* expr = NULL;
		GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );

		{
			GenesisProceduralEncounterProperties pep = { 0 };

			pep.encounter_name = NULL;
			pep.genesis_mission_name = context->zone_mission->desc.pcName;
			pep.genesis_open_mission = (context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission);
			pep.genesis_mission_num = context->mission_num;
			StructCopyAll(parse_GenesisWhen, &challenge->clickieVisibleWhen, &pep.spawn_when);
			{
				int it = 0;
				for( it = 0; it != eaSize( &pep.spawn_when.eaChallengeNames ); ++it ) {
					GenesisMissionZoneChallenge* zoneChallenge = genesisFindZoneChallenge( context->genesis_data, context->zone_mission, challenge->spawnWhen.eaChallengeNames[ it ]);
					if( zoneChallenge ) {
						eaPush( &pep.when_challenges, zoneChallenge );
					}
				}
			}

			expr = genesisCreateChallengeSpawnCond( context, zmapInfoGetPublicName( context->zmap_info ), &pep);
			eaDestroy( &pep.when_challenges );
		}

		// This should only happen if there's a duplicate
		// challenge name, but that should have been checked for
		// earlier.
		assert( !params->clickieVisibleWhenCond );
		params->clickieVisibleWhenCond = expr;

		if( challenge->clickieVisibleWhen.checkedAttrib ) {
			params->clickieVisibleWhenCondPerEnt = true;
		}
	}

	if( challenge->succeedCheckedAttrib ) {
		char* exprText = genesisCheckedAttribText( context, challenge->succeedCheckedAttrib, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER2( context, zone_mission, desc.pcName ), NULL ), "SucceedCheckedAttrib", false );

		if( exprText && challenge->eType == GenesisChallenge_Clickie ) {
			GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );
			params->succeedWhenCond = exprCreateFromString( exprText, NULL );
		} else if( exprText && challenge->bIsVolume ) {
			GenesisProceduralObjectParams* params = genesisInternVolumeRequirementParams( context, challenge->pcName );
			genesisProceduralObjectSetEventVolume( params );
			params->event_volume_properties->entered_cond = exprCreateFromString( exprText, NULL );
		} else {
			genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER2( context, zone_mission, desc.pcName ), NULL ),
							   "SucceedSkillCheck is set, but challenge is not a clicky or volume." );
		}

		estrDestroy( &exprText );
	}

	if( challenge->pClickie ) {
		GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );
		genesisCreateMessage( context, &params->displayNameMsg, challenge->pClickie->strVisibleName );
	}

	if( challenge->pClickie && IS_HANDLE_ACTIVE( challenge->pClickie->hInteractionDef )) {
		GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );
		params->bIsUGCDoor = challenge->pClickie->bIsUGCDoor;
		eaPush( &params->eaInteractionEntries, genesisClickieMakeInteractionEntry( context, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER2( context, zone_mission, desc.pcName ), NULL ), challenge->pClickie, challenge->succeedCheckedAttrib ));
	}
	
	if (challenge->pContact)
	{
		GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, challenge->pcName );
		if (!params->pContact)
		{
			params->pContact = StructCreate(parse_GenesisMissionContactRequirements);
		}
		genesisCreateMessage( context, &params->pContact->contactName, challenge->pContact->pcContactName);
		COPY_HANDLE(params->pContact->hCostume, challenge->pContact->hCostume);
	}

	if(eaSize(&challenge->eaChildren))
	{
		GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, challenge->pcName );

		FOR_EACH_IN_EARRAY_FORWARDS(challenge->eaChildren, GenesisPlacementChildParams, child) {
			GenesisInstancedChildParams *instanced = NULL;
			instanced = eaGet(&params->eaChildParams, FOR_EACH_IDX(0, child));
			
			if(!instanced)
				eaSet(&params->eaChildParams, instanced = StructCreate(parse_GenesisInstancedChildParams), FOR_EACH_IDX(0, child));

			if(!nullStr(child->actor_params.pcActorName))
				genesisCreateMessage(context, &instanced->displayNameMsg, child->actor_params.pcActorName);
		} FOR_EACH_END;
		params->bChildParamsAreGroupDefs = challenge->bChildrenAreGroupDefs;
	}

	{
		char *volume_entered_expr = NULL;
		char *clicky_expr = NULL;
		FOR_EACH_IN_EARRAY(challenge->eaTraps, GenesisMissionTrap, trap)
		{
			if (trap->bOnVolumeEntered)
			{
				if (volume_entered_expr)
					estrAppend2(&volume_entered_expr, ";\n");
				estrConcatf(&volume_entered_expr, "ActivatePowerPointToPoint(\"%s\",\"default\",\"Namedpoint:%s_%s\",\"Namedpoint:%s_%s\")", trap->pcPowerName, context->zone_mission->desc.pcName, trap->pcEmitterChallenge, context->zone_mission->desc.pcName, trap->pcTargetChallenge);
			}
			else
			{
				if (clicky_expr)
					estrAppend2(&clicky_expr, ";\n");
				estrConcatf(&clicky_expr, "ActivatePowerPointToPoint(\"%s\",\"default\",\"Namedpoint:%s_%s\",\"Namedpoint:%s_%s\")", trap->pcPowerName, context->zone_mission->desc.pcName, trap->pcEmitterChallenge, context->zone_mission->desc.pcName, trap->pcTargetChallenge);
			}
		}
		FOR_EACH_END;

		if (volume_entered_expr)
		{
			GenesisProceduralObjectParams* params = genesisInternVolumeRequirementParams( context, challenge->pcName );
			genesisProceduralObjectSetActionVolume(params);

			params->action_volume_properties->entered_action = exprCreateFromString(volume_entered_expr, NULL);
			{
				char condition_str[1024];
				if (challenge->pcTrapObjective)
					sprintf(condition_str, "OpenMissionStateInProgress(\"%s::%s\") and ClickableGetVisibleChild(\"%s_TRAP\") = 0", genesisMissionName( context, false ), challenge->pcTrapObjective, challenge->pcName);
				else
					sprintf(condition_str, "ClickableGetVisibleChild(\"%s_TRAP\") = 0", challenge->pcName);
				params->action_volume_properties->entered_action_cond = exprCreateFromString(condition_str, NULL);
			}

			estrDestroy(&volume_entered_expr);
		}
		if (clicky_expr)
		{
			GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge->pcName );
			if (eaSize(&params->eaInteractionEntries) > 0)
			{
				WorldInteractionPropertyEntry *entry = params->eaInteractionEntries[0];
				if (!entry->pActionProperties)
					entry->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
				entry->pActionProperties->pSuccessExpr = exprCreateFromString(clicky_expr, NULL);
			}

			estrDestroy(&clicky_expr);
		}
	}
}

/// Fixup CHALLENGE-NAME for the transmogrify pass.
void genesisChallengeNameFixup( GenesisTransmogrifyMissionContext* context, char** challengeName )
{
	char* oldChallengeName = *challengeName;
	char newChallengeName[ 256 ];
	bool challengeIsShared;
	genesisFindChallenge( context->map_desc, context->mission_desc, *challengeName, &challengeIsShared );
	
	if( challengeIsShared ) {
		sprintf( newChallengeName, "Shared_%s", oldChallengeName );
	} else {
		sprintf( newChallengeName, "%s_%s", context->mission_desc->zoneDesc.pcName, oldChallengeName );
	}

	*challengeName = StructAllocString( newChallengeName );
	StructFreeString( oldChallengeName );
}

/// Create a PORTAL between two rooms.
///
/// If IS-REVERSED is set, then go from end to start, otherwise go
/// from start to end.
void genesisCreatePortal( GenesisMissionContext* context, GenesisMissionPortal* portal, bool isReversed )
{
	const char* missionName = context->zone_mission->desc.pcName;
	const char* startLayout;
	const char* endLayout;
	const char* startRoom;
	const char* endRoomPoint;
	const char* endZmap;
	const char* warpText;
	const char* startClickable = NULL;
	char startClickableBuffer[ 512 ];
	char endRoomPointBuffer[ 512 ];
	bool startVolume = false;

	if( !isReversed ) {
		startLayout = portal->pcStartLayout;
		if( portal->eType == GenesisMissionPortal_BetweenLayouts ) {
			endLayout = portal->pcEndLayout;
		} else {
			endLayout = portal->pcStartLayout;
		}
		endRoomPoint = genesisMissionPortalSpawnTargetName( endRoomPointBuffer, portal, isReversed, missionName, endLayout );
		endZmap = portal->pcEndZmap;
		warpText = portal->pcWarpToEndText;
		startVolume = portal->bStartUseVolume;
		startClickable = portal->pcStartClickable;
		startRoom = portal->pcStartRoom;
	} else {
		if( portal->eType == GenesisMissionPortal_BetweenLayouts ) {
			startLayout = portal->pcEndLayout;
		} else {
			startLayout = portal->pcStartLayout;
		}
		endLayout = portal->pcStartLayout;
		endRoomPoint = genesisMissionPortalSpawnTargetName( endRoomPointBuffer, portal, isReversed, missionName, endLayout );
		endZmap = NULL;
		warpText = portal->pcWarpToStartText;
		startVolume = portal->bEndUseVolume;
		startClickable = portal->pcEndClickable;
		startRoom = portal->pcEndRoom;
	}

	if(portal->eUseType == GenesisMissionPortal_Door) {
		GenesisMissionRoomRequirements *roomReq = genesisInternRoomRequirements( context, startLayout, startRoom );
		GenesisMissionDoorRequirements *newDoor = StructCreate(parse_GenesisMissionDoorRequirements);
		char newDoorNameBuffer[ 512 ];

		if(isReversed && portal->pcEndDoor && portal->pcEndDoor[0])
			strcpy(newDoorNameBuffer, portal->pcEndDoor);
		else if (!isReversed && portal->pcStartDoor && portal->pcStartDoor[0])
			strcpy(newDoorNameBuffer, portal->pcStartDoor);
		else
			sprintf(newDoorNameBuffer, "%s_%s_%s", missionName, portal->pcName, (isReversed ? "End" : "Start"));
		newDoor->doorName = StructAllocString(newDoorNameBuffer);
		eaPush(&roomReq->doors, newDoor);

		sprintf(startClickableBuffer, "DoorCap_%s_%s_%s", newDoor->doorName, startRoom, startLayout);
		startClickable = startClickableBuffer;
	}

	if (startClickable)
	{
		WorldInteractionPropertyEntry* entry;
		if (startVolume)
			entry = genesisCreateInteractableChallengeVolumeRequirement( context, startClickable );
		else
			entry = genesisCreateInteractableChallengeRequirement( context, startClickable );

		entry->pTextProperties = StructCreate(parse_WorldTextInteractionProperties);
		genesisCreateMessage( context, &entry->pTextProperties->interactOptionText, warpText );

		entry->pcInteractionClass = allocAddString( "DOOR" );
		entry->pDoorProperties = StructCreate( parse_WorldDoorInteractionProperties );
		StructReset( parse_WorldVariableDef, &entry->pDoorProperties->doorDest );
		entry->pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
		entry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
		entry->pDoorProperties->doorDest.pSpecificValue = StructCreate( parse_WorldVariable );
		entry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
		entry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap = StructAllocString( endZmap );
		entry->pDoorProperties->doorDest.pSpecificValue->pcStringVal = StructAllocString( endRoomPoint );
		eaCopyStructs( &portal->eaEndVariables, &entry->pDoorProperties->eaVariableDefs, parse_WorldVariableDef );

//		COPY_HANDLE( entry->pDoorProperties->hTransSequence, startDesc->hExitTransitionOverride );
	}
	else
	{
		GenesisProceduralObjectParams* params = genesisInternRoomRequirementParams( context, startLayout, startRoom );
		WorldOptionalActionVolumeEntry* entry = StructCreate( parse_WorldOptionalActionVolumeEntry );
		WorldGameActionProperties* actionProps = StructCreate( parse_WorldGameActionProperties );
		GenesisRuntimeErrorContext* debugContext = genesisMakeTempErrorContextPortal( portal->pcName, SAFE_MEMBER( context->zone_mission, desc.pcName ), portal->pcStartLayout );
		genesisProceduralObjectSetOptionalActionVolume( params );

		actionProps->eActionType = WorldGameActionType_Warp;
		actionProps->pWarpProperties = StructCreate( parse_WorldWarpActionProperties );
		eaPush( &entry->actions.eaActions, actionProps );


		{
			char* whenText = genesisWhenExprText( context, &portal->when, debugContext, "When", false );
			if( whenText ) {
				entry->visible_cond = exprCreateFromString( whenText, NULL );
			}
		}
				
		genesisCreateMessage( context, &entry->display_name_msg, warpText );
		entry->category_name = allocAddString( "Warp" );
				
		actionProps->pWarpProperties->warpDest.eType = WVAR_MAP_POINT;
		actionProps->pWarpProperties->warpDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
		actionProps->pWarpProperties->warpDest.pSpecificValue = StructCreate( parse_WorldVariable );
		actionProps->pWarpProperties->warpDest.pSpecificValue->eType = WVAR_MAP_POINT;
		actionProps->pWarpProperties->warpDest.pSpecificValue->pcZoneMap = StructAllocString( endZmap );
		actionProps->pWarpProperties->warpDest.pSpecificValue->pcStringVal = StructAllocString( endRoomPoint );
		eaCopyStructs( &portal->eaEndVariables, &actionProps->pWarpProperties->eaVariableDefs, parse_WorldVariableDef );

		eaPush( &params->optionalaction_volume_properties->entries, entry );
	}
}

/// Create a global expression that corresponds to WHEN.
///
/// Don't call this if you are going to put the expression on a
/// mission.  For that you need to call
/// genesisWhenMissionExprTextAndEvents()
char* genesisWhenExprText( GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isEncounter )
{
	return genesisWhenExprTextRaw( context, NULL, -1, NULL, NULL, when, debugContext, debugFieldName, isEncounter );
}

#endif

/// Create a global expression that corresponds to WHEN.
///
/// This exists to support TomY ENCOUNTER_HACK.  The EncounterHacks
/// can pass in via OVERRIDE-* the data usually stored in CONTEXT.
char* genesisWhenExprTextRaw( GenesisMissionContext* context, const char* overrideZmapName, GenesisMissionGenerationType overrideGenerationType, const char* overrideMissionName, GenesisMissionZoneChallenge** overrideChallenges,
							  GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isEncounter )
{
#ifdef NO_EDITORS
	// This should *never* actually be called by Xbox, because it should be loading from bins.
	return NULL;
#else	
	char* accum = NULL;
	const char* missionName;
	const char* playerMissionName;
	const char* zmapName;
	const char* shortMissionName;
	GenesisMissionGenerationType generationType;
	
	// If OVERRIDE-GENERATION-TYPE is set, that will override the type
	// in CONTEXT.
	if( overrideGenerationType != -1 ) {
		generationType = overrideGenerationType;
	} else {
		assert( context );
		generationType = context->zone_mission->desc.generationType;
	}

	// If OVERRIDE-ZMAP-NAME is set, that will override the name in
	// CONTEXT.
	if( overrideZmapName ) {
		zmapName = overrideZmapName;
	} else {
		assert( context );
		zmapName = context->zmap_info ? zmapInfoGetPublicName( context->zmap_info ) : NULL;
	}
	
	// If OVERRIDE-MISSION-NAME is set, that will override the mission
	// name in CONTEXT.
	if( overrideMissionName ) {
		shortMissionName = overrideMissionName;
		missionName = genesisMissionNameRaw( zmapName, overrideMissionName, (generationType != GenesisMissionGenerationType_PlayerMission) );
		playerMissionName = genesisMissionNameRaw( zmapName, overrideMissionName, false );
	} else {
		assert( context );
		shortMissionName = context->zone_mission->desc.pcName;
		missionName = genesisMissionName( context, false );
		playerMissionName = genesisMissionName( context, true );
	}

	/// Okay, all overrides are finished

	switch( when->type ) {
		case GenesisWhen_MapStart:
			// nothing to do -- the default expr should work
			
		xcase GenesisWhen_Manual: case GenesisWhen_ExternalRewardBoxLooted: case GenesisWhen_RewardBoxLooted:
			estrConcatStatic( &accum, "0" );
			
		xcase GenesisWhen_MissionComplete:
			if( generationType == GenesisMissionGenerationType_PlayerMission ) {
				if( isEncounter ) {
					estrConcatf( &accum, "EntCount(GetNearbyPlayersForEnc().EntCropMissionStateSucceeded(\"%s\")) or EntCount(GetNearbyPlayersForEnc().EntCropHasCompletedMission(\"%s\"))",
								 playerMissionName, playerMissionName );
				} else {
					estrConcatf( &accum, "MissionStateSucceeded(\"%s\") or HasCompletedMission(\"%s\")",
								 playerMissionName, playerMissionName );
				}
			} else {
				estrConcatf( &accum, "OpenMissionStateSucceeded(\"%s\")", missionName );
		   }
			
		xcase GenesisWhen_MissionNotInProgress:
			if( generationType == GenesisMissionGenerationType_OpenMission_NoPlayerMission ) {
				genesisRaiseError( GENESIS_ERROR, debugContext,
								   "%s is PlayerMissionNotInProgress, but there is no player mission.",
								   debugFieldName );
				estrConcatStatic( &accum, "0" );
			} else {
				if( isEncounter ) {
					estrConcatf( &accum, "not (EntCount(GetNearbyPlayersForEnc().EntCropMissionStateInProgress(\"%s\")) or EntCount(GetNearbyPlayersForEnc().EntCropMissionStateSucceeded(\"%s\")) or EntCount(GetNearbyPlayersForEnc().EntCropHasCompletedMission(\"%s\")))",
								 playerMissionName, playerMissionName, playerMissionName );
				} else {
					estrConcatf( &accum, "not (MissionStateInProgress(\"%s\") or MissionStateSucceeded(\"%s\") or HasCompletedMission(\"%s\"))",
								 playerMissionName, playerMissionName, playerMissionName );
				}
			}
			
		xcase GenesisWhen_ObjectiveComplete: case GenesisWhen_ObjectiveCompleteAll: {
			if( eaSize( &when->eaObjectiveNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ObjectiveComplete, but references no objectives.",
								   debugFieldName );
			} else {
				const char* conjunction;
				int it;

				if( when->type == GenesisWhen_ObjectiveComplete ) {
					conjunction = " or ";
				} else {
					conjunction = " and ";
				}
				for( it = 0; it != eaSize( &when->eaObjectiveNames ); ++it ) {
					if( generationType == GenesisMissionGenerationType_PlayerMission ) {
						if( isEncounter ) {
							estrConcatf( &accum, "%sEntCount(GetNearbyPlayersForEnc().EntCropMissionStateSucceeded(\"%s::%s\"))",
										 (it != 0) ? conjunction : "",
										 missionName, when->eaObjectiveNames[ it ]);
						} else {
							estrConcatf( &accum, "%sMissionStateSucceeded(\"%s::%s\")",
										 (it != 0) ? conjunction : "",
										 missionName, when->eaObjectiveNames[ it ]);
						}
					} else {
						estrConcatf( &accum, "%sOpenMissionStateSucceeded(\"%s::%s\")",
									 (it != 0) ? conjunction : "",
									 missionName, when->eaObjectiveNames[ it ]);
					}
				}
			}
		}

		xcase GenesisWhen_ObjectiveInProgress: {
			if( eaSize( &when->eaObjectiveNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ObjectiveInProgress, but references no objectives.",
								   debugFieldName );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaObjectiveNames ); ++it ) {
					if( generationType == GenesisMissionGenerationType_PlayerMission ) {
						if( isEncounter ) {
							estrConcatf( &accum, "%sEntCount(GetNearbyPlayersForEnc().EntCropMissionStateInProgress(\"%s::%s\"))",
										 (it != 0) ? " or " : "",
										 missionName, when->eaObjectiveNames[ it ]);
						} else {
							estrConcatf( &accum, "%sMissionStateInProgress(\"%s::%s\")",
										 (it != 0 ) ? " or " : "",
										 missionName, when->eaObjectiveNames[ it ]);
						}
					} else {
						estrConcatf( &accum, "%sOpenMissionStateInProgress(\"%s::%s\")",
									 (it != 0) ? " or " : "",
									 missionName, when->eaObjectiveNames[ it ]);
					}
				}
			}
		}
			
		xcase GenesisWhen_PromptStart: case GenesisWhen_PromptComplete: case GenesisWhen_PromptCompleteAll: {
			bool isComplete = (when->type != GenesisWhen_PromptStart);
			if( eaSize( &when->eaPromptNames ) == 0 && eaSize( &when->eaPromptBlocks ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type PromptComplete, but references no prompts.",
								   debugFieldName );
			} else {
				const char* conjunction;
				int it;
				int blockIt;

				if( when->type == GenesisWhen_PromptComplete ) {
					conjunction = " or ";
				} else {
					conjunction = " and ";
				}
				
				for( it = 0; it != eaSize( &when->eaPromptNames ); ++it ) {
					GenesisMissionPrompt* prompt = genesisFindPrompt( context, when->eaPromptNames[ it ]);

					if( !prompt ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references prompt \"%s\", but it does not exist.",
										   debugFieldName, when->eaPromptNames[ it ]);
					} else {
						char** completeBlockNames = genesisPromptBlockNames( context, prompt, isComplete );
						char* promptMapChallenge;

						if( prompt->pcChallengeName && !eaSize( &prompt->eaExternalMapNames )) {
							promptMapChallenge = prompt->pcChallengeName;
						} else {
							promptMapChallenge = NULL;
						}
					
						if( eaSize( &completeBlockNames )) {
							estrConcatf( &accum, "%s(", (estrLength( &accum ) ? conjunction : "") );
						}
						for( blockIt = 0; blockIt != eaSize( &completeBlockNames ); ++blockIt ) {
					
							if( isEncounter ) {
								estrConcatf( &accum, "%sEventCount(\"",
											 (blockIt != 0 ? " or " : "") );
								genesisWriteText( &accum, genesisPromptEvent( when->eaPromptNames[ it ], completeBlockNames[ blockIt ], isComplete,
																			  shortMissionName, zmapName, promptMapChallenge ), true );
								estrConcatStatic( &accum, "\")" );
							} else {
								estrConcatf( &accum, "%sHasRecentlyCompletedContactDialog(\"%s\", \"%s\")",
											 (blockIt != 0 ? " or " : ""),
											 genesisContactNameRaw( zmapName, shortMissionName, promptMapChallenge ),
											 genesisSpecialDialogBlockNameTemp( when->eaPromptNames[ it ], completeBlockNames[ blockIt ]));
							}
						}
						if( eaSize( &completeBlockNames )) {
							estrConcatf( &accum, ")" );
						}
					
						eaDestroy( &completeBlockNames );
					}
				}
				for( it = 0; it != eaSize( &when->eaPromptBlocks ); ++it ) {
					GenesisWhenPromptBlock* whenPB = when->eaPromptBlocks[ it ];
					GenesisMissionPrompt* prompt = genesisFindPrompt( context, whenPB->promptName );
					GenesisMissionPromptBlock* block = genesisFindPromptBlock( context, prompt, whenPB->blockName );

					if( !prompt || !block ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references prompt \"%s\", block \"%s\", but it does not exist.",
										   debugFieldName, whenPB->promptName, whenPB->blockName );
					} else {
						char* promptMapChallenge;

						if( prompt->pcChallengeName && !eaSize( &prompt->eaExternalMapNames )) {
							promptMapChallenge = prompt->pcChallengeName;
						} else {
							promptMapChallenge = NULL;
						}
						
						if( isEncounter ) {
							estrConcatf( &accum, "%sEventCount(\"",
										 (estrLength( &accum ) ? conjunction : "") );
							genesisWriteText( &accum, genesisPromptEvent( whenPB->promptName, whenPB->blockName, isComplete,
																		  shortMissionName, zmapName, promptMapChallenge ), true );
							estrConcatf( &accum, "\")" );
						} else {
							estrConcatf( &accum, "%sHasRecentlyCompletedContactDialog(\"%s\", \"%s\")",
										 (it != 0 ? " or " : ""),
										 genesisContactNameRaw( zmapName, shortMissionName, promptMapChallenge ),
										 genesisSpecialDialogBlockNameTemp( whenPB->promptName, whenPB->blockName ));
						}
					}
				}
			}
		}

		xcase GenesisWhen_ExternalPromptComplete: {
			if( eaSize( &when->eaExternalPrompts ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ExternalPromptComplete, but references no prompts.",
								   debugFieldName );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaExternalPrompts ); ++it ) {
					if( isEncounter ) {
						estrConcatf( &accum, "%sEventCount(\"",
									 (it != 0 ? " or " : "") );
						genesisWriteText( &accum, genesisExternalPromptEvent( when->eaExternalPrompts[ it ]->pcPromptName, when->eaExternalPrompts[ it ]->pcContactName, true ), true );
						estrConcatStatic( &accum, "\")" );
					} else {
						estrConcatf( &accum, "%sHasRecentlyCompletedContactDialog(\"%s\", \"%s\")",
									 (it != 0 ? " or " : ""),
									 when->eaExternalPrompts[ it ]->pcContactName,
									 when->eaExternalPrompts[ it ]->pcPromptName );
					}
				}
			}
		}
			
		xcase GenesisWhen_ContactComplete: {
			if( eaSize( &when->eaContactNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ContactComplete, but references no contacts.",
								   debugFieldName );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaContactNames ); ++it ) {
					if( isEncounter ) {
						estrConcatf( &accum, "%sEventCount(\"",
									 (it != 0) ? " or " : "" );
						genesisWriteText( &accum, genesisTalkToContactEvent( when->eaContactNames[ it ]), true );
						estrConcatStatic( &accum, "\")" );
					} else {
						estrConcatf( &accum, "%sHasRecentlyCompletedContactDialog(\"%s\", \"%s\")",
									 (it != 0 ? " or " : ""),
									 when->eaContactNames[ it ], "" );
					}
				}
			}
		}
			
		xcase GenesisWhen_ChallengeComplete: {
			if( eaSize( &when->eaChallengeNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ChallengeComplete, but references no challenges.",
								   debugFieldName );
			} else {
				GameEvent** challengeCompletes = NULL;
				int challengeCount = 0;
				int it;
				for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
					GenesisMissionZoneChallenge* challenge = genesisFindZoneChallengeRaw( SAFE_MEMBER( context, genesis_data ), SAFE_MEMBER( context, zone_mission ), overrideChallenges, when->eaChallengeNames[ it ]);
					if( !challenge ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references challenge \"%s\", but it does not exist.",
										   debugFieldName, when->eaChallengeNames[ it ]);
					} else if( challenge->eType == GenesisChallenge_None ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references challenge \"%s\" with no type.",
										   debugFieldName, when->eaChallengeNames[ it ]);
					} else {
						eaPush( &challengeCompletes, genesisCompleteChallengeEvent( challenge->eType, challenge->pcName, true, zmapName ));
						challengeCount += challenge->iNumToComplete;
					}
				}

				for( it = 0; it != eaSize( &challengeCompletes ); ++it ) {
					estrConcatf( &accum, "%sEventCount(\"",
								 (it != 0) ? " + " : "" );
					genesisWriteText( &accum, challengeCompletes[ it ], true );
					estrConcatf( &accum, "\")" );
				}
				estrConcatf( &accum, " >= %d",
							 (when->iChallengeNumToComplete ? when->iChallengeNumToComplete : challengeCount) );
				eaDestroy( &challengeCompletes );
			}
		}

		xcase GenesisWhen_ExternalChallengeComplete: {
			if( eaSize( &when->eaExternalChallenges ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ExternalChallengeComplete, but references no external challenges.",
								   debugFieldName );
			} else {
				GameEvent** challengeCompletes = NULL;
				int challengeCount = 0;
				int it;
				for( it = 0; it != eaSize( &when->eaExternalChallenges ); ++it ) {
					GenesisWhenExternalChallenge* challenge = when->eaExternalChallenges[ it ];
					GameEvent* event = genesisCompleteChallengeEvent( challenge->eType, challenge->pcName, false, challenge->pcMapName );
					event->tMatchSourceTeam = TriState_Yes;
					eaPush( &challengeCompletes, event );
					challengeCount += 1;
				}

				for( it = 0; it != eaSize( &challengeCompletes ); ++it ) {
					estrConcatf( &accum, "%sEventCount(\"",
								 (it != 0) ? " + " : "" );
					genesisWriteText( &accum, challengeCompletes[ it ], true );
					estrConcatf( &accum, "\")" );
				}
				estrConcatf( &accum, " >= %d", challengeCount );
				eaDestroy( &challengeCompletes );
			}
		}
			
		xcase GenesisWhen_ChallengeAdvance: {
			if( !isEncounter ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "ChallengeAdvance is only allowed on encounters." );
			} else if( eaSize( &when->eaChallengeNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ChallengeAdvance, but references no challenges.",
								   debugFieldName );
			} else {
				GameEvent** challengeCompletes = NULL;
				int it;
				for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
					GenesisMissionZoneChallenge* challenge = genesisFindZoneChallengeRaw( SAFE_MEMBER( context, genesis_data ), SAFE_MEMBER( context, zone_mission ), overrideChallenges, when->eaChallengeNames[ it ]);
					if( !challenge ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references challenge \"%s\", but it does not exist.",
										   debugFieldName, when->eaChallengeNames[ it ]);
					} else {
						eaPush( &challengeCompletes, genesisCompleteChallengeEvent( challenge->eType, challenge->pcName, true, zmapName ));
					}
				}

				for( it = 0; it != eaSize( &challengeCompletes ); ++it ) {
					estrConcatf( &accum, "%sEventCountSinceSpawn(\"",
								 (it != 0) ? " or " : "" );
					genesisWriteText( &accum, challengeCompletes[ it ], true );
					estrConcatf( &accum, "\")" );
				}
			}
		}
			
		xcase GenesisWhen_RoomEntry: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				GenesisProceduralObjectParams* params;

				if( context ) {
					params = genesisInternRoomRequirementParams( context, when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName );
				} else {
					params = NULL;
				}

				estrConcatf( &accum, "%sEventCount(\"",
							 (it != 0) ? " or " : "" );
				genesisWriteText( &accum, genesisReachLocationEvent( when->eaRooms[ it ]->layoutName,
																	 when->eaRooms[ it ]->roomName,
																	 shortMissionName, zmapName ),
								  true );
				estrConcatStatic( &accum, "\")" );

				if( context ) {
					genesisProceduralObjectSetEventVolume( params );
				}
			}

			if( when->type == GenesisWhen_RoomEntryAll ) {
				estrConcatf( &accum, " >= %d", eaSize( &when->eaRooms ));
			}
		}

		xcase GenesisWhen_ExternalRoomEntry: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				estrConcatf( &accum, "%sEventCount(\"",
							 (it != 0) ? " or " : "" );
				genesisWriteText( &accum, genesisReachLocationEventRaw( when->eaExternalRooms[ it ]->pcMapName, when->eaExternalRooms[ it ]->pcName ),
								  true );
				estrConcatStatic( &accum, "\")" );
			}
		}

		xcase GenesisWhen_RoomEntryAll: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				GenesisProceduralObjectParams* params;

				if( context ) {
					params = genesisInternRoomRequirementParams( context, when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName );
				} else {
					params = NULL;
				}

				estrConcatf( &accum, "%s(EventCount(\"",
							 (it != 0) ? " + " : "" );
				genesisWriteText( &accum, genesisReachLocationEvent( when->eaRooms[ it ]->layoutName,
																	 when->eaRooms[ it ]->roomName,
																	 shortMissionName, zmapName ),
								  true );
				estrConcatf( &accum, "\") > 0)" );

				if( context ) {
					genesisProceduralObjectSetEventVolume( params );
				}
			}

			estrConcatf( &accum, " >= %d", eaSize( &when->eaRooms ));
		}

		xcase GenesisWhen_CritterKill: {
			if( eaSize( &when->eaCritterDefNames ) + eaSize( &when->eaCritterGroupNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is CritterKill, but no critters are specified.",
								   debugFieldName );
				estrConcatStatic( &accum, "0" );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaCritterDefNames ); ++it ) {
					estrConcatf( &accum, "%sEventCount(\"",
								 (estrLength( &accum ) ? " + " : "") );
					genesisWriteText( &accum, genesisKillCritterEvent( when->eaCritterDefNames[ it ], zmapName ), true );
					estrConcatStatic( &accum, "\")" );
				}
				for( it = 0; it != eaSize( &when->eaCritterGroupNames ); ++it ) {
					estrConcatf( &accum, "%sEventCount(\"",
								 (estrLength( &accum ) ? " + " : "") );
					genesisWriteText( &accum, genesisKillCritterGroupEvent( when->eaCritterGroupNames[ it ], zmapName ), true );
					estrConcatStatic( &accum, "\")" );
				}

				estrConcatf( &accum, " >= %d", MAX( 1, when->iCritterNumToComplete ));
			}
		}

		xcase GenesisWhen_ItemCount:
			if( eaSize( &when->eaItemDefNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ItemCount, but no items are specified.",
								   debugFieldName );
				estrConcatStatic( &accum, "0" );
			} else {
				if( isEncounter ) {
					estrConcatf( &accum, "EntCount(GetNearbyPlayersForEnc().EntCropExpr({" );
				}
				
				{
					int it;
					for( it = 0; it != eaSize( &when->eaItemDefNames ); ++it ) {
						estrConcatf( &accum, "%sPlayerItemCount(\"%s\")",
									 (it != 0 ? " + " : ""),
									 when->eaItemDefNames[ it ]);
					}
					estrConcatf( &accum, " >= %d", when->iItemCount );
				}

				if( isEncounter ) {
					estrConcatf( &accum, "}))" );
				}
			}

		xcase GenesisWhen_ExternalOpenMissionComplete:
			if( eaSize( &when->eaExternalMissionNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ExternalOpenMissionComplete, but no missions are specified.",
								   debugFieldName );
				estrConcatStatic( &accum, "0" );
			} else {
				if( isEncounter ) {
					estrConcatf( &accum, "EntCount(GetNearbyPlayersForEnc().EntCropExpr({" );
				}

				{
					int it;
					for( it = 0; it != eaSize( &when->eaExternalMissionNames ); ++it ) {
						estrConcatf( &accum, "%sOpenMissionMapCredit(\"%s\")",
									 (it != 0) ? " or " : "",
									 when->eaExternalMissionNames[ it ]);
					}
				}

				if( isEncounter ) {
					estrConcatf( &accum, "}))" );
				}
			}

		xcase GenesisWhen_ExternalMapStart:
			if(when->bAnyCrypticMap)
				estrConcatf( &accum, "NOT IsOnUGCMap()");
			else
			{
				if( eaSize( &when->eaExternalMapNames ) == 0 ) {
					genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ExternalMapStart, but no map is specified",
									   debugFieldName );
					estrConcatStatic( &accum, "0" );
				} else {
					int it;
					for( it = 0; it != eaSize( &when->eaExternalMapNames ); ++it ) {
						estrConcatf( &accum, "%sIsOnMapNamed(%s)",
										(it != 0) ? " or " : "",
										when->eaExternalMapNames[ it ]);
					}
				}
			}

		xcase GenesisWhen_ReachChallenge: {
			int it;
			for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
				if (context) {
					GenesisProceduralObjectParams *params = genesisInternVolumeRequirementParams( context, when->eaChallengeNames[it]);
					genesisProceduralObjectSetEventVolume(params);
				}

				estrConcatf( &accum, "%sEventCount(\"",
							 (it != 0) ? " or " : "" );
				genesisWriteText( &accum, genesisReachLocationEvent( NULL, when->eaChallengeNames[ it ],
																	 shortMissionName, zmapName ),
								  true );
				estrConcatStatic( &accum, "\")" );
			}
		}


		xdefault: {
			char buffer[256];
			genesisErrorPrintContextStr(debugContext, SAFESTR(buffer));
			genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "%s's %s uses unimplemented When type %s",
										   buffer, debugFieldName, StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ));
		}
	}
	
	if( when->checkedAttrib ) {
		char* exprText = genesisCheckedAttribText( context, when->checkedAttrib, debugContext, debugFieldName, false );

		if( exprText && estrLength( &accum )) {
			estrInsertf( &accum, 0, "(" );
			estrConcatf( &accum, ") and %s", exprText );
		} else if( exprText ) {
			estrConcatf( &accum, "%s", exprText );
		}

		estrDestroy( &exprText );
	}

	if( when->bNot ) {
		estrInsertf( &accum, 0, "not (" );
		estrConcatf( &accum, ")" );
	}

	return accum;
#endif
}

#ifndef NO_EDITORS

/// Create an expression and events suitable for missions that
/// corresponds to WHEN.
void genesisWhenMissionExprTextAndEvents( char** outEstr, GameEvent*** outEvents, bool* outShowCount, GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName )
{
	const char* missionName = genesisMissionName( context, false );
	const char* playerMissionName = genesisMissionName( context, true );
	const char* zmapName = context->zmap_info ? zmapInfoGetPublicName( context->zmap_info ) : NULL;
	const char* shortMissionName = context->zone_mission->desc.pcName;
	GenesisMissionGenerationType generationType = context->zone_mission->desc.generationType;

	bool isForUGC = (context && context->is_ugc);
	bool dummyOutShowCount;

	// outShowCount can be NULL, which means we just want to throw it away
	if( !outShowCount ) {
		outShowCount = &dummyOutShowCount;
	}

	estrClear( outEstr );
	eaClearStruct( outEvents, parse_GameEvent );
	
	switch( when->type ) {
		// Conditions that shouldn't work for missions.
		case GenesisWhen_MissionComplete:
		case GenesisWhen_MissionNotInProgress: case GenesisWhen_ChallengeAdvance:
			genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not use %s for %s.",
							   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ), debugFieldName );

		xcase GenesisWhen_MapStart: 
			if( !isForUGC ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not use %s for %s.",
								   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ), debugFieldName );
			} else {
				estrConcatf( outEstr, "1" );
			}
			
		xcase GenesisWhen_Manual: case GenesisWhen_ExternalRewardBoxLooted: case GenesisWhen_RewardBoxLooted:
			if( !isForUGC ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not use %s for %s.",
								   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ), debugFieldName );
			} else {
				estrConcatf( outEstr, "0" );
			}

		xcase GenesisWhen_ObjectiveComplete: case GenesisWhen_ObjectiveCompleteAll:
		case GenesisWhen_ObjectiveInProgress:
			if( !isForUGC ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not use %s for %s.",
								   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ), debugFieldName );
			} else if( eaSize( &when->eaObjectiveNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "Objective is of type %s, but references no objectives.",
								   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ));
			} else {
				const char* conjunction;
				const char* exprFunc;

				if( when->type == GenesisWhen_ObjectiveCompleteAll ) {
					conjunction = " or ";
				} else {
					conjunction = " and ";
				}

				if( when->type == GenesisWhen_ObjectiveInProgress ) {
					if( context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission ) {
						exprFunc = "OpenMissionStateInProgress";
					} else {
						exprFunc = "MissionStateInProgress";
					}
				} else {
					if( context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission ) {
						exprFunc = "OpenMissionStateSucceeded";
					} else {
						exprFunc = "MissionStateSucceeded";
					}
				}

				{
					int it;
					for( it = 0 ; it != eaSize( &when->eaObjectiveNames ); ++it ) {
						estrConcatf( outEstr, "%s%s(\"%s::%s\")",
									 (it != 0) ? conjunction : "", exprFunc,
									 missionName, when->eaObjectiveNames[ it ]);
					}
				}
			}
			
		xcase GenesisWhen_PromptStart: case GenesisWhen_PromptComplete:
		case GenesisWhen_PromptCompleteAll: {
			bool isComplete = (when->type != GenesisWhen_PromptStart);
			if( eaSize( &when->eaPromptNames ) == 0 && eaSize( &when->eaPromptBlocks ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type PromptComplete, but references no prompts.",
								   debugFieldName );
			} else {
				const char* conjunction;
				int it;
				int blockIt;

				if( when->type == GenesisWhen_PromptComplete ) {
					conjunction = " or ";
				} else {
					conjunction = " and ";
				}
				
				for( it = 0; it != eaSize( &when->eaPromptNames ); ++it ) {
					if(   stricmp( when->eaPromptNames[ it ], "MissionReturn" ) == 0
						  || stricmp( when->eaPromptNames[ it ], "MissionContinue" ) == 0 ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not reference MissionReturn and MissionContinue prompts." );
					} else {
						GenesisMissionPrompt* prompt = genesisFindPrompt( context, when->eaPromptNames[ it ]);

						if( !prompt ) {
							genesisRaiseError( GENESIS_ERROR, debugContext, "%s references prompt \"%s\", but it does not exist.",
											   debugFieldName, when->eaPromptNames[ it ]);
						} else {
							char** completeBlockNames = genesisPromptBlockNames( context, prompt, isComplete );

							if( eaSize( &completeBlockNames )) {
								estrConcatf( outEstr, "%s(", (it != 0 ? conjunction : "") );
							}
							for( blockIt = 0; blockIt != eaSize( &completeBlockNames ); ++blockIt ) {
								char* promptMapChallenge;
								GameEvent* event;
								char eventName[ 256 ];

								if( prompt->pcChallengeName && !eaSize( &prompt->eaExternalMapNames )) {
									promptMapChallenge = prompt->pcChallengeName;
								} else {
									promptMapChallenge = NULL;
								}

								event = genesisPromptEvent( when->eaPromptNames[ it ], completeBlockNames[ blockIt ], isComplete,
															shortMissionName, zmapName, promptMapChallenge );
								sprintf( eventName, "Prompt_Complete_%s_%d",
										 when->eaPromptNames[ it ], eaSize( outEvents ) + 1 );
								estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
											 (blockIt != 0 ? " or " : ""), eventName );
								event->pchEventName = allocAddString( eventName );
								eaPush( outEvents, event );
							}
							if( eaSize( &completeBlockNames )) {
								estrConcatf( outEstr, ")" );
							}

							eaDestroy( &completeBlockNames );
						}
					}
				}

				for( it = 0; it != eaSize( &when->eaPromptBlocks ); ++it ) {
					GenesisWhenPromptBlock* whenPB = when->eaPromptBlocks[ it ];
					GenesisMissionPrompt* prompt = genesisFindPrompt( context, whenPB->promptName );
					GenesisMissionPromptBlock* block = genesisFindPromptBlock( context, prompt, whenPB->blockName );

					if( !prompt || !block ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references prompt \"%s\", block \"%s\", but it does not exist.",
										   debugFieldName, whenPB->promptName, whenPB->blockName );
					} else {
						char* promptMapChallenge;
						GameEvent* event;
						char eventName[ 256 ];

						if( prompt->pcChallengeName && !eaSize( &prompt->eaExternalMapNames )) {
							promptMapChallenge = prompt->pcChallengeName;
						} else {
							promptMapChallenge = NULL;
						}

						event = genesisPromptEvent( whenPB->promptName, whenPB->blockName, isComplete,
													shortMissionName, zmapName, promptMapChallenge );
						sprintf( eventName, "Prompt_Complete_%s_%s_%d",
								 whenPB->promptName, whenPB->blockName, eaSize( outEvents ) + 1 );
						estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
									 (estrLength( outEstr ) ? conjunction : ""), eventName );
						event->pchEventName = allocAddString( eventName );
						eaPush( outEvents, event );
					}
				}
			}
		}

		xcase GenesisWhen_ExternalPromptComplete: {
			if( eaSize( &when->eaExternalPrompts ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ExternalPromptComplete, but references no prompts.",
								   debugFieldName );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaExternalPrompts ); ++it ) {
					GameEvent* event = genesisExternalPromptEvent( when->eaExternalPrompts[ it ]->pcPromptName, when->eaExternalPrompts[ it ]->pcContactName, true );
					char eventName[ 256 ];
						
					sprintf( eventName, "Prompt_Complete_%s_%d",
							 when->eaExternalPrompts[ it ]->pcContactName, eaSize( outEvents ) + 1 );
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0 ? " or " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}
			}
		}
			
		xcase GenesisWhen_ContactComplete: {
			if( eaSize( &when->eaContactNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ContactComplete, but references no contacts.",
								   debugFieldName );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaContactNames ); ++it ) {
					GameEvent* event = genesisTalkToContactEvent( when->eaContactNames[ it ]);
					char eventName[ 256 ];
						
					sprintf( eventName, "Contact_Complete_%s_%d",
							 when->eaContactNames[ it ], eaSize( outEvents ) + 1 );
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0 ? " or " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}
			}
		}

		xcase GenesisWhen_ChallengeComplete: {
			if( eaSize( &when->eaChallengeNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ChallengeComplete, but references no challenges.",
								   debugFieldName );
			} else {
				GameEvent** challengeCompletes = NULL;
				int challengeCount = 0;
				int it;
				for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
					GenesisMissionZoneChallenge* challenge = genesisFindZoneChallengeRaw( SAFE_MEMBER( context, genesis_data ), SAFE_MEMBER( context, zone_mission ), NULL, when->eaChallengeNames[ it ]);
					if( !challenge ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references challenge \"%s\", but it does not exist.",
										   debugFieldName, when->eaChallengeNames[ it ]);
					} else if( challenge->eType == GenesisChallenge_None ) {
						genesisRaiseError( GENESIS_ERROR, debugContext, "%s references challenge \"%s\" with no type.",
										   debugFieldName, when->eaChallengeNames[ it ]);
					} else {
						GameEvent* event = genesisCompleteChallengeEvent( challenge->eType, challenge->pcName, true, zmapName );
						char eventName[ 256 ];

						sprintf( eventName, "Complete_Challenge_%s_%d",
								 when->eaChallengeNames[ it ],
								 eaSize( outEvents ) + eaSize( &challengeCompletes ) + 1 );
						event->pchEventName = allocAddString( eventName );
						
						eaPush( &challengeCompletes, event );
						challengeCount += challenge->iNumToComplete;
					}
				}

				for( it = 0; it != eaSize( &challengeCompletes ); ++it ) {
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0) ? " + " : "",
								 challengeCompletes[ it ]->pchEventName );
					eaPush( outEvents, challengeCompletes[ it ]);
				}
				estrConcatf( outEstr, " >= %d",
							 (when->iChallengeNumToComplete ? when->iChallengeNumToComplete : challengeCount) );
				*outShowCount = (challengeCount > 1);
				eaDestroy( &challengeCompletes );
			}
		}
			
		xcase GenesisWhen_ExternalChallengeComplete: {
			if( eaSize( &when->eaExternalChallenges ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is of type ExternalChallengeComplete, but references no external challenges.",
								   debugFieldName );
			} else {
				GameEvent** challengeCompletes = NULL;
				int challengeCount = 0;
				int it;
				for( it = 0; it != eaSize( &when->eaExternalChallenges ); ++it ) {
					GenesisWhenExternalChallenge* challenge = when->eaExternalChallenges[ it ];
					
					GameEvent* event = genesisCompleteChallengeEvent( challenge->eType, challenge->pcName, false, challenge->pcMapName );
					char eventName[ 256 ];

					sprintf( eventName, "External_Challenge_Complete_%d",
								 eaSize( outEvents ) + eaSize( &challengeCompletes ) + 1 );
					event->pchEventName = allocAddString( eventName );
						
					eaPush( &challengeCompletes, event );
					challengeCount += 1;
				}

				for( it = 0; it != eaSize( &challengeCompletes ); ++it ) {
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0) ? " + " : "",
								 challengeCompletes[ it ]->pchEventName );
					eaPush( outEvents, challengeCompletes[ it ]);
				}
				estrConcatf( outEstr, " >= %d", challengeCount );
				*outShowCount = (challengeCount > 1 );
				eaDestroy( &challengeCompletes );
			}
		}
		
		xcase GenesisWhen_RoomEntry: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				GenesisProceduralObjectParams* params;

				if( context ) {
					params = genesisInternRoomRequirementParams( context, when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName );
				} else {
					params = NULL;
				}

				{
					GameEvent* event = genesisReachLocationEvent( when->eaRooms[ it ]->layoutName,
																  when->eaRooms[ it ]->roomName,
																  shortMissionName, zmapName );
					char eventName[ 256 ];

					sprintf( eventName, "Room_Entry_%s_%s_%d",
							 when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName, eaSize( outEvents ) + 1 );

					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0 ? " or " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}

				if( context ) {
					genesisProceduralObjectSetEventVolume( params );
				}
			}
		}

		xcase GenesisWhen_ExternalRoomEntry: {
			int it;
			for( it = 0; it != eaSize( &when->eaExternalRooms ); ++it ) {
				GameEvent* event = genesisReachLocationEventRaw( when->eaExternalRooms[ it ]->pcMapName,
																 when->eaExternalRooms[ it ]->pcName );
				char eventName[ 256 ];

				sprintf( eventName, "External_Room_Entry_%s_%s_%d",
						 when->eaExternalRooms[ it ]->pcMapName, when->eaExternalRooms[ it ]->pcName, eaSize( outEvents ) + 1 );
				estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
							 (it != 0 ? " or " : ""), eventName );
				event->pchEventName = allocAddString( eventName );
				eaPush( outEvents, event );
			}
		}

		xcase GenesisWhen_RoomEntryAll: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				GenesisProceduralObjectParams* params;

				if( context ) {
					params = genesisInternRoomRequirementParams( context, when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName );
				} else {
					params = NULL;
				}

				{
					GameEvent* event = genesisReachLocationEvent( when->eaRooms[ it ]->layoutName,
																  when->eaRooms[ it ]->roomName,
																  shortMissionName, zmapName );
					char eventName[ 256 ];

					sprintf( eventName, "Room_Entry_%s_%s_%d",
							 when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName, eaSize( outEvents ) + 1 );

					estrConcatf( outEstr, "%s(MissionEventCount(\"%s\") > 0)",
								 (it != 0 ? " + " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}

				if( context ) {
					genesisProceduralObjectSetEventVolume( params );
				}
			}

			estrConcatf( outEstr, " >= %d", eaSize( &when->eaRooms ));
			*outShowCount = (eaSize( &when->eaRooms ) > 1);
		}

		xcase GenesisWhen_CritterKill: {
			if( eaSize( &when->eaCritterDefNames ) + eaSize( &when->eaCritterGroupNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is CritterKill, but no critters are specified.",
								   debugFieldName );
				estrConcatStatic( outEstr, "0" );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaCritterDefNames ); ++it ) {
					GameEvent* event = genesisKillCritterEvent( when->eaCritterDefNames[ it ], zmapName );
					char eventName[ 256 ];

					sprintf( eventName, "Critter_Kill_%s_%d",
							 when->eaCritterDefNames[ it ], eaSize( outEvents ) + 1 );
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (estrLength( outEstr ) ? " + " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}
				for( it = 0; it != eaSize( &when->eaCritterGroupNames ); ++it ) {
					GameEvent* event = genesisKillCritterEvent( when->eaCritterGroupNames[ it ], zmapName );
					char eventName[ 256 ];

					sprintf( eventName, "Critter_Group_%s_%d",
							 when->eaCritterGroupNames[ it ], eaSize( outEvents ) + 1 );
					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (estrLength( outEstr ) ? " + " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}

				estrConcatf( outEstr, " >= %d", MAX( 1, when->iCritterNumToComplete ));
				*outShowCount = (when->iCritterNumToComplete > 1);
			}
		}

		xcase GenesisWhen_ItemCount:
			if( eaSize( &when->eaItemDefNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ItemCount, but no items are specified.",
								   debugFieldName );
				estrConcatStatic( outEstr, "0" );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaItemDefNames ); ++it ) {
					estrConcatf( outEstr, "%sPlayerItemCount(\"%s\")",
								 (estrLength( outEstr ) ? " + " : ""),
								 when->eaItemDefNames[ it ]);
				}
				estrConcatf( outEstr, " >= %d", when->iItemCount );
				*outShowCount = (when->iItemCount > 1 );
			}

		xcase GenesisWhen_ExternalOpenMissionComplete:
			if( eaSize( &when->eaExternalMissionNames ) == 0 ) {
				genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ExternalOpenMissionComplete, but no missions are specified.",
								   debugFieldName );
				estrConcatStatic( outEstr, "0" );
			} else {
				int it;
				for( it = 0; it != eaSize( &when->eaExternalMissionNames ); ++it ) {
					estrConcatf( outEstr, "%sOpenMissionMapCredit(\"%s\")",
								 (it != 0) ? " or " : "",
								 when->eaExternalMissionNames[ it ]);
				}
			}
			
		xcase GenesisWhen_ExternalMapStart:
			if(when->bAnyCrypticMap)
				estrConcatf( outEstr, "NOT IsOnUGCMap()" );
			else
			{
				if( eaSize( &when->eaExternalMapNames ) == 0 ) {
					genesisRaiseError( GENESIS_ERROR, debugContext, "%s is ExternalMapStart, but no map is specified",
									   debugFieldName );
					estrConcatStatic( outEstr, "0" );
				} else {
					int it;
					for( it = 0; it != eaSize( &when->eaExternalMapNames ); ++it ) {
						estrConcatf( outEstr, "%sIsOnMapNamed(\"%s\")",
									 (it != 0) ? " or " : "",
									 when->eaExternalMapNames[ it ]);
					}
				}
			}

		xcase GenesisWhen_ReachChallenge: {
			int it;
			for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
				if (context) {
					GenesisProceduralObjectParams *params = genesisInternVolumeRequirementParams( context, when->eaChallengeNames[it]);
					genesisProceduralObjectSetEventVolume(params);
				}

				{
					GameEvent* event = genesisReachLocationEvent( NULL, when->eaChallengeNames[it],
																  shortMissionName, zmapName );
					char eventName[ 256 ];

					sprintf( eventName, "Reach_Challenge_%s_%d",
							 when->eaChallengeNames[it], eaSize( outEvents ) + 1 );

					estrConcatf( outEstr, "%sMissionEventCount(\"%s\")",
								 (it != 0 ? " or " : ""), eventName );
					event->pchEventName = allocAddString( eventName );
					eaPush( outEvents, event );
				}
			}
		}


		xdefault: {
			char buffer[256];
			genesisErrorPrintContextStr(debugContext, SAFESTR(buffer));
			genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "%s's %s uses unimplemented When type %s",
										   buffer, debugFieldName, StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ));
		}
	}
	
	if( when->checkedAttrib ) {
		genesisRaiseError( GENESIS_ERROR, debugContext, "%s has a checked attrib, which is not allowed on a mission.",
						   debugFieldName );
	}

	if( when->bNot ) {
		estrInsertf( outEstr, 0, "not (" );
		estrConcatf( outEstr, ")" );
	}

	// If the mission is a player mission, then this mission could be
	// shared by a bunch of unrelated people
	if( generationType == GenesisMissionGenerationType_PlayerMission ) {
		int eventIt;
		for( eventIt = 0; eventIt != eaSize( outEvents ); ++eventIt ) {
			(*outEvents)[ eventIt ]->tMatchSourceTeam = TriState_Yes;
		}
	}
}

/// Create an expression that corresponds to ATTRIB.
char* genesisCheckedAttribText( GenesisMissionContext* context, GenesisCheckedAttrib* attrib, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName, bool isTeam )
{
	char* accum = NULL;

	if( attrib ) {

		if (attrib->name == allocAddString("PlayerHasItem"))
		{
			estrPrintf( &accum, "PlayerItemCount(\"%s\")", attrib->astrItemName );
		}
		else
		{
			GenesisConfigCheckedAttrib* checkedAttrib = genesisCheckedAttrib( attrib->name );
			if( checkedAttrib ) {
				if( !isTeam ) {
					estrPrintf( &accum, "GetPlayerEnt().%s", checkedAttrib->playerExprText );
				} else {
					estrPrintf( &accum, "GetTeamEntsAll().%s", checkedAttrib->teamExprText );
				}

				if( attrib->bNot ) {
					estrInsertf( &accum, 0, "not " );;
				}
			} else {
				genesisRaiseError( GENESIS_ERROR, debugContext, "Trying to set %s to CheckedAttrib %s, but that CheckedAttrib does not exist.",
								   debugFieldName, attrib->name );
			}
		}
	}

	return accum;
}

/// Return an array of names for objects that should be in a waypoint lists.
void genesisWhenMissionWaypointObjects( char*** out_makeVolumeObjects, MissionWaypoint*** out_waypoints, GenesisMissionContext* context, GenesisWhen* when, GenesisRuntimeErrorContext* debugContext, const char* debugFieldName )
{
	char** accum = NULL;
	MissionWaypoint** waypointsAccum = NULL;
	
	switch( when->type ) {
		// Conditions that shouldn't work for missions.
		case GenesisWhen_MapStart: case GenesisWhen_Manual: case GenesisWhen_MissionComplete:
		case GenesisWhen_MissionNotInProgress: case GenesisWhen_ObjectiveComplete: case GenesisWhen_ObjectiveCompleteAll:
		case GenesisWhen_ObjectiveInProgress: case GenesisWhen_ChallengeAdvance:
			genesisRaiseError( GENESIS_ERROR, debugContext, "Missions can not use %s for %s.",
							   StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ), debugFieldName );

		xcase GenesisWhen_PromptComplete: case GenesisWhen_PromptCompleteAll:
			if( when->pcPromptChallengeName ) {
				eaPush( &accum, StructAllocString( when->pcPromptChallengeName ));
			} else {
				genesisPushAllRoomNames( context, &accum );
			}

		xcase GenesisWhen_ContactComplete:
			genesisPushAllRoomNames( context, &accum );

		xcase GenesisWhen_ChallengeComplete: {
			int it;
			for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
				eaPush( &accum, StructAllocString( when->eaChallengeNames[ it ]));
			}
		}

		xcase GenesisWhen_ReachChallenge: {
			int it;
			for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
				eaPush( &accum, StructAllocString( genesisMissionChallengeVolumeName( when->eaChallengeNames[ it ],
																					  context->zone_mission->desc.pcName )));
			}
		}

		xcase GenesisWhen_RoomEntry: case GenesisWhen_RoomEntryAll: {
			int it;
			for( it = 0; it != eaSize( &when->eaRooms ); ++it ) {
				eaPush( &accum, StructAllocString( genesisMissionRoomVolumeName( when->eaRooms[ it ]->layoutName, when->eaRooms[ it ]->roomName,
																				 context->zone_mission->desc.pcName )));
			}
		}

		xcase GenesisWhen_CritterKill: {
			int it;
			for( it = 0; it != eaSize( &context->zone_mission->eaChallenges ); ++it ) {
				GenesisMissionZoneChallenge* challenge = context->zone_mission->eaChallenges[ it ];
				if( challenge->eType == GenesisChallenge_Encounter || challenge->eType == GenesisChallenge_Encounter2 ) {
					eaPush( &accum, StructAllocString( challenge->pcName ));
				}
			}
		}
		
		xcase GenesisWhen_ItemCount: {
			int it;
			for( it = 0; it != eaSize( &context->zone_mission->eaChallenges ); ++it ) {
				GenesisMissionZoneChallenge* challenge = context->zone_mission->eaChallenges[ it ];
				eaPush( &accum, StructAllocString( challenge->pcName ));
			}
		}

		xcase GenesisWhen_ExternalOpenMissionComplete: case GenesisWhen_ExternalMapStart: {
			// nothing to do, if you're on the map, you're on the map!
		}

		xcase GenesisWhen_ExternalChallengeComplete: {
			// MJF Aug/16/2012: This is not done here, because if it
			// was the waypoints would not go away as you interacted
			// with them.  Instead, each subobjective generated will
			// have a waypoint on it.
			//
			// See the call to
			// genesisCreateMissionWaypointForExternalChallenge() in
			// genesisCreateObjective().
		}
			
			
		xcase GenesisWhen_ExternalRewardBoxLooted: case GenesisWhen_RewardBoxLooted: {
			int it;
			for( it = 0; it != eaSize( &when->eaExternalChallenges ); ++it ) {
				GenesisWhenExternalChallenge* challenge = when->eaExternalChallenges[ it ];
				eaPush( &waypointsAccum, genesisCreateMissionWaypointForExternalChallenge( challenge ));
			}
		}

		xcase GenesisWhen_ExternalPromptComplete: {
			int it;
			for( it = 0; it != eaSize( &when->eaExternalPrompts ); ++it ) {
				GenesisWhenExternalPrompt* prompt = when->eaExternalPrompts[ it ];
				if( prompt->pcEncounterName && prompt->pcEncounterMapName ) {
					MissionWaypoint* waypointAccum = StructCreate( parse_MissionWaypoint );
					eaPush( &waypointsAccum, waypointAccum );

					waypointAccum->type = MissionWaypointType_Encounter;
					waypointAccum->name = StructAllocString( prompt->pcEncounterName );
					waypointAccum->mapName = allocAddString( prompt->pcEncounterMapName );
				}
			}
		}
			
		xcase GenesisWhen_ExternalRoomEntry: {
			int it;
			for( it = 0; it != eaSize( &when->eaExternalRooms ); ++it ) {
				GenesisWhenExternalRoom* room = when->eaExternalRooms[ it ];
				MissionWaypoint* waypointAccum = StructCreate( parse_MissionWaypoint );
				eaPush( &waypointsAccum, waypointAccum );

				waypointAccum->type = MissionWaypointType_Volume;
				waypointAccum->name = StructAllocString( room->pcName );
				waypointAccum->mapName = allocAddString( room->pcMapName );
			}
		}

		xdefault: {
			char buffer[256];
			genesisErrorPrintContextStr(debugContext, SAFESTR(buffer));
			genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "%s's %s uses unimplemented When type %s",
										   buffer, debugFieldName, StaticDefineIntRevLookup( GenesisWhenTypeEnum, when->type ));
		}
	}

	*out_makeVolumeObjects = accum;
	*out_waypoints = waypointsAccum;
}

/// Create a waypoint for an external challenge.
MissionWaypoint* genesisCreateMissionWaypointForExternalChallenge( GenesisWhenExternalChallenge* challenge )
{
	MissionWaypoint* waypointAccum = StructCreate( parse_MissionWaypoint );

	switch( challenge->eType ) {
		case GenesisChallenge_Clickie: case GenesisChallenge_Destructible:
			waypointAccum->type = MissionWaypointType_Clicky;
						
		xcase GenesisChallenge_Encounter: case GenesisChallenge_Encounter2:
		case GenesisChallenge_Contact:
			waypointAccum->type = MissionWaypointType_Encounter;
						
		xdefault:
			waypointAccum->type = MissionWaypointType_None;
	}
	waypointAccum->name = StructAllocString( challenge->pcName );
	waypointAccum->mapName = allocAddString( challenge->pcMapName );

	return waypointAccum;
}



/// Validate that this context has all the information needed to
/// transmogrify.
bool genesisTransmogrifyMissionValidate( GenesisTransmogrifyMissionContext* context )
{
	bool fatalAccum = false;

	if (!zmapInfoGetStartSpawnName(context->zmap_info) &&
		!context->mission_desc->zoneDesc.startDescription.pcStartRoom)
	{
		genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMission(context->mission_desc->zoneDesc.pcName), "Mission has no starting room!");
	}

	// Validate challenges
	{
		int it;
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		for( it = 0; it != eaSize( &context->mission_desc->eaChallenges ); ++it ) {
			GenesisMissionChallenge* challenge = context->mission_desc->eaChallenges[ it ];
			
			if( !stashAddInt( table, challenge->pcName, it, false )) {
				int firstIndex;
				stashFindInt( table, challenge->pcName, &firstIndex );
				genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), challenge->pcLayoutName ),
								   "Duplicate challenge found at index %d and %d.",
								   firstIndex + 1, it + 1 );
				fatalAccum = true;
			}

			if( !genesisTransmogrifyChallengeValidate( context, challenge )) {
				fatalAccum = true;
			}
		}
		for( it = 0; it != eaSize( &context->map_desc->shared_challenges ); ++it ) {
			GenesisMissionChallenge* challenge = context->map_desc->shared_challenges[ it ];
			
			if( !stashAddInt( table, challenge->pcName, it | 0xF00D0000, false )) {
				int firstIndex;
				bool firstShared;
				stashFindInt( table, challenge->pcName, &firstIndex );

				if( (firstIndex & 0xFFFF0000) == 0xF00D0000 ) {
					firstShared = true;
					firstIndex &= 0xFFFF;
				} else {
					firstShared = false;
				}
				
				genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), challenge->pcLayoutName ),
								   "Duplicate challenge found at index %d%s and %d (shared).",
								   firstIndex + 1, (firstShared ? " (shared)" : ""), it + 1 );
				fatalAccum = true;
			}
		}
		stashTableDestroy( table );
	}

	// Validate prompts
	{
		int it;
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		const char** mapStartPrompts = NULL;
		for( it = 0; it != eaSize( &context->mission_desc->zoneDesc.eaPrompts ); ++it ) {
			GenesisMissionPrompt* prompt = context->mission_desc->zoneDesc.eaPrompts[ it ];
			
			if( !stashAddInt( table, prompt->pcName, it, false )) {
				int firstIndex;
				stashFindInt( table, prompt->pcName, &firstIndex );
				genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), prompt->pcLayoutName ),
								   "Duplicate prompt found at index %d and %d.",
								   firstIndex + 1, it + 1 );
			}

			if( prompt->showWhen.type == GenesisWhen_MapStart && !prompt->pcChallengeName ) {
				eaPush( &mapStartPrompts, prompt->pcName );
			}
		}

		if( eaSize( &mapStartPrompts ) > 1 ) {
			for( it = 0; it != eaSize( &mapStartPrompts ); ++it ) {
				genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextPrompt( mapStartPrompts[ it ], NULL, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), NULL),
								   "Multiple prompts show at MapStart." );
			}
		}
		eaDestroy( &mapStartPrompts );

		stashTableDestroy( table );
	}

	// Validate FSMs
	{
		int it;
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		for( it = 0; it != eaSize( &context->mission_desc->zoneDesc.eaFSMs ); ++it ) {
			GenesisFSM* gfsm = context->mission_desc->zoneDesc.eaFSMs[ it ];

			if( !stashAddInt( table, gfsm->pcName, it, false )) {
				int firstIndex;
				stashFindInt( table, gfsm->pcName, &firstIndex );
				genesisRaiseError(	GENESIS_FATAL_ERROR, genesisMakeTempErrorContextPrompt( gfsm->pcName, NULL, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), NULL),
									"Duplicate prompt found at index %d and %d.",
									firstIndex + 1, it + 1 );
			}
		}

		stashTableDestroy( table );
	}

	return !fatalAccum;
}

/// Validate that this context has all the shared missions needed to
/// transmogrify.
bool genesisTransmogrifySharedChallengesValidate( GenesisTransmogrifyMissionContext* context )
{
	bool fatalAccum = false;
	int it;

	for( it = 0; it != eaSize( &context->map_desc->shared_challenges ); ++it ) {
		GenesisMissionChallenge* challenge = context->map_desc->shared_challenges[ it ];

		if( !genesisTransmogrifyChallengeValidate( context, challenge )) {
			fatalAccum = true;
		}
	}
	
	return !fatalAccum;
}


/// Validate that this challenge has all the data needed to
/// transmogrify.
bool genesisTransmogrifyChallengeValidate( GenesisTransmogrifyMissionContext* context, GenesisMissionChallenge* challenge )
{
	bool fatalAccum = false;
	bool isShared = (context->mission_desc == NULL);
	GenesisRuntimeErrorContext* contextBuffer = genesisMakeErrorContextChallenge( challenge->pcName, SAFE_MEMBER( context->mission_desc, zoneDesc.pcName ), challenge->pcLayoutName );

	// Warn if the challenge is using anything that does not get
	// persisted correctly with player missions.
	if(   challenge->spawnWhen.type != GenesisWhen_MapStart
		  && challenge->spawnWhen.type != GenesisWhen_ObjectiveInProgress
		  && challenge->spawnWhen.type != GenesisWhen_ObjectiveComplete
		  && challenge->spawnWhen.type != GenesisWhen_ObjectiveCompleteAll
		  && challenge->spawnWhen.type != GenesisWhen_ChallengeAdvance
		  && challenge->spawnWhen.type != GenesisWhen_Manual
		  && !resNamespaceIsUGC( zmapInfoGetPublicName( context->zmap_info ))) {
		genesisRaiseError( GENESIS_WARNING, contextBuffer,
						   "Spawn when is set to %s, this is error prone.  "
						   "Consider using ObjectiveInProgress or ObjectiveComplete "
						   "instead.",
						   StaticDefineIntRevLookup( GenesisWhenTypeEnum, challenge->spawnWhen.type ));
	}
	
	if( isShared && challenge->spawnWhen.type != GenesisWhen_MapStart ) {
		genesisRaiseError( GENESIS_FATAL_ERROR, contextBuffer, "Shared challenges must spawn with MapStart." );
		fatalAccum = true;
	} else if( challenge->spawnWhen.type == GenesisWhen_PromptComplete ) {
		// UGC does its own checking to make sure you can't create uncompletable missions.
		if( !resNamespaceIsUGC( zmapInfoGetPublicName( context->zmap_info ))) {
			// If the challenge's when condition is a prompt, but that
			// prompt is not permanently shown, it is possible to get
			// the map into a state where the prompt would never need
			// to show.  They should be using ObjectiveComplete or
			// ObjectiveInProgress on these conditions anyway.
			int it;
			for( it = 0; it != eaSize( &challenge->spawnWhen.eaPromptNames ); ++it ) {
				const char* neverSpawnMessage = "Challenge may never spawn!  Consider changing the challenge's SpawnWhen to ObjectiveInProgress.";
				GenesisMissionPrompt* prompt = genesisTransmogrifyFindPrompt( context, challenge->spawnWhen.eaPromptNames[ it ]);
			
				if( !prompt ) {
					genesisRaiseError( GENESIS_FATAL_ERROR, contextBuffer, "%s  Challenge will not spawn unless prompt %s is shown, but the prompt does not exist.",
									   neverSpawnMessage, challenge->spawnWhen.eaPromptNames[ it ]);
				} else if( !prompt->bOptional ) {
					genesisRaiseError( GENESIS_FATAL_ERROR, contextBuffer, "%s  Challenge will not spawn unless prompt %s is shown, but the prompt does not have a button so it may never show.",
									   neverSpawnMessage, prompt->pcName );
				} else if( prompt->bOptional && prompt->bOptionalHideOnComplete ) {
					genesisRaiseError( GENESIS_FATAL_ERROR, contextBuffer, "%s  Challenge will not spawn unless prompt %s is shown, but the prompt is marked HideOnComplete.",
									   neverSpawnMessage, prompt->pcName );
				} else if( prompt->showWhen.type == GenesisWhen_MissionNotInProgress ) {
					genesisRaiseError( GENESIS_FATAL_ERROR, contextBuffer, "%s  Challenge will not spawn unless prompt %s is shown, but the prompt will only show if a mission is not in progress.",
									   neverSpawnMessage, prompt->pcName );
				}
			}
		}
	}

	StructDestroy( parse_GenesisRuntimeErrorContext, contextBuffer );
	return !fatalAccum;
}

/// Validate that this context has all the information needed to
/// generate.
bool genesisGenerateMissionValidate( GenesisMissionContext* context )
{
	bool fatalAccum = false;
	
	if( nullStr( context->zone_mission->desc.pcName )) {
		char buffer[256];
		sprintf( buffer, "#%d", context->mission_num );
		genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMission(buffer), "Mission missing mission name." );
		fatalAccum = true;
		return !fatalAccum;
	}

	if( !context->zmap_info ) {
		if( context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMission( context->zone_mission->desc.pcName ),
							   "Mission is set to generate an open mission, but this mission is not tied to any map." );
			fatalAccum = true;
		}
		if( eaSize( &context->zone_mission->eaChallenges )) {
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMission( context->zone_mission->desc.pcName ),
							   "Mission has challenges, but this mission is not tied to any map." );
			fatalAccum = true;
		}
		if( context->zone_mission->desc.grantDescription.eGrantType == GenesisMissionGrantType_MapEntry ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMission( context->zone_mission->desc.pcName ),
							   "Mission is to be granted on map entry, but this mission is not tied to any map." );
			fatalAccum = true;
		}
	}
	
	
	if(   context->genesis_data
		  && !(eaSize(&context->genesis_data->genesis_interiors) > 0 && context->genesis_data->genesis_interiors[0]->override_positions)
		  && nullStr( context->zone_mission->desc.startDescription.pcStartRoom )) {
		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context->zone_mission->desc.pcName), "No starting room.");
	}

	{
		int it;
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		for( it = 0; it != eaSize( &context->zone_mission->eaChallenges ); ++it ) {
			GenesisMissionZoneChallenge* challenge = context->zone_mission->eaChallenges[ it ];
			
			if( !stashAddInt( table, challenge->pcName, it, false )) {
				int firstIndex;
				stashFindInt( table, challenge->pcName, &firstIndex );
				genesisRaiseError( GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER( context->zone_mission, desc.pcName ), challenge->pcLayoutName ), 
					"Duplicate challenge found at index %d and %d.",
					firstIndex + 1, it + 1 );
			}
		}

		stashTableDestroy( table );
	}

	{
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		int it;
		for( it = 0; it != eaSize( &context->zone_mission->desc.eaObjectives ); ++it ) {
			genesisGenerateMissionValidateObjective( context, table, &fatalAccum, context->zone_mission->desc.eaObjectives[ it ]);
		}
		stashTableDestroy( table );
	}

	{
		int it;
		StashTable table = stashTableCreateWithStringKeys( 4, StashDefault );
		for( it = 0; it != eaSize( &context->zone_mission->desc.eaPrompts ); ++it ) {
			GenesisMissionPrompt* prompt = context->zone_mission->desc.eaPrompts[ it ];
			
			if( !stashAddInt( table, prompt->pcName, it, false )) {
				int firstIndex;
				stashFindInt( table, prompt->pcName, &firstIndex );
				genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->zone_mission, desc.pcName ), prompt->pcLayoutName ),
								   "Duplicate prompt found at index %d and %d.",
								   firstIndex + 1, it + 1 );
			}
		}

		stashTableDestroy( table );
	}

	return !fatalAccum;
}

/// Validate a specific GenesisMissionObjective.
///
/// TABLE is used to keep a set of objective names unique.
void genesisGenerateMissionValidateObjective( GenesisMissionContext* context, StashTable table, bool* fatalAccum, GenesisMissionObjective* objective )
{
	if( !stashAddPointer( table, objective->pcName, objective->pcName, false )) {
		genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextObjective( objective->pcName, SAFE_MEMBER( context->zone_mission, desc.pcName ) ),
						   "Duplicate objective found." );
		*fatalAccum = true;
	}

	{
		int it;
		for( it = 0; it != eaSize( &objective->eaChildren ); ++it ) {
			genesisGenerateMissionValidateObjective( context, table, fatalAccum, objective->eaChildren[ it ]);
		}
	}
}

/// Fill out player-specific data for the mission into MISSION.
void genesisGenerateMissionPlayerData( GenesisMissionContext* context, MissionDef* mission )
{
	GenesisMissionGrantDescription* grantDescription = &context->zone_mission->desc.grantDescription;
	
	if( grantDescription->eGrantType == GenesisMissionGrantType_RandomNPC ) {
		mission->missionType = MissionType_AutoAvailable;
	} else {																	
		mission->missionType = MissionType_Normal;							
	}
	mission->eShareable = context->zone_mission->desc.eShareable;
	mission->bDisableCompletionTracking = !context->zone_mission->bTrackingEnabled;

	genesisCreateMessage( context, &mission->summaryMsg, context->zone_mission->desc.pcSummaryText );
	genesisCreateMessage( context, &mission->detailStringMsg, context->zone_mission->desc.pcDescriptionText );

	if( nullStr( context->zone_mission->desc.startDescription.pcEntryFromMapName ) != nullStr( context->zone_mission->desc.startDescription.pcEntryFromInteractableName )) {
		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context->zone_mission->desc.pcName), "Only one of EntryFromMapName and EntryFromInteractableName is set, both or neither must be set.");
	} else if( !nullStr( context->zone_mission->desc.startDescription.pcEntryFromMapName ) && !nullStr( context->zone_mission->desc.startDescription.pcEntryFromInteractableName )) {
		// Add the interactable override
		{
			InteractableOverride* interactAccum = StructCreate( parse_InteractableOverride );
			char buffer[ 256 ];
			interactAccum->pPropertyEntry = StructCreate( parse_WorldInteractionPropertyEntry );
			interactAccum->pPropertyEntry->pDoorProperties = StructCreate( parse_WorldDoorInteractionProperties );

			interactAccum->pcMapName = allocAddString( context->zone_mission->desc.startDescription.pcEntryFromMapName );
			interactAccum->pcInteractableName = allocAddString( context->zone_mission->desc.startDescription.pcEntryFromInteractableName );
			interactAccum->pPropertyEntry->pcInteractionClass = allocAddString( "Door" );
			if( grantDescription->eGrantType != GenesisMissionGrantType_MapEntry ) {
				sprintf( buffer, "MissionStateInProgress(\"%s\")", mission->name );
				interactAccum->pPropertyEntry->pInteractCond = exprCreateFromString( buffer, NULL );
			}
			interactAccum->pPropertyEntry->pDoorProperties->doorDest.eType = WVAR_MAP_POINT;
			interactAccum->pPropertyEntry->pDoorProperties->doorDest.eDefaultType = WVARDEF_SPECIFY_DEFAULT;
			interactAccum->pPropertyEntry->pDoorProperties->doorDest.pSpecificValue = StructCreate(parse_WorldVariable);
			interactAccum->pPropertyEntry->pDoorProperties->doorDest.pSpecificValue->eType = WVAR_MAP_POINT;
			interactAccum->pPropertyEntry->pDoorProperties->doorDest.pSpecificValue->pcZoneMap = StructAllocString( zmapInfoGetPublicName( context->zmap_info ));
			{
				WorldVariableDef* varAccum = StructCreate( parse_WorldVariableDef );
				varAccum->pSpecificValue = StructCreate( parse_WorldVariable );
				
				varAccum->pSpecificValue->pcName = varAccum->pcName = allocAddString( "Mission_Num" );
				varAccum->pSpecificValue->eType = varAccum->eType = WVAR_INT;
				varAccum->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
				varAccum->pSpecificValue->iIntVal = context->mission_num;
				eaPush( &interactAccum->pPropertyEntry->pDoorProperties->eaVariableDefs, varAccum );
			}				
			eaPush( &mission->ppInteractableOverrides, interactAccum );
		}

		// And its location
		{
			MissionWaypoint* waypointAccum = StructCreate( parse_MissionWaypoint );

			waypointAccum->type = MissionWaypointType_Clicky;
			waypointAccum->name = StructAllocString( context->zone_mission->desc.startDescription.pcEntryFromInteractableName );
			waypointAccum->mapName = allocAddString( context->zone_mission->desc.startDescription.pcEntryFromMapName );

			eaPush(&mission->eaWaypoints, waypointAccum);
		}
	}

	mission->params = StructCreate( parse_MissionDefParams );
	mission->params->OnsuccessRewardTableName = REF_STRING_FROM_HANDLE( context->zone_mission->desc.hReward );
	mission->params->NumericRewardScale = context->zone_mission->desc.rewardScale;
	
	// If this is a StarCluster map, create a door key game action to occur when the mission starts
	if( genesisIsStarClusterMap( context->zmap_info )) {
		WorldVariableDef* pDestDef = StructCreate(parse_WorldVariableDef);
		WorldGameActionProperties* pAction = StructCreate(parse_WorldGameActionProperties);
		pAction->eActionType = WorldGameActionType_GiveDoorKeyItem;
		pAction->pGiveDoorKeyItemProperties = StructCreate(parse_WorldGiveDoorKeyItemActionProperties);
		SET_HANDLE_FROM_STRING("ItemDef", ITEM_MISSION_DOOR_KEY_DEF, pAction->pGiveDoorKeyItemProperties->hItemDef);
		pAction->pGiveDoorKeyItemProperties->pDestinationMap = pDestDef;
		pDestDef->eType = WVAR_MAP_POINT;
		pDestDef->pSpecificValue = StructCreate(parse_WorldVariable);
		pDestDef->pSpecificValue->eType = WVAR_MAP_POINT;
		eaPush(&mission->ppOnStartActions, pAction);
	}

	// grant mission info
	switch( grantDescription->eGrantType ) {
		case GenesisMissionGrantType_MapEntry: {
			GenesisMissionRequirements* grantReq = genesisInternRequirement( context );
			char grantExpr[ 1024 ];
			
			if( !grantReq->params ) {
				grantReq->params = StructCreate( parse_GenesisProceduralObjectParams );
			}
			genesisProceduralObjectSetActionVolume( grantReq->params );

			sprintf( grantExpr, "GrantMission(\"%s\")", genesisMissionName( context, true ));			
			if( grantReq->params->action_volume_properties->entered_action ) {
				exprAppendStringLines( grantReq->params->action_volume_properties->entered_action, grantExpr );
			} else {
				grantReq->params->action_volume_properties->entered_action = exprCreateFromString( grantExpr, NULL );
			}
		}
			
		xcase GenesisMissionGrantType_RandomNPC:
			// nothing else to do

		xcase GenesisMissionGrantType_Manual:
			// nothing else to do
					
		xcase GenesisMissionGrantType_Contact: {
			const GenesisMissionGrant_Contact* contact = grantDescription->pGrantContact;
			ContactMissionOffer* offerAccum = genesisInternMissionOffer( context, context->zone_mission->desc.pcName, false );
					
			offerAccum->allowGrantOrReturn = ContactMissionAllow_GrantOnly;
			eaPush( &offerAccum->offerDialog, genesisCreateDialogBlock( context, contact->pcOfferText, NULL ));
			eaPush( &offerAccum->inProgressDialog, genesisCreateDialogBlock( context, contact->pcInProgressText, NULL ));
		}
	}

	switch( grantDescription->eTurnInType ) {
		case GenesisMissionTurnInType_Automatic:
			mission->needsReturn = false;

		xcase GenesisMissionTurnInType_GrantingContact: case GenesisMissionTurnInType_DifferentContact: {
			const GenesisMissionTurnIn_Contact* contact = grantDescription->pTurnInContact;
			bool isSameContact = (grantDescription->eTurnInType == GenesisMissionTurnInType_GrantingContact);
			ContactMissionOffer* offerAccum = genesisInternMissionOffer( context, context->zone_mission->desc.pcName, !isSameContact );

			if( isSameContact ) {
				// The contact should already exist, filled out above.
				if( offerAccum->allowGrantOrReturn != ContactMissionAllow_GrantOnly ) {
					genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextMission(context->zone_mission->desc.pcName), "Return type is GrantingContact, but a contact does not grant this mission!");
				}
				
				offerAccum->allowGrantOrReturn = ContactMissionAllow_GrantAndReturn;
			} else {
				offerAccum->allowGrantOrReturn = ContactMissionAllow_ReturnOnly;
			}
			mission->needsReturn = true;
			mission->eReturnType = MissionReturnType_Message;
			genesisCreateMessage( context, &mission->msgReturnStringMsg, contact->pcMissionReturnText );
					
			eaPush( &offerAccum->completedDialog, genesisCreateDialogBlock( context, contact->pcCompletedText, NULL ));
		}
	}

	switch( grantDescription->eFailType ) {
		case GenesisMissionFailType_Never:
			// nothing to do

		xcase GenesisMissionFailType_Timeout:
			mission->uTimeout = grantDescription->iFailTimeoutSeconds;
			genesisAccumFailureExpr( mission, "TimeExpired()" );
			{
				WorldGameActionProperties* floatieAccum = StructCreate( parse_WorldGameActionProperties );
				floatieAccum->eActionType = WorldGameActionType_SendFloaterMsg;
				floatieAccum->pSendFloaterProperties = StructCreate( parse_WorldSendFloaterActionProperties );
				genesisCreateMessage( context, &floatieAccum->pSendFloaterProperties->floaterMsg, "Out of time." );
				floatieAccum->pSendFloaterProperties->floaterMsg.bEditorCopyIsServer = true;
				setVec3( floatieAccum->pSendFloaterProperties->vColor, 226.0 / 255.0, 0, 0 );

				eaPush( &mission->ppFailureActions, floatieAccum );
			}

		xdefault:
			FatalErrorf( "not yet implemented" );
	}

	// cooldown info
	if( grantDescription->bRepeatable ) {
		mission->repeatable = true;
		mission->fRepeatCooldownHours = grantDescription->fRepeatCooldownHours;
		mission->fRepeatCooldownHoursFromStart = grantDescription->fRepeatCooldownHoursFromStart;
		mission->iRepeatCooldownCount = grantDescription->iRepeatCooldownCount;
		mission->bRepeatCooldownBlockTime = grantDescription->bRepeatCooldownBlockTime;
	}

	// requires info
	if( eaSize( &grantDescription->eaRequiresMissions )) {
		char* requiresExprAccum = NULL;
		int it;

		for( it = 0; it != eaSize( &grantDescription->eaRequiresMissions ); ++it ) {
			const char* missionName = grantDescription->eaRequiresMissions[ it ];

			estrConcatf( &requiresExprAccum, "%sHasCompletedMission(\"%s\")",
						 (it != 0 ? " and " : ""),
						 missionName );
		}

		mission->missionReqs = exprCreateFromString( requiresExprAccum, NULL );
		estrDestroy( &requiresExprAccum );
	}
}

/// Fill out MISSION's filename and scope.
const void genesisMissionUpdateFilename( GenesisMissionContext* context, MissionDef* mission )
{
	if( context->zmap_info ) {
		char path[ MAX_PATH ];
		char nameSpace[RESOURCE_NAME_MAX_SIZE];
		char baseName[RESOURCE_NAME_MAX_SIZE];
	
		resExtractNameSpace_s(zmapInfoGetFilename( context->zmap_info ), NULL, 0, SAFESTR(path));
		getDirectoryName( path );

		strcat( path, "/missions" );
		mission->scope = allocAddFilename( path );
	
		if (resExtractNameSpace(mission->name, nameSpace, baseName))
			sprintf( path, "%s:%s/%s.mission", nameSpace, mission->scope, baseName);
		else
			strcatf( path, "/%s.mission", mission->name );
		mission->filename = allocAddFilename( path );
	} else {
		// Right now, only UGC is using this feature
		char path[ MAX_PATH ];
		sprintf( path, "Maps/%s", context->project_prefix);
		mission->scope = allocAddFilename(path);
	}
}

/// Return an alloc'd name for a mission.
///
/// If PLAYER-SPECIFIC is true, then return the name of the player
/// specific mission.  This only is different when running the
/// transmogrifier on an open mission, in which case the open mission
/// has _OpenMission at the end.
const char* genesisMissionName( GenesisMissionContext* context, bool playerSpecific )
{
	if( context->zmap_info ) {
		return genesisMissionNameRaw( zmapInfoGetPublicName( context->zmap_info ),
									  context->zone_mission->desc.pcName,
									  !playerSpecific && context->zone_mission->desc.generationType != GenesisMissionGenerationType_PlayerMission );
	} else {
		return allocAddString( context->zone_mission->desc.pcName );
	}
}

/// Return an alloc'd name for a contact
const char* genesisContactName( GenesisMissionContext* context, GenesisMissionPrompt* prompt )
{
	return genesisContactNameRaw( (context->zmap_info ? zmapInfoGetPublicName( context->zmap_info ) : NULL),
								  context->zone_mission->desc.pcName,
								  SAFE_MEMBER( prompt, pcChallengeName ));
}

/// Return a ContactDef with name CONTACT_NAME.
ContactDef* genesisInternContactDef( GenesisMissionContext* context, const char* contact_name )
{
	int it;
	for( it = 0; it != eaSize( context->contacts_accum ); ++it ) {
		if( (*context->contacts_accum)[ it ]->name == contact_name ) {
			return (*context->contacts_accum)[ it ];
		}
	}

	{
		ContactDef* accum = StructCreate( parse_ContactDef );
		char path[ MAX_PATH ];

		accum->name = allocAddString( contact_name );
		accum->genesisZonemap = StructAllocString( zmapInfoGetPublicName( context->zmap_info ));
		accum->type = ContactType_SingleDialog;

		if( context->zmap_info ) {
			char noNameSpacePath[ MAX_PATH ];
			strcpy( path, zmapInfoGetFilename( context->zmap_info ));
			getDirectoryName( path );
			strcat( path, "/contacts" );
			resExtractNameSpace_s(path, NULL, 0, SAFESTR(noNameSpacePath));
			accum->scope = allocAddFilename( noNameSpacePath );
		} else {
			sprintf( path, "Maps/%s", context->project_prefix );
			accum->scope = allocAddFilename( path );
		}
		
		eaPush( context->contacts_accum, accum );
		return accum;
	}
}

/// Create the Optional Action entry for PROMPT, using VISIBLE-EXPR as
/// the condition which triggers visibility, and hook it up to show
/// anywhere in the map.
void genesisCreatePromptOptionalAction( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr )
{
	if( eaSize( &prompt->eaExternalMapNames ) == 0 ) {
		GenesisMissionRequirements* req = genesisInternRequirement( context );
		WorldOptionalActionVolumeEntry* entry = genesisCreatePromptOptionalActionEntry( context, prompt, visibleExpr );

		if (!req->params) {
			req->params = StructCreate(parse_GenesisProceduralObjectParams);
		}
		genesisProceduralObjectSetOptionalActionVolume( req->params );
		eaPush( &req->params->optionalaction_volume_properties->entries, entry );
	} else {
		int mapIt;
		int it;
		for( mapIt = 0; mapIt != eaSize( &prompt->eaExternalMapNames ); ++mapIt ) {
			const char* externalMapName = prompt->eaExternalMapNames[ mapIt ];
			
			ZoneMapEncounterInfo* zeni = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", externalMapName );
			char** volumeNames = SAFE_MEMBER( zeni, volume_logical_name );
			WorldInteractionPropertyEntry* entry = genesisCreatePromptInteractionEntry( context, prompt, visibleExpr );

			for( it = 0; it != eaSize( &volumeNames ); ++it ) {
				InteractableOverride* volumeInteractable = StructCreate( parse_InteractableOverride );
				volumeInteractable->pcInteractableName = allocAddString( volumeNames[ it ]);
				volumeInteractable->pcMapName = allocAddString( externalMapName );
				volumeInteractable->pPropertyEntry = StructClone( parse_WorldInteractionPropertyEntry, entry );

				eaPush( &context->root_mission_accum->ppInteractableOverrides, volumeInteractable );
			}
			
			StructDestroy( parse_WorldInteractionPropertyEntry, entry );
		}
	}
}

/// Create the Optional Action entry for PROMPT, using VISIBLE-EXPR as
/// the condition which triggers visibility.
///
/// Unlike genesisCreatePromptOptionalAction(), this does NOT hook up
/// the entry to any requirements, you must hook it up yourself.
WorldOptionalActionVolumeEntry* genesisCreatePromptOptionalActionEntry( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr )
{
	WorldOptionalActionVolumeEntry* accum = StructCreate( parse_WorldOptionalActionVolumeEntry );
	
	assert( prompt->bOptional );

	if( !prompt->bOptionalHideOnComplete ) {
		accum->visible_cond = (!nullStr( visibleExpr ) ? exprCreateFromString( visibleExpr, NULL ) : NULL);
	} else {
		GenesisMissionPrompt* hidePrompt;
		char* estr = NULL;

		if( nullStr( prompt->pcOptionalHideOnCompletePrompt )) {
			hidePrompt = prompt;
		} else {
			hidePrompt = genesisFindPrompt( context, prompt->pcOptionalHideOnCompletePrompt );

			if( !hidePrompt ) {
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->zone_mission, desc.pcName ), prompt->pcLayoutName ),
								   "Prompt should hide when prompt \"%s\" completes, but no such prompt exists.",
								   prompt->pcOptionalHideOnCompletePrompt );
			}
		}

		if( visibleExpr ) {
			estrPrintf( &estr, "(%s) and ", visibleExpr );
		}

		if( hidePrompt ) {
			char** completeBlockNames = genesisPromptBlockNames( context, prompt, true );
			int it;
			estrConcatf( &estr, "(" );
			for( it = 0; it != eaSize( &completeBlockNames ); ++it ) {
				estrConcatf( &estr, "%sHasRecentlyCompletedContactDialog(\"%s\",\"%s\") = 0",
							 (it ? " and " : ""),
							 genesisContactName( context, prompt ),
							 genesisSpecialDialogBlockNameTemp( hidePrompt->pcName, completeBlockNames[ it ]));
			}
			estrConcatf( &estr, ")" );
			eaDestroy( &completeBlockNames );
		} else {
			estrConcatf( &estr, "1" );
		}

		accum->visible_cond = exprCreateFromString( estr, NULL );
		estrDestroy( &estr );
	}
	genesisCreateMessage( context, &accum->display_name_msg, prompt->pcOptionalButtonText );
	accum->auto_execute = prompt->bOptionalAutoExecute;
	if( accum->auto_execute ) {
		accum->enabled_cond = exprCreateFromString( "not PlayerIsInCombat()", NULL );
	}
	accum->category_name = StructAllocString( prompt->pcOptionalCategoryName );
	accum->priority = prompt->eOptionalPriority;
	
	eaPush( &accum->actions.eaActions, genesisCreatePromptAction( context, prompt ));

	return accum;
}

/// Create the Optional Action entry for PROMPT, using VISIBLE-EXPR as
/// the condition which triggers visibility.
///
/// Unlike genesisCreatePromptOptionalAction(), this does NOT hook up
/// the entry to any requirements, you must hook it up yourself.
WorldInteractionPropertyEntry* genesisCreatePromptInteractionEntry( GenesisMissionContext* context, GenesisMissionPrompt* prompt, const char* visibleExpr )
{
	WorldInteractionPropertyEntry* accum = StructCreate( parse_WorldInteractionPropertyEntry );
	accum->pcInteractionClass = allocAddString( "CONTACT" );
	assert( prompt->bOptional );

	if( !prompt->bOptionalHideOnComplete ) {
		accum->pInteractCond = (!nullStr( visibleExpr ) ? exprCreateFromString( visibleExpr, NULL ) : NULL);
	} else {
		GenesisMissionPrompt* hidePrompt;
		char* estr = NULL;

		if( nullStr( prompt->pcOptionalHideOnCompletePrompt )) {
			hidePrompt = prompt;
		} else {
			hidePrompt = genesisFindPrompt( context, prompt->pcOptionalHideOnCompletePrompt );

			if( !hidePrompt ) {
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->zone_mission, desc.pcName ), prompt->pcLayoutName ),
								   "Prompt should hide when prompt \"%s\" completes, but no such prompt exists.",
								   prompt->pcOptionalHideOnCompletePrompt );
			}
		}

		if( visibleExpr ) {
			estrPrintf( &estr, "(%s) and ", visibleExpr );
		}
		
		if( prompt->bOptionalAutoExecute ) {
			estrConcatf( &estr, "not PlayerIsInCombat() and " );
		}

		if( hidePrompt ) {
			char** completeBlockNames = genesisPromptBlockNames( context, hidePrompt, true );
			int it;

			estrConcatf( &estr, "(" );
			for( it = 0; it != eaSize( &completeBlockNames ); ++it ) {
				estrConcatf( &estr, "%sHasRecentlyCompletedContactDialog(\"%s\",\"%s\") = 0",
							 (it ? " and " : ""),
							 genesisContactName( context, prompt ),
							 genesisSpecialDialogBlockNameTemp( hidePrompt->pcName, completeBlockNames[ it ]));
			}
			estrConcatf( &estr, ")" );
			eaDestroy( &completeBlockNames );
		} else {
			estrConcatf( &estr, "1" );
		}

		accum->pInteractCond = exprCreateFromString( estr, NULL );
		estrDestroy( &estr );
	}

	accum->pTextProperties = StructCreate( parse_WorldTextInteractionProperties );
	genesisCreateMessage( context, &accum->pTextProperties->interactOptionText, prompt->pcOptionalButtonText );
	
	accum->bAutoExecute = prompt->bOptionalAutoExecute;
	accum->pcCategoryName = StructAllocString( prompt->pcOptionalCategoryName );
	accum->iPriority = prompt->eOptionalPriority;

	accum->pContactProperties = StructCreate( parse_WorldContactInteractionProperties );
	SET_HANDLE_FROM_STRING( g_ContactDictionary, genesisContactName( context, prompt ),
							accum->pContactProperties->hContactDef );
	accum->pContactProperties->pcDialogName = StructAllocString( prompt->pcName );

	return accum;
}

/// Return a ContactMissionOffer for MISSION-NAME.
///
/// If IS-RETURN-ONLY, then this Contact will not be shared with any
/// granting mission offers.
ContactMissionOffer* genesisInternMissionOffer( GenesisMissionContext* context, const char* mission_name, bool isReturnOnly )
{
	char buffer[256];
	char* fix = NULL;
	const char* contactName;

	sprintf( buffer, "%s%s",
			 genesisContactName( context, NULL ),
			 (isReturnOnly ? "_Return" : "") );

	if( resFixName( buffer, &fix )) {
		contactName = allocAddString( fix );
		estrDestroy( &fix );
	} else {
		contactName = allocAddString( buffer );
	}

	{
		ContactDef* contactAccum = genesisInternContactDef( context, contactName );
		int it;
		ContactMissionOffer** eaOfferList = NULL;

		contact_GetMissionOfferList(contactAccum, NULL, &eaOfferList);

		contactAccum->type = ContactType_List;
		
		for( it = 0; it != eaSize( &eaOfferList ); ++it ) {
			ContactMissionOffer* pOffer = eaOfferList[ it ];
			if( stricmp( REF_STRING_FROM_HANDLE( pOffer->missionDef ), genesisMissionName( context, true )) == 0 ) {
				eaDestroy(&eaOfferList);
				return pOffer;
			}
		}

		if(eaOfferList)
			eaDestroy(&eaOfferList);

		{
			ContactMissionOffer* offerAccum = StructCreate( parse_ContactMissionOffer );
			SET_HANDLE_FROM_STRING( g_MissionDictionary, genesisMissionName( context, true ), offerAccum->missionDef );
			eaPush( &contactAccum->offerList, offerAccum );
			return offerAccum;
		}
	}
}

/// Return a requirement for the mission.
GenesisMissionRequirements* genesisInternRequirement( GenesisMissionContext* context )
{
	assert( context->req_accum );

	return context->req_accum;
}

/// Return the extra volume named VOLUME-NAME.
///
/// If no such extra volume exists, create one.
GenesisMissionExtraVolume* genesisInternExtraVolume( GenesisMissionContext* context, const char* volume_name )
{
	GenesisMissionRequirements* req = genesisInternRequirement( context );
	int it;
	for( it = 0; it != eaSize( &req->extraVolumes ); ++it ) {
		GenesisMissionExtraVolume* extraVolume = req->extraVolumes[ it ];
		if( stricmp( volume_name, extraVolume->volumeName ) == 0 ) {
			return extraVolume;
		}
	}

	{
		GenesisMissionExtraVolume* accum = StructCreate( parse_GenesisMissionExtraVolume );
		accum->volumeName = StructAllocString( volume_name );
		eaPush( &req->extraVolumes, accum );
		return accum;
	}
}

/// Return a room requirement params for the room with name ROOM_NAME.
GenesisProceduralObjectParams* genesisInternRoomRequirementParams( GenesisMissionContext* context, const char* layout_name, const char* room_name )
{
	assert( context->req_accum );

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->roomRequirements ); ++it ) {
			GenesisMissionRoomRequirements* roomReq = context->req_accum->roomRequirements[ it ]; 
			if( stricmp( roomReq->layoutName, layout_name ) == 0 && stricmp( roomReq->roomName, room_name ) == 0 ) {
				return roomReq->params;
			}
		}
	}

	{
		GenesisMissionRoomRequirements* newRoomReq = StructCreate( parse_GenesisMissionRoomRequirements );
		newRoomReq->layoutName = StructAllocString( layout_name );
		newRoomReq->roomName = StructAllocString( room_name );
		newRoomReq->params = StructCreate( parse_GenesisProceduralObjectParams );
		eaPush( &context->req_accum->roomRequirements, newRoomReq );
		return newRoomReq->params;
	}
}

/// Return a room requirement for the room with name ROOM_NAME.
GenesisMissionRoomRequirements* genesisInternRoomRequirements( GenesisMissionContext* context, const char* layout_name, const char* room_name )
{
	assert( context->req_accum );

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->roomRequirements ); ++it ) {
			GenesisMissionRoomRequirements* roomReq = context->req_accum->roomRequirements[ it ]; 
			if( stricmp( roomReq->layoutName, layout_name ) == 0 && stricmp( roomReq->roomName, room_name ) == 0 ) {
				return roomReq;
			}
		}
	}

	{
		GenesisMissionRoomRequirements* newRoomReq = StructCreate( parse_GenesisMissionRoomRequirements );
		newRoomReq->layoutName = StructAllocString( layout_name );
		newRoomReq->roomName = StructAllocString( room_name );
		newRoomReq->params = StructCreate( parse_GenesisProceduralObjectParams );
		eaPush( &context->req_accum->roomRequirements, newRoomReq );
		return newRoomReq;
	}
}

/// Return a room requirement for the starting room.
GenesisProceduralObjectParams* genesisInternStartRoomRequirementParams( GenesisMissionContext* context )
{
	return genesisInternRoomRequirementParams( context, context->zone_mission->desc.startDescription.pcStartLayout, context->zone_mission->desc.startDescription.pcStartRoom );
}

/// Return a challenge requirement for the challenge with name
/// CHALLENGE_NAME.
GenesisInstancedObjectParams* genesisInternChallengeRequirementParams( GenesisMissionContext* context, const char* challenge_name )
{
	assert( context->req_accum );

	// MJF TODO: remove reference to syntactical recognition of shared
	// challenges
	if( strStartsWith( challenge_name, "Shared_" )) {
		static GenesisMissionChallengeRequirements req = { 0 };

		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge_name, SAFE_MEMBER( context->zone_mission, desc.pcName ), NULL ),
						   "Trying to specify mission-specific requirements is not supported for shared challenges." );
		StructReset( parse_GenesisMissionChallengeRequirements, &req );
	}

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->challengeRequirements ); ++it ) {
			GenesisMissionChallengeRequirements* challengeReq = context->req_accum->challengeRequirements[ it ];
			if( stricmp( challengeReq->challengeName, challenge_name ) == 0 ) {
				if (!challengeReq->params)
					challengeReq->params = StructCreate( parse_GenesisInstancedObjectParams );
				return challengeReq->params;
			}
		}
	}

	{
		GenesisMissionChallengeRequirements* newChallengeReq = StructCreate( parse_GenesisMissionChallengeRequirements );
		newChallengeReq->challengeName = StructAllocString( challenge_name );
		newChallengeReq->params = StructCreate( parse_GenesisInstancedObjectParams );
		eaPush( &context->req_accum->challengeRequirements, newChallengeReq );
		return newChallengeReq->params;
	}
}

/// Return a challenge interact requirement for the challenge with name
/// CHALLENGE_NAME.
GenesisInteractObjectParams* genesisInternInteractRequirementParams( GenesisMissionContext* context, const char* challenge_name )
{
	assert( context->req_accum );

	// MJF TODO: remove reference to syntaticall recognition of shared
	// challenges
	if( strStartsWith( challenge_name, "Shared_" )) {
		static GenesisMissionChallengeRequirements req = { 0 };

		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge_name, SAFE_MEMBER( context->zone_mission, desc.pcName ), NULL ),
						   "Trying to specify mission-specific requirements is not supported for shared challenges." );
		StructReset( parse_GenesisMissionChallengeRequirements, &req );
	}

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->challengeRequirements ); ++it ) {
			GenesisMissionChallengeRequirements* challengeReq = context->req_accum->challengeRequirements[ it ];
			if( stricmp( challengeReq->challengeName, challenge_name ) == 0 ) {
				if (!challengeReq->interactParams)
					challengeReq->interactParams = StructCreate( parse_GenesisInteractObjectParams );
				return challengeReq->interactParams;
			}
		}
	}

	{
		GenesisMissionChallengeRequirements* newChallengeReq = StructCreate( parse_GenesisMissionChallengeRequirements );
		newChallengeReq->challengeName = StructAllocString( challenge_name );
		newChallengeReq->interactParams = StructCreate( parse_GenesisInteractObjectParams );
		newChallengeReq->interactParams->bDisallowVolume = true;
		eaPush( &context->req_accum->challengeRequirements, newChallengeReq );
		return newChallengeReq->interactParams;
	}
}

/// Return a volume requirement for the challenge with name
/// CHALLENGE_NAME.
GenesisProceduralObjectParams* genesisInternVolumeRequirementParams( GenesisMissionContext* context, const char* challenge_name )
{
	assert( context->req_accum );

	// MJF TODO: remove reference to syntaticall recognition of shared
	// challenges
	if( strStartsWith( challenge_name, "Shared_" )) {
		static GenesisMissionChallengeRequirements req = { 0 };

		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge_name, SAFE_MEMBER( context->zone_mission, desc.pcName ), NULL ),
			"Trying to specify mission-specific requirements is not supported for shared challenges." );
		StructReset( parse_GenesisMissionChallengeRequirements, &req );
	}

	{
		int it;
		for( it = 0; it != eaSize( &context->req_accum->challengeRequirements ); ++it ) {
			GenesisMissionChallengeRequirements* challengeReq = context->req_accum->challengeRequirements[ it ];
			if( stricmp( challengeReq->challengeName, challenge_name ) == 0 ) {
				if (!challengeReq->volumeParams)
					challengeReq->volumeParams = StructCreate( parse_GenesisProceduralObjectParams );
				return challengeReq->volumeParams;
			}
		}
	}

	{
		GenesisMissionChallengeRequirements* newChallengeReq = StructCreate( parse_GenesisMissionChallengeRequirements );
		newChallengeReq->challengeName = StructAllocString( challenge_name );
		newChallengeReq->volumeParams = StructCreate( parse_GenesisProceduralObjectParams );
		eaPush( &context->req_accum->challengeRequirements, newChallengeReq );
		return newChallengeReq->volumeParams;
	}
}

/// Return an interactable property entry that should be filled out
/// for a requirement for CHALLENGE_NAME.
WorldInteractionPropertyEntry* genesisCreateInteractableChallengeRequirement( GenesisMissionContext* context, const char* challenge_name )
	
{
	GenesisInteractObjectParams* params = genesisInternInteractRequirementParams( context, challenge_name );
	WorldInteractionPropertyEntry* entry = StructCreate( parse_WorldInteractionPropertyEntry );

	eaPush( &params->eaInteractionEntries, entry );
	return entry;
}

/// Return an interactable property entry that should be filled out
/// for a requirement for CHALLENGE_NAME's volume.
WorldInteractionPropertyEntry* genesisCreateInteractableChallengeVolumeRequirement( GenesisMissionContext* context, const char* challenge_name )
	
{
	GenesisProceduralObjectParams* params = genesisInternVolumeRequirementParams( context, challenge_name );
	WorldInteractionPropertyEntry* entry = StructCreate( parse_WorldInteractionPropertyEntry );

	if (!params->interaction_properties)
		params->interaction_properties = StructCreate(parse_WorldInteractionProperties);
	eaPush( &params->interaction_properties->eaEntries, entry );
	return entry;
}

/// Return a dialog block with text DIALOG-TEXT
DialogBlock* genesisCreateDialogBlock( GenesisMissionContext* context, char* dialogText, const char* astrAnimList )
{
	DialogBlock* accum = StructCreate( parse_DialogBlock );
	if( nullStr( dialogText )) {
		dialogText = " ";
	}
	
	genesisCreateMessage( context, &accum->displayTextMesg, dialogText );
	if( astrAnimList ) {
		SET_HANDLE_FROM_STRING( "AIAnimList", astrAnimList, accum->hAnimList );
	}

	return accum;
}

void genesisRefSystemUpdate( DictionaryHandleOrName dict, const char* key, void* obj )
{
	void* oldObj = RefSystem_ReferentFromString( dict, key );

	if( oldObj ) {
		RefSystem_MoveReferent( obj, oldObj );
	} else {
		RefSystem_AddReferent( dict, key, obj );
	}
}

void genesisRunValidate( DictionaryHandleOrName dict, const char* key, void* obj )
{
	resRunValidate(RESVALIDATE_POST_TEXT_READING, dict, key, obj, -1, NULL);
	resRunValidate(RESVALIDATE_POST_BINNING, dict, key, obj, -1, NULL);
	resRunValidate(RESVALIDATE_FINAL_LOCATION, dict, key, obj, -1, NULL);
}

/// Call ParserWriteTextFileFromDictionary, and display error dialogs
/// as appropriate.
void genesisWriteTextFileFromDictionary( const char* filename, DictionaryHandleOrName dict )
{
	if( filename && !ParserWriteTextFileFromDictionary( filename, dict, 0, 0 )) {
		ErrorFilenamef( filename, "Unable to write out file.  Is it not checked out?" );
	}
}

/// Create a DisplayMessage containing DEFAULT-STRING.
///
/// This also applies substitution rules, which take substring like
/// {Thing} and replaces it with the name for Thing.
void genesisCreateMessage( GenesisMissionContext* context, DisplayMessage* dispMessage, const char* defaultString )
{
	if( defaultString ) {
		char* estrDefaultString = estrCreateFromStr( defaultString );

		// {Genesis.MissionName}
		if( context && !context->is_ugc ) {
			estrReplaceOccurrences_CaseInsensitive( &estrDefaultString, "{Genesis.MissionName}", context->zone_mission->desc.pcDisplayName );
		}

		// {Genesis.MapName}
		if( context && !context->is_ugc ) {
			DisplayMessage* mapNameDispMsg = zmapInfoGetDisplayNameMessage( context->zmap_info );
			const char* mapName = NULL;

			if( mapNameDispMsg->pEditorCopy ) {
				mapName = SAFE_MEMBER( mapNameDispMsg->pEditorCopy, pcDefaultString );
			} else {
				mapName = SAFE_MEMBER( GET_REF( mapNameDispMsg->hMessage ), pcDefaultString );
			}
		
			if( !mapName ) {
				mapName = zmapInfoGetPublicName( context->zmap_info );
			}

			estrReplaceOccurrences_CaseInsensitive( &estrDefaultString, "{Genesis.MapName}", mapName );
			estrReplaceOccurrences_CaseInsensitive( &estrDefaultString, "{Genesis.SystemName}", mapName );
		}

		// prevent macroexpansion of langCreateMessage on this ONE line.
		if(!dispMessage->pEditorCopy)
			dispMessage->pEditorCopy = (langCreateMessage)( NULL, NULL, NULL, estrDefaultString );
		else
			dispMessage->pEditorCopy->pcDefaultString = StructAllocString(estrDefaultString);

		dispMessage->bEditorCopyIsServer = true;

		estrDestroy( &estrDefaultString );
	}
}

static void genesisPushRoomNames( char* missionName, GenesisZoneMapRoom** rooms, GenesisZoneMapPath** paths, char*** nameList )
{
	int it;
	for( it = 0; it != eaSize( &rooms ); ++it ) {
		char buffer[ 256 ];
		sprintf( buffer, "%s_%s", missionName, rooms[ it ]->room.name );
		eaPush( nameList, strdup( buffer ));
	}
	for( it = 0; it != eaSize( &paths ); ++it ) {
		char buffer[ 256 ];
		sprintf( buffer, "%s_%s", missionName, paths[ it ]->path.name );
		eaPush( nameList, strdup( buffer ));
	}
}

void genesisPushAllRoomNames( GenesisMissionContext* context, char*** nameList )
{
	int i;
	char* missionName = context->zone_mission->desc.pcName;
	
	for ( i=0; i < eaSize(&context->genesis_data->solar_systems); i++ ) {
		int pointListIt;
		int pointIt;
		GenesisSolSysZoneMap *solar_system = context->genesis_data->solar_systems[ i ];
		GenesisShoebox* shoebox = &solar_system->shoebox;
		for( pointListIt = 0; pointListIt != eaSize( &shoebox->point_lists ); ++pointListIt ) {
			ShoeboxPointList* pointList = solar_system->shoebox.point_lists[ pointListIt ];
			for( pointIt = 0; pointIt != eaSize( &pointList->points ); ++pointIt ) {
				ShoeboxPoint* point = pointList->points[ pointIt ];
				char buffer[ 256 ];
				sprintf( buffer, "%s_%s", missionName, point->name  );
				eaPush( nameList, strdup( buffer ));
			}
		}
	}
	
	for ( i=0; i < eaSize(&context->genesis_data->genesis_interiors); i++ )
	{
		GenesisZoneMapRoom** rooms = context->genesis_data->genesis_interiors[ i ]->rooms;
		GenesisZoneMapPath** paths = context->genesis_data->genesis_interiors[ i ]->paths;
		genesisPushRoomNames(missionName, rooms, paths, nameList);
	}
	if( context->genesis_data->genesis_exterior ) {
		GenesisZoneMapRoom** rooms = context->genesis_data->genesis_exterior->rooms;
		GenesisZoneMapPath** paths = context->genesis_data->genesis_exterior->paths;
		genesisPushRoomNames(missionName, rooms, paths, nameList);
	}
}

#endif

#ifndef NO_EDITORS

void genesisTransmogrifyWhenFixup( GenesisTransmogrifyMissionContext* context, GenesisWhen *when, GenesisRuntimeErrorContext *error_context )
{
	int it;
	for( it = 0; it != eaSize( &when->eaChallengeNames ); ++it ) {
		char* oldChallengeName = when->eaChallengeNames[ it ];
		bool isShared;

		if( genesisFindChallenge( context->map_desc, context->mission_desc, oldChallengeName, &isShared )) {
			char newChallengeName[ 256 ];
			sprintf( newChallengeName, "%s_%s",
					 (isShared ? "Shared" : context->mission_desc->zoneDesc.pcName),
					 oldChallengeName );
			when->eaChallengeNames[ it ] = StructAllocString( newChallengeName );
			StructFreeString( oldChallengeName );
		} else {
			genesisRaiseError( GENESIS_ERROR, error_context,
							   "When references challenge \"%s\", but it does not exist.",
							   when->eaChallengeNames[ it ]);
			eaRemove( &when->eaChallengeNames, it );
			--it;
		}
	}

	if( when->pcPromptChallengeName ) {
		genesisChallengeNameFixup( context, &when->pcPromptChallengeName );
	}

}

/// Transmogrify a prompt
void genesisTransmogrifyPromptFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionPrompt* prompt )
{
	genesisTransmogrifyWhenFixup(context, &prompt->showWhen, genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER(context->mission_desc, zoneDesc.pcName ), prompt->pcLayoutName ));

	if( !nullStr( prompt->pcChallengeName )) {
		genesisChallengeNameFixup( context, &prompt->pcChallengeName );
	}
}

/// Transmogrify an FSM
void genesisTransmogrifyFSMFixup(GenesisTransmogrifyMissionContext* context, GenesisFSM *gfsm)
{
	if(!nullStr(gfsm->pcChallengeLogicalName))
	{
		genesisChallengeNameFixup(context, &gfsm->pcChallengeLogicalName);
	}
}

/// Transmogrify a portal
void genesisTransmogrifyPortalFixup( GenesisTransmogrifyMissionContext* context, GenesisMissionPortal *portal )
{
	genesisTransmogrifyWhenFixup(context, &portal->when, genesisMakeTempErrorContextPortal( portal->pcName, SAFE_MEMBER(context->mission_desc, zoneDesc.pcName ), portal->pcStartLayout ));
}

/// Create a prompt for a GenesisMissionPrompt
void genesisCreatePrompt( GenesisMissionContext* context, GenesisMissionPrompt* prompt )
{
	const char* contactName = genesisContactName( context, prompt );
	ContactDef* defAccum = genesisInternContactDef( context, contactName );
	
	SpecialDialogBlock* primarySpecialDialog = genesisCreatePromptBlock( context, prompt, -1 );
	SpecialDialogBlock** specialDialogs = NULL;
	{
		int it;
		for( it = 0; it != eaSize( &prompt->namedBlocks ); ++it ) {
			eaPush( &specialDialogs, genesisCreatePromptBlock( context, prompt, it ));
		}
	}

	if( prompt->showWhen.type != GenesisWhen_Manual )
	{
		GenesisRuntimeErrorContext* debugContext
			= genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->zone_mission, desc.pcName ), prompt->pcLayoutName );
		char* exprText;

		// Prompts that are are objective complete of a single
		// objective should actually be during the AfterPrompt_
		// objective.
		//
		// NOTE: may be better to fix up all these prompts in a
		// preprocessing pass.
		{
			GenesisWhen showWhen = { 0 };
			StructCopyAll( parse_GenesisWhen, &prompt->showWhen, &showWhen );

			if( showWhen.type == GenesisWhen_ObjectiveComplete && eaSize( &showWhen.eaObjectiveNames ) == 1 ) {
				char afterPromptObjectiveName[ 256 ];

				sprintf( afterPromptObjectiveName, "AfterPrompt_%s", showWhen.eaObjectiveNames[ 0 ]);
				showWhen.type = GenesisWhen_ObjectiveInProgress;
				StructCopyString( &showWhen.eaObjectiveNames[ 0 ], afterPromptObjectiveName );
			}
			
			exprText = genesisWhenExprText( context, &showWhen, debugContext, "ShowWhen", false );
			StructDeInit( parse_GenesisWhen, &showWhen );
		}
		
		if( prompt->pcExternalContactName ) {
			primarySpecialDialog->bUsesLocalCondExpression = true;
			primarySpecialDialog->pCondition = exprCreateFromString( exprText, NULL );
		} else if( prompt->pcChallengeName ) {
			if( eaSize( &prompt->eaExternalMapNames ) == 0 ) { 
				GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, prompt->pcChallengeName );
				GenesisMissionPromptExprPair* pair;
				if (!params->pContact)
				{
					params->pContact = StructCreate(parse_GenesisMissionContactRequirements);
				}
				pair = StructCreate( parse_GenesisMissionPromptExprPair );
				pair->name = StructAllocString( prompt->pcName );
				pair->exprText = StructAllocString( exprText );
				eaPush(&params->pContact->eaPrompts, pair);
				StructCopyString(&params->pContact->pcContactFileName, contactName);
			} else {
				InteractableOverride* contactProp = StructCreate( parse_InteractableOverride );
				WorldInteractionPropertyEntry* entry = StructCreate( parse_WorldInteractionPropertyEntry );
				eaPush( &context->root_mission_accum->ppInteractableOverrides, contactProp );
				contactProp->pPropertyEntry = entry;

				contactProp->pcMapName = allocAddString( prompt->eaExternalMapNames[ 0 ]);
				contactProp->pcInteractableName = allocAddString( prompt->pcChallengeName );

				// interaction properties
				entry->pcInteractionClass = allocAddString( "CONTACT" );
				entry->pContactProperties = StructCreate( parse_WorldContactInteractionProperties );
				SET_HANDLE_FROM_STRING( g_ContactDictionary, genesisContactName( context, prompt ),
										entry->pContactProperties->hContactDef );
				entry->pContactProperties->pcDialogName = StructAllocString( prompt->pcName );
				entry->pTextProperties = StructCreate( parse_WorldTextInteractionProperties );
				genesisCreateMessage( context, &entry->pTextProperties->interactOptionText, "Click Me" );		
				entry->pInteractCond = exprCreateFromString( exprText, NULL );

				if( eaSize( &prompt->eaExternalMapNames ) > 1 ) {
					genesisRaiseError( GENESIS_ERROR, debugContext,
									   "Trying to set interaction properties "
									   "on a specific external contact with "
									   "multiple maps specified." );
				}
			}
		} else {
			if( !prompt->bOptional ) {
				// Making it an optional action prevents prompts from not
				// showing due to there already being a prompt up.  This is
				// better because prompts may be necesarry for missions to
				// work.
				//
				// One exception for UGC -- if this is the start prompt for an
				// external map, we'll assume wherever you got the
				// mission is a safe area.
				if( !exprText && !context->zmap_info ) {
					eaPush( &context->root_mission_accum->ppOnStartActions, genesisCreatePromptAction( context, prompt ));
				} else {
					GenesisMissionPrompt promptAsOptional = { 0 };
					StructCopyAll( parse_GenesisMissionPrompt, prompt, &promptAsOptional );
					promptAsOptional.bOptional = true;
					promptAsOptional.pcOptionalButtonText = StructAllocString( SAFE_MEMBER( context->config, fallback_prompt_text ));
					promptAsOptional.bOptionalAutoExecute = true;
					promptAsOptional.bOptionalHideOnComplete = true;
					promptAsOptional.eOptionalPriority = WorldOptionalActionPriority_Low;

					genesisCreatePromptOptionalAction( context, &promptAsOptional, exprText );

					StructDeInit( parse_GenesisMissionPrompt, &promptAsOptional );
				}
			} else {
				genesisCreatePromptOptionalAction( context, prompt, exprText );
			}
		}

		estrDestroy( &exprText );
	}

	if( prompt->pcExternalContactName ) {
		SpecialDialogOverride* override = StructCreate( parse_SpecialDialogOverride );
		override->pcContactName = prompt->pcExternalContactName;
		override->pSpecialDialog = primarySpecialDialog;
		eaPush( &context->root_mission_accum->ppSpecialDialogOverrides, override );

		{
			int otherIt;
			for( otherIt = 0; otherIt != eaSize( &specialDialogs ); ++otherIt ) {
				SpecialDialogBlock* otherDialog = specialDialogs[ otherIt ];
				SpecialDialogOverride* otherOverride = StructCreate( parse_SpecialDialogOverride );

				otherDialog->bUsesLocalCondExpression = true;
				otherDialog->pCondition = exprCreateFromString( "0", NULL );
				otherOverride->pcContactName = prompt->pcExternalContactName;
				otherOverride->pSpecialDialog = otherDialog;
				eaPush( &context->root_mission_accum->ppSpecialDialogOverrides, otherOverride );
			}
		}
	} else {
		eaPush( &defAccum->specialDialog, primarySpecialDialog );
		eaPushEArray( &defAccum->specialDialog, &specialDialogs );
	}
	
	eaDestroy( &specialDialogs );
}

SpecialDialogBlock* genesisCreatePromptBlock( GenesisMissionContext* context, GenesisMissionPrompt* prompt, int blockIndex )
{
	GenesisMissionPromptBlock* block = (blockIndex < 0 ? &prompt->sPrimaryBlock : prompt->namedBlocks[ blockIndex ]);
	SpecialDialogBlock* dialogAccum = StructCreate( parse_SpecialDialogBlock );

	dialogAccum->name = allocAddString( genesisSpecialDialogBlockNameTemp( prompt->pcName, block->name ));
	genesisMissionCostumeToContactCostume( &block->costume, &dialogAccum->costumePrefs );
	dialogAccum->pchHeadshotStyleOverride = allocAddString( block->pchHeadshotStyle );
	COPY_HANDLE( dialogAccum->hCutSceneDef, block->hCutsceneDef );
	dialogAccum->eIndicator = SpecialDialogIndicator_Important;
	dialogAccum->eFlags = block->eDialogFlags;
	dialogAccum->bDelayIfInCombat = true;
	
	genesisCreateMessage( context, &dialogAccum->displayNameMesg, block->pcTitleText );

	{
		int it;
		for( it = 0; it != eaSize( &block->eaBodyText ); ++it ) {
			eaPush( &dialogAccum->dialogBlock, genesisCreateDialogBlock( context, block->eaBodyText[ it ], REF_STRING_FROM_HANDLE( block->hAnimList )));

			if( it == 0 ) {
				int val = StaticDefineIntGetInt( ContactAudioPhrasesEnum, block->pcPhrase );
				if( val >= 0 ) {
					dialogAccum->dialogBlock[0]->ePhrase = val;
				}
			}
		}
	}

	{
		int actionIt;
		for( actionIt = 0; actionIt != eaSize( &block->eaActions ); ++actionIt ) {
			GenesisMissionPromptAction* promptAction = block->eaActions[ actionIt ];
			SpecialDialogAction* actionAccum = StructCreate( parse_SpecialDialogAction );

			{
				char* estrText = NULL;
				estrPrintf( &estrText, "%s", promptAction->pcText ? promptAction->pcText : "Continue" );
				if( promptAction->astrStyleName ) {
					estrInsertf( &estrText, 0, "<font style=%s>", promptAction->astrStyleName );
					estrConcatf( &estrText, "</font>" );
				}
				genesisCreateMessage( context, &actionAccum->displayNameMesg, estrText );
				estrDestroy( &estrText );
			}
			{
				GenesisRuntimeErrorContext* debugContext
					= genesisMakeTempErrorContextPrompt( prompt->pcName, NULL, SAFE_MEMBER( context->zone_mission, desc.pcName ), prompt->pcLayoutName );
				char* whenText = genesisWhenExprText( context, &promptAction->when, debugContext, "When", false );
				if( whenText ) {
					actionAccum->condition = exprCreateFromString( whenText, NULL );
				}
			}
			
			if(   stricmp( promptAction->pcNextPromptName, "MissionReturn" ) == 0
				  && stricmp( GetShortProductName(), "ST" ) == 0 ) {
				WorldGameActionProperties* returnAction = StructCreate( parse_WorldGameActionProperties );
				
				returnAction->eActionType = WorldGameActionType_SendNotification;
				returnAction->pSendNotificationProperties = StructCreate( parse_WorldSendNotificationActionProperties );
				returnAction->pSendNotificationProperties->pchNotifyType = StaticDefineIntRevLookup(NotifyTypeEnum, kNotifyType_RequestLeaveMap);
				genesisCreateMessage( context, &returnAction->pSendNotificationProperties->notifyMsg, "XXX" );
				eaPush( &actionAccum->actionBlock.eaActions, returnAction );
			} else {
				if( !nullStr( promptAction->pcNextBlockName )) {
					char buffer[ 256 ];
					assert( nullStr( promptAction->pcNextPromptName ));

					sprintf( buffer, "%s_%s", prompt->pcName, promptAction->pcNextBlockName );

					if( prompt->pcExternalContactName ) {
						char overrideBuffer[ 256 ];
						sprintf( overrideBuffer, "%s/%s", genesisMissionName( context, false ), buffer );
						actionAccum->dialogName = allocAddString( overrideBuffer );
					} else {
						actionAccum->dialogName = allocAddString( buffer );
					}
				} else {
					GenesisMissionPrompt* nextPrompt = genesisFindPrompt( context, promptAction->pcNextPromptName );
					
					if( nextPrompt ) {
						if( prompt->pcExternalContactName && prompt == nextPrompt ) {
							char overrideBuffer[ 256 ];
							sprintf( overrideBuffer, "%s/%s", genesisMissionName( context, false ), promptAction->pcNextPromptName );
							actionAccum->dialogName = allocAddString( overrideBuffer );
						} else {
							actionAccum->dialogName = allocAddString( promptAction->pcNextPromptName );
						}
					}
				}
			
				StructCopyAll( parse_WorldGameActionBlock, &promptAction->actionBlock, &actionAccum->actionBlock );
				if( promptAction->bGrantMission ) {
					WorldGameActionProperties* grantAccum = StructCreate( parse_WorldGameActionProperties );
					grantAccum->eActionType = WorldGameActionType_GrantMission;
					grantAccum->pGrantMissionProperties = StructCreate( parse_WorldGrantMissionActionProperties );
					SET_HANDLE_FROM_STRING( g_MissionDictionary, genesisMissionName( context, true ),
											grantAccum->pGrantMissionProperties->hMissionDef );
					eaPush( &actionAccum->actionBlock.eaActions, grantAccum );
				}
			}
			
			actionAccum->bSendComplete = !promptAction->bDismissAction && nullStr( promptAction->pcNextBlockName );

			if( promptAction->enabledCheckedAttrib ) {
				char* attribText = genesisCheckedAttribText( context, promptAction->enabledCheckedAttrib, genesisMakeTempErrorContextPrompt( prompt->pcName, dialogAccum->name, SAFE_MEMBER( context->zone_mission, desc.pcName ), NULL ), "EnabledCheckedAttrib", true );

				if( attribText ) {
					GenesisConfigCheckedAttrib* checkedAttrib = genesisCheckedAttrib( promptAction->enabledCheckedAttrib->name );
			
					actionAccum->canChooseCondition = exprCreateFromString( attribText, NULL );

					if( checkedAttrib && checkedAttrib->displayName ) {
						char* estrBuffer = NULL;
						estrPrintf( &estrBuffer, "(%s) %s", checkedAttrib->displayName, actionAccum->displayNameMesg.pEditorCopy->pcDefaultString );
						StructCopyString( &actionAccum->displayNameMesg.pEditorCopy->pcDefaultString, estrBuffer );
						estrDestroy( &estrBuffer );
					}
				}

				estrDestroy( &attribText );
			}
			
			eaPush( &dialogAccum->dialogActions, actionAccum );
		}
	}

	return dialogAccum;
}

void genesisBucketFSM(GenesisMissionContext *context, ObjectFSMData ***dataArray, GenesisFSM *fsm)
{
	if(!nullStr(fsm->pcChallengeLogicalName))
	{
		GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, fsm->pcChallengeLogicalName );
		char *name = NULL;
		ObjectFSMData *data = NULL;
			
		FOR_EACH_IN_EARRAY(*dataArray, ObjectFSMData, test)
		{
			if(stricmp(test->challengeName, fsm->pcChallengeLogicalName ) == 0)
			{
				data = test;
				break;
			}
		}
		FOR_EACH_END;

		if(!data)
		{
			data = calloc(1, sizeof(ObjectFSMData));
			data->challengeName = strdup( fsm->pcChallengeLogicalName );
			eaPush(dataArray, data);
		}

		eaPush(&data->fsms, fsm);
	}
}

FSMState* fsmCreateState(FSM *fsm, const char *stateName)
{
	FSMState *state = StructCreate(parse_FSMState);
	state->name = allocAddString(stateName);

	eaPush(&fsm->states, state);

	return state;
}

void fsmStateSetAction(FSM *fsm, FSMState *state, const char* expr)
{
	if(!nullStr(expr))
		state->action = exprCreateFromString(expr, fsm->fileName);
}

void fsmStateSetOnEntry(FSM *fsm, FSMState *state, const char* onEntry, const char* onEntryFirst)
{
	if(!nullStr(onEntry))
		state->onEntry = exprCreateFromString(onEntry, fsm->fileName);
	if(!nullStr(onEntryFirst))
		state->onFirstEntry = exprCreateFromString(onEntryFirst, fsm->fileName);
}

FSMTransition* fsmStateAddTransition(FSM *fsm, FSMState *state, FSMState *target, const char* cond, const char* action)
{
	FSMTransition *transition = StructCreate(parse_FSMTransition);

	if(!nullStr(cond))
		transition->expr = exprCreateFromString(cond, fsm->fileName);
	if(!nullStr(action))
		transition->action = exprCreateFromString(action, fsm->fileName);
	transition->targetName = (char*)target->name;

	eaPush(&state->transitions, transition);

	return transition;
}

S32 genesisSortFSMs(GenesisMissionContext *context, ObjectFSMData *data)
{
	int curObj = 0;
	int matched0th = false;
	int curFSM;
	ObjectFSMTempData *tmpData = NULL;
	
	for(curFSM=0; curFSM<eaSize(&data->fsms); curFSM++)
	{
		GenesisFSM *fsm = data->fsms[curFSM];

		if(fsm->activeWhen.type==GenesisWhen_MapStart)
		{
			tmpData = calloc(1, sizeof(ObjectFSMTempData));
			tmpData->fsm = fsm;

			if(curObj==0)
				matched0th = true;

			eaPush(&data->fsmsAndStates, tmpData);
		}
	}

	curObj = 0;
	for(curObj=0; curObj<eaSize(&context->zone_mission->desc.eaObjectives); curObj++)
	{
		int i;
		const char* objName = context->zone_mission->desc.eaObjectives[curObj]->pcName;
		int dontPush = false;

		tmpData = NULL;
		for(curFSM=0; curFSM<eaSize(&data->fsms); curFSM++)
		{
			GenesisFSM *fsm = data->fsms[curFSM];

			if(!eaSize(&fsm->activeWhen.eaObjectiveNames))
				continue;

			for(i=0; i<eaSize(&fsm->activeWhen.eaObjectiveNames); i++)
			{
				if(!stricmp(fsm->activeWhen.eaObjectiveNames[i], objName))
				{
					if(i!=0)  // Only push one for the initial
						dontPush = true;
					else
					{
						tmpData = calloc(1, sizeof(ObjectFSMTempData));
						tmpData->fsm = fsm;

						if(curObj==0)
							matched0th = true;
					}
					break;
				}
			}
		}

		if(dontPush)
			continue;

		if(!tmpData)
			tmpData = calloc(1, sizeof(ObjectFSMTempData));

		tmpData->objName = objName;

		eaPush(&data->fsmsAndStates, tmpData);
	}

	eaDestroy(&data->fsms);  // Clean up and ensure earray isn't used

	return matched0th;
}

FSM* genesisGenerateSubFSM(GenesisMissionContext *context, GenesisFSM *gfsm)
{
	/*
	Refactor, or copy if not possible, the code that makes the primary FSM
	*/

	assert(0);
	return NULL;
}

const char* genesisExternVarGetMsgKey(GenesisMissionContext *context, const char* varPrefix, WorldVariableDef* def)
{
	char keyBuffer[1024];
	sprintf(keyBuffer, "%s.ExternVar.%s", varPrefix, def->pcName);
	return allocAddString(keyBuffer);
}

void genesisFSMStateProcessVars(GenesisMissionContext *context, const char* varPrefix, WorldVariableDef **vars, char **estrOut)
{
	estrClear(estrOut);
	FOR_EACH_IN_EARRAY(vars, WorldVariableDef, var)
	{
		char *valueStr = NULL;
		char *typeStr = NULL;
		MultiVal val = {0};

		var->pSpecificValue->eType = var->eType;

		worldVariableToMultival(NULL, var->pSpecificValue, &val);
		MultiValToEString(&val, &valueStr);

		switch(var->eType)
		{
			xcase WVAR_STRING: {
				typeStr = "String";
			}
			xcase WVAR_INT: {
				typeStr = "Int";
			}
			xcase WVAR_FLOAT: {
				typeStr = "Float";
			}
			xcase WVAR_ANIMATION: {
				typeStr = "String";
			}
			xcase WVAR_MESSAGE: {
				DisplayMessage fsmMessage = { 0 };
				char scopeBuffer[ 256 ];
				typeStr = "String";

				// Have to reset the value every time, in case it's a republish, which changes the namespace
				estrPrintf(&valueStr, "%s", genesisExternVarGetMsgKey(context, varPrefix, var));
				sprintf( scopeBuffer, "ExternVar%d", FOR_EACH_IDX( vars, var ));

				// Also need to store the message
				genesisCreateMessage( context, &fsmMessage, var->pSpecificValue->pcStringVal );
				langFixupMessage( fsmMessage.pEditorCopy, valueStr, "Message for extern var", scopeBuffer );
				
				eaPush( &context->extra_messages_accum->messages, StructClone( parse_DisplayMessage, &fsmMessage ));
				StructReset( parse_DisplayMessage, &fsmMessage );
			}
		}

		if(!stricmp(typeStr, "String"))
			estrConcatf(estrOut, "OverrideExtern%sVarCurState(\"encounter\", \"%s\",\"%s\"); ", typeStr, var->pcName, valueStr);
		else
			estrConcatf(estrOut, "OverrideExtern%sVarCurState(\"encounter\", \"%s\",%s); ", typeStr, var->pcName, valueStr);

		estrDestroy(&valueStr);
		MultiValClear(&val);
	}
	FOR_EACH_END
}

GenesisMissionObjective* genesisFindMissionObjective(GenesisMissionContext *context, const char* name)
{
	FOR_EACH_IN_EARRAY(context->zone_mission->desc.eaObjectives, GenesisMissionObjective, obj)
	{
		if(!stricmp(obj->pcName, name))
			return obj;
	}
	FOR_EACH_END

	return NULL;
}

void genesisWhenFSMExprTextAndEventsFromObjective(GenesisMissionContext *context, const char *objectiveName, char **condOut, char **actionOut, char **eventListenOut)
{
	char *evStr = NULL;
	GenesisMissionObjective *objective = genesisFindMissionObjective(context, objectiveName);
	GameEvent *ev = genesisCompleteObjectiveEvent( objective, zmapInfoGetPublicName(context->zmap_info));

	gameevent_WriteEventEscaped(ev, &evStr);
	// Our 'trigger' only happens when the last challenge is done.
	estrPrintf(eventListenOut, "GlobalEventAddListenAliased(\"%s\", \"%s\"); ", evStr, objectiveName);
	estrPrintf(condOut, "CheckMessage(\"%s\")>0", objectiveName);
	estrPrintf(actionOut, "ClearMessage(\"%s\")", objectiveName);

	estrDestroy(&evStr);
	StructDestroySafe(parse_GameEvent, &ev);
}

void genesisWhenFSMExprTextAndEvents(GenesisMissionContext *context, GenesisWhen *when, char **condOut, char **actionOut, char **eventListenOut, int start)
{
	switch(when->type)
	{
		xcase GenesisWhen_MapStart: {
			
		}
		xcase GenesisWhen_ObjectiveInProgress : {
			char *evStr = NULL;
			char *objName = start ? eaHead(&when->eaObjectiveNames) : eaTail(&when->eaObjectiveNames);

			genesisWhenFSMExprTextAndEventsFromObjective(context, objName, condOut, actionOut, eventListenOut);
		}
		xdefault : {
			assert(0);
		}
	}
}

/// Fill out FSM's name, filename, and scope with namespace info.
const void genesisFSMUpdateNames( GenesisMissionContext* context, char *pcName, FSM* fsm)
{
	if( context->zmap_info ) {
		char path[ MAX_PATH ];
		char nameSpace[RESOURCE_NAME_MAX_SIZE];
		char name[RESOURCE_NAME_MAX_SIZE];
		char scope[RESOURCE_NAME_MAX_SIZE];

		if (!resExtractNameSpace_s(zmapInfoGetFilename( context->zmap_info ), SAFESTR(nameSpace), SAFESTR(path)))
		{
			nameSpace[0] = '\0';
			strcpy(path, zmapInfoGetFilename( context->zmap_info ));
		}
		getDirectoryName( path );
		strcat( path, "/FSM" );

		strcpy(scope, "UGCMap");
		strcpy_s(&scope[6], RESOURCE_NAME_MAX_SIZE-6, &path[4]);
		fsm->scope = allocAddFilename( scope );
		fsm->group = allocAddFilename( "UGCMap" );

		if (nameSpace[0])
		{
			char path2[ MAX_PATH ];
			sprintf(name, "%s:%s", nameSpace, pcName);
			fsm->name = allocAddString(name);
			sprintf(path2, NAMESPACE_PATH"%s/%s/%s.fsm", nameSpace, path, pcName);
			fsm->fileName = allocAddFilename(path2);
		}
		else
		{
			fsm->name = allocAddString(pcName);
			strcatf(path, "/%s.fsm", pcName);
			fsm->fileName = allocAddFilename(path);
		}
	}
	else
		assert(0);
}

bool genesisExternVarIsDefault(WorldVariableDef *var)
{
	if(var->eType==WVAR_NONE)
		return true;

	switch(var->eType)
	{
		xcase WVAR_INT: {
			if(var->pSpecificValue->iIntVal==0)
				return true;
		}
		xcase WVAR_FLOAT: {
			if(var->pSpecificValue->fFloatVal==0)
				return true;
		}
		xcase WVAR_STRING: {
			if(nullStr(var->pSpecificValue->pcStringVal))
				return true;
		}
		xcase WVAR_ANIMATION: {
			if(nullStr(var->pSpecificValue->pcStringVal))
				return true;
		}
		xcase WVAR_MESSAGE: {
			if(nullStr(var->pSpecificValue->pcStringVal))
				return true;
		}
		xdefault: {
			assert(0);
		}
	}

	return false;
}

void genesisFSMPruneExternVars(GenesisFSM *fsm)
{
	FOR_EACH_IN_EARRAY(fsm->eaVarDefs, WorldVariableDef, var)
	{
		if(genesisExternVarIsDefault(var))
		{
			eaRemove(&fsm->eaVarDefs, FOR_EACH_IDX(0, var));
			StructDestroy(parse_WorldVariableDef, var);
		}
	}
	FOR_EACH_END
}
 
void genesisCreateFSM(GenesisMissionContext *context, ObjectFSMData *data)
{
	FOR_EACH_IN_EARRAY(data->fsms, GenesisFSM, fsm)
	{
		genesisFSMPruneExternVars(fsm);
	}
	FOR_EACH_END

	if(eaSize(&data->fsms)==1 && 
		data->fsms[0]->activeWhen.type==GenesisWhen_MapStart &&
		!eaSize(&data->fsms[0]->eaVarDefs))
	{
		// Simple case, referencing a single FSM, with no conditions, at all times
		GenesisFSM *gfsm = data->fsms[0];
		GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, data->challengeName );

		params->pcFSMName = StructAllocString(gfsm->pcFSMName);
	}
	else
	{
		// Here we need to actually build an FSM
		StashTable statesOnEntry;
		FSM *topFSM = StructCreate(parse_FSM);
		char* filename = NULL;
		FSMState *stateAmbient, *stateCombat;
		int needsAmbientFromTrans = false;
		char *startOnEntryFirst = NULL;
		char *name = NULL;

		genesisSortFSMs(context, data);

		statesOnEntry = stashTableCreateAddress(10);

		estrPrintf(&name, "UGCFSM_%s", data->challengeName);
		genesisFSMUpdateNames(context, name, topFSM);
		topFSM->comment = StructAllocString("Autogenerated FSM from UGC");

		stateAmbient = fsmCreateState(topFSM, "Ambient");
		fsmStateSetAction(topFSM, stateAmbient, "Ambient()");

		stateCombat = fsmCreateState(topFSM, "Combat");
		fsmStateSetAction(topFSM, stateCombat, "Combat()");

		fsmStateAddTransition(topFSM, stateAmbient, stateCombat, "DefaultEnterCombat()", "");
		fsmStateAddTransition(topFSM, stateCombat, stateAmbient, "DefaultDropOutOfCombat()", "");

		// Create all states
		FOR_EACH_IN_EARRAY_FORWARDS(data->fsmsAndStates, ObjectFSMTempData, fsmAndState)
		{
			char varPrefixBuffer[ 1024 ];
			char *varStr = NULL;

			if(!fsmAndState || !fsmAndState->fsm)  // Empty objective
				continue;

			estrPrintf(&name, "SubFSM_%s", fsmAndState->fsm->pcName);

			fsmAndState->state = fsmCreateState(topFSM, name);

			sprintf( varPrefixBuffer, "%s_SubFSM_%s", topFSM->name, fsmAndState->fsm->pcName );
			genesisFSMStateProcessVars(context, varPrefixBuffer, fsmAndState->fsm->eaVarDefs, &varStr);
			if(fsmAndState->fsm->pcFSMName)
				SET_HANDLE_FROM_STRING(gFSMDict, fsmAndState->fsm->pcFSMName, fsmAndState->state->subFSM);
			else
				devassert(0);

			stashAddressAddPointer(statesOnEntry, fsmAndState->state, varStr, true);
		}
		FOR_EACH_END

		ANALYSIS_ASSUME(data->fsmsAndStates && data->fsmsAndStates[0]);
		if(!data->fsmsAndStates[0]->fsm)
		{
			needsAmbientFromTrans = true;
		}
		else
		{
			int ambIdx = eaFind(&topFSM->states, stateAmbient);
			int stIdx = eaFind(&topFSM->states, data->fsmsAndStates[0]->state);
			eaSwap(&topFSM->states, ambIdx, stIdx);
			needsAmbientFromTrans = false;
		}

		FOR_EACH_IN_EARRAY_FORWARDS(data->fsmsAndStates, ObjectFSMTempData, fsmAndState)
		{
			FSMState *state;
			FSMState *stateNext;
			ObjectFSMTempData *fsmAndStateNext;
			ObjectFSMTempData *fsmAndStatePrev;
			GenesisFSM *next;
			GenesisFSM *gfsm;
			char *transCond = NULL;
			char *transAction = NULL; 
			char *listenExprs = NULL;

			if(!fsmAndState || !fsmAndState->fsm)
				continue;

			gfsm = fsmAndState->fsm;
			state = fsmAndState->state;

			fsmAndStatePrev = eaGet(&data->fsmsAndStates, FOR_EACH_IDX(0, fsmAndState)-1);
			devassert(!fsmAndStatePrev || !fsmAndStatePrev->fsm || fsmAndStatePrev->fsm->activeWhen.type!=GenesisWhen_MapStart);

			fsmAndStateNext = eaGet(&data->fsmsAndStates, FOR_EACH_IDX(0, fsmAndState)+1);
			next = fsmAndStateNext ? fsmAndStateNext->fsm : NULL;
			stateNext = fsmAndStateNext ? fsmAndStateNext->state : NULL;
			
			if(gfsm->pcFSMName)
				SET_HANDLE_FROM_STRING(gFSMDict, gfsm->pcFSMName, state->subFSM);
			
			if(needsAmbientFromTrans && fsmAndState->fsm->activeWhen.type!=GenesisWhen_MapStart)
			{
				devassert(fsmAndStatePrev && !fsmAndStatePrev->fsm);
				genesisWhenFSMExprTextAndEventsFromObjective(context, fsmAndStatePrev->objName, &transCond, &transAction, &listenExprs);
				estrAppend(&startOnEntryFirst, &listenExprs);

				fsmStateAddTransition(topFSM, stateAmbient, state, transCond, transAction);
			}

			genesisWhenFSMExprTextAndEvents(context, &gfsm->activeWhen, &transCond, &transAction, &listenExprs, false);
			estrAppend(&startOnEntryFirst, &listenExprs);

			if(next)
			{
				needsAmbientFromTrans = false;
				fsmStateAddTransition(topFSM, state, stateNext, transCond, transAction);
			}
			else
			{
				needsAmbientFromTrans = true;
				fsmStateAddTransition(topFSM, state, stateAmbient, transCond, transAction);
			}

			estrDestroy(&transAction);
			estrDestroy(&transCond);
			estrDestroy(&listenExprs);
		}
		FOR_EACH_END

		if(data->fsmsAndStates[0]->fsm && data->fsmsAndStates[0]->fsm->activeWhen.type==GenesisWhen_MapStart)
		{
			int stateIt;
			int transitionIt;
			for( stateIt = eaSize(&topFSM->states) - 1; stateIt >= 0; --stateIt) {
				for( transitionIt = eaSize(&topFSM->states[stateIt]->transitions) - 1; transitionIt >= 0; --transitionIt) {
					FSMTransition* transition = topFSM->states[stateIt]->transitions[transitionIt];
					if(transition->targetName == stateAmbient->name) {
						StructDestroy( parse_FSMTransition, transition );
						eaRemove( &topFSM->states[stateIt]->transitions, transitionIt );
					}
				}
			}
			
			eaSetSize(&topFSM->states, 1);
			StructDestroy(parse_FSMState, stateAmbient);
			StructDestroy(parse_FSMState, stateCombat);
		}

		if(!nullStr(startOnEntryFirst))
			topFSM->states[0]->onFirstEntry = exprCreateFromString(startOnEntryFirst, topFSM->fileName);

		if(stashGetCount(statesOnEntry))
		{
			StashTableIterator iter;
			StashElement elem;
			stashGetIterator(statesOnEntry, &iter);

			while(stashGetNextElement(&iter, &elem))
			{
				FSMState *state = stashElementGetKey(elem);
				char *entry = stashElementGetPointer(elem);

				fsmStateSetOnEntry(topFSM, state, entry, NULL);
			}
		}

		eaPush(context->fsm_accum, topFSM);

		{
			GenesisInstancedObjectParams* params = genesisInternChallengeRequirementParams( context, data->challengeName );
			params->pcFSMName = StructAllocString(topFSM->name);
		}

		estrDestroy(&name);
	}
}

/// Create a WorldGameActionProperties that will show the prompt PROMPT. 
WorldGameActionProperties* genesisCreatePromptAction( GenesisMissionContext* context, GenesisMissionPrompt* prompt )
{
	WorldGameActionProperties* accum = StructCreate( parse_WorldGameActionProperties );

	accum->eActionType = WorldGameActionType_Contact;
	accum->pContactProperties = StructCreate( parse_WorldContactActionProperties );
	SET_HANDLE_FROM_STRING( g_ContactDictionary, genesisContactName( context, prompt ),
							accum->pContactProperties->hContactDef );
	accum->pContactProperties->pcDialogName = StructAllocString( prompt->pcName );

	return accum;
}

static void genesisEnsureObjectHasInteractProperties(GroupDef *def, bool disallow_volume)
{
	bool defIsVolume = (def->property_structs.volume && !def->property_structs.volume->bSubVolume) && !disallow_volume;
	if( defIsVolume ) {
		if( !def->property_structs.server_volume.interaction_volume_properties ) {
			def->property_structs.server_volume.interaction_volume_properties = StructCreate( parse_WorldInteractionProperties );
		}

		groupDefAddVolumeType(def, "Interaction");
	} else {
		if( !def->property_structs.interaction_properties ) {
			def->property_structs.interaction_properties = StructCreate( parse_WorldInteractionProperties );
		}
	}

	// Assuming interaction is only added for clickies, this may
	// have transformed a NONE challenge into a clickie challenge.
	if( !def->property_structs.genesis_challenge_properties ) {
		def->property_structs.genesis_challenge_properties = StructCreate( parse_WorldGenesisChallengeProperties );
	}
	if( def->property_structs.genesis_challenge_properties->type == GenesisChallenge_None ) {
		def->property_structs.genesis_challenge_properties->type = GenesisChallenge_Clickie;
	}
}

void genesisApplyObjectVisibilityParams(GroupDef *def, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext)
{
	const char* clickableStr = allocAddString( "CLICKABLE" );
	const char* destructibleStr = allocAddString( "DESTRUCTIBLE" );
	const char* fromDefinitionStr = allocAddString( "FROMDEFINITION" );
	const char* namedObjectStr = allocAddString( "NAMEDOBJECT" );
	const char* contactStr = allocAddString( "CONTACT" );
	if (interact_params->clickieVisibleWhenCond) {
		WorldInteractionPropertyEntry* interactEntry = NULL;
		int it;

		genesisEnsureObjectHasInteractProperties(def, interact_params->bDisallowVolume);

		if( !eaSize( &def->property_structs.interaction_properties->eaEntries )) {
			eaPush( &def->property_structs.interaction_properties->eaEntries, StructCreate( parse_WorldInteractionPropertyEntry ));
			def->property_structs.interaction_properties->eaEntries[ 0 ]->pcInteractionClass = namedObjectStr;
		}

		for( it = 0; it != eaSize( &def->property_structs.interaction_properties->eaEntries ); ++it ) {
			WorldInteractionPropertyEntry* entry = def->property_structs.interaction_properties->eaEntries[ it ];
			if(   entry->pcInteractionClass == clickableStr || entry->pcInteractionClass == destructibleStr
				  || entry->pcInteractionClass == fromDefinitionStr || entry->pcInteractionClass == namedObjectStr
				  || entry->pcInteractionClass == contactStr ) {
				interactEntry = entry;
				break;
			}
		}
		
		if( !interactEntry ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
							   "Trying to set a clicky visible "
							   "when condition on an interactable that is "
							   "not a clickie nor a destructable." );
			return;
		}

		if( interactEntry->pcInteractionClass == destructibleStr ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
							   "Clicky visible when on destructables is not supported." );
		} else {
			if( interactEntry->pVisibleExpr ) {
				StructDestroy( parse_Expression, interactEntry->pVisibleExpr );
			}
			interactEntry->pVisibleExpr = StructClone( parse_Expression, interact_params->clickieVisibleWhenCond );
			def->property_structs.interaction_properties->bEvalVisExprPerEnt = interact_params->clickieVisibleWhenCondPerEnt;
			interactEntry->bOverrideVisibility = true;
			if( interact_params->clickieVisibleWhenCondPerEnt ) {
				groupDefAddPropertyBool( def, "NoCollision", true );
			}
		}
	}
}

void gensisApplyInteractObjectDoorParams(GroupDef *def, GenesisInteractObjectParams *interact_params, GenesisRuntimeErrorContext *debugContext)
{
	bool defIsVolume = (def->property_structs.volume && !def->property_structs.volume->bSubVolume) && !interact_params->bDisallowVolume;
	if (eaSize(&interact_params->eaInteractionEntries) != 1)
	{
		genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
				"Trying to set more than one interaction entry on a "
				"UGC door.");
	}
	else
	{
		WorldInteractionPropertyEntry *existing_entry = NULL;
		if (defIsVolume) {
			if (!def->property_structs.server_volume.interaction_volume_properties
				|| eaSize(&def->property_structs.server_volume.interaction_volume_properties->eaEntries) != 1)
			{
				genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
						"Trying to set UGC door interact properties on a volume "
						"that has no existing interaction properties.");
			}
			else
			{
				existing_entry = def->property_structs.server_volume.interaction_volume_properties->eaEntries[0];
			}
		} else {
			if (!def->property_structs.interaction_properties
				|| eaSize(&def->property_structs.interaction_properties->eaEntries) != 1)
			{
				genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
						"Trying to set UGC door interact properties on an interactable "
						"that has no existing interaction properties.");
			}
			else
			{
				existing_entry = def->property_structs.interaction_properties->eaEntries[0];
			}
		}
		if (existing_entry)
		{
			WorldInteractionPropertyEntry *new_entry = interact_params->eaInteractionEntries[0];

			REMOVE_HANDLE(new_entry->hInteractionDef);
			new_entry->pcInteractionClass = existing_entry->pcInteractionClass;
			new_entry->bOverrideInteract = existing_entry->bOverrideInteract;
			new_entry->bOverrideVisibility = existing_entry->bOverrideVisibility;
			new_entry->bOverrideCategoryPriority = existing_entry->bOverrideCategoryPriority;
			new_entry->bExclusiveInteraction = existing_entry->bExclusiveInteraction;
			new_entry->bUseExclusionFlag = existing_entry->bUseExclusionFlag;
			new_entry->bDisablePowersInterrupt = existing_entry->bDisablePowersInterrupt;
			if (existing_entry->pTimeProperties)
			{
				new_entry->pTimeProperties = StructClone(parse_WorldTimeInteractionProperties, existing_entry->pTimeProperties);
			}
			if (existing_entry->pActionProperties)
			{
				if (!new_entry->pActionProperties)
					new_entry->pActionProperties = StructCreate(parse_WorldActionInteractionProperties);
				if (existing_entry->pActionProperties->pSuccessExpr)
				{
					new_entry->pActionProperties->pSuccessExpr = StructClone(parse_Expression, existing_entry->pActionProperties->pSuccessExpr);
				}

				if (existing_entry->pActionProperties->pFailureExpr)
				{
					new_entry->pActionProperties->pFailureExpr = StructClone(parse_Expression, existing_entry->pActionProperties->pFailureExpr);
				}

				FOR_EACH_IN_EARRAY(existing_entry->pActionProperties->successActions.eaActions, WorldGameActionProperties, action)
				{
					eaPush(&new_entry->pActionProperties->successActions.eaActions, StructClone(parse_WorldGameActionProperties, action));
				}
				FOR_EACH_END;
			}
			if (existing_entry->pAnimationProperties)
			{
				StructDestroy(parse_WorldAnimationInteractionProperties, new_entry->pAnimationProperties);
				new_entry->pAnimationProperties = StructClone(parse_WorldAnimationInteractionProperties, existing_entry->pAnimationProperties);
			}

			if (existing_entry->pSuccessCond)
			{
				genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
						"UGC door interaction object cannot have an "
						"existing success condition.");
			}
		}
	}
}

void genesisApplyInteractObjectParams(const char *zmap_name, GroupDef *def, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext)
{
	const char* clickableStr = allocAddString( "CLICKABLE" );
	const char* destructibleStr = allocAddString( "DESTRUCTIBLE" );
	const char* fromDefinitionStr = allocAddString( "FROMDEFINITION" );
	const char* namedObjectStr = allocAddString( "NAMEDOBJECT" );
	bool defIsVolume = (def->property_structs.volume && !def->property_structs.volume->bSubVolume) && !interact_params->bDisallowVolume;

	if (interact_params->bIsUGCDoor)
	{
		gensisApplyInteractObjectDoorParams(def, interact_params, debugContext);
	}

	// This *MUST* be valid on non-interactables, so that plain ol'
	// object library pieces can be made interactable.
	if( eaSize( &interact_params->eaInteractionEntries ) > 0 ) {
		genesisEnsureObjectHasInteractProperties(def, interact_params->bDisallowVolume);

		if (defIsVolume) {
			int it;
			// This is needed because other interaction properties will override the door
			eaDestroyStruct( &def->property_structs.server_volume.interaction_volume_properties->eaEntries, parse_WorldInteractionPropertyEntry );

			for( it = 0; it != eaSize( &interact_params->eaInteractionEntries ); ++it ) {
				eaPush( &def->property_structs.server_volume.interaction_volume_properties->eaEntries, StructClone( parse_WorldInteractionPropertyEntry, interact_params->eaInteractionEntries[ it ]));
			}
		}
		else
		{
			int it;

			// This is needed because other interaction properties will override the door
			StructCopyAll( parse_DisplayMessage, &interact_params->displayNameMsg, &def->property_structs.interaction_properties->displayNameMsg );
			eaDestroyStruct( &def->property_structs.interaction_properties->eaEntries, parse_WorldInteractionPropertyEntry );

			for( it = 0; it != eaSize( &interact_params->eaInteractionEntries ); ++it ) {
				eaPush( &def->property_structs.interaction_properties->eaEntries, StructClone( parse_WorldInteractionPropertyEntry, interact_params->eaInteractionEntries[ it ]));
			}
		}
	}

	if( interact_params->interactWhenCond ) {
		WorldInteractionPropertyEntry* interactEntry = NULL;
		if( defIsVolume ) {
			if( def->property_structs.server_volume.interaction_volume_properties ) {
				int it;
				for( it = 0; it != eaSize( &def->property_structs.server_volume.interaction_volume_properties->eaEntries ); ++it ) {
					WorldInteractionPropertyEntry* entry = def->property_structs.server_volume.interaction_volume_properties->eaEntries[ it ];
					if( entry->pcInteractionClass == clickableStr || entry->pcInteractionClass == destructibleStr
							|| entry->pcInteractionClass == fromDefinitionStr ) {
						interactEntry = entry;
						break;
					}
				}
			}
		} else if (def->property_structs.interaction_properties) {
			int it;
			for( it = 0; it != eaSize( &def->property_structs.interaction_properties->eaEntries ); ++it ) {
				WorldInteractionPropertyEntry* entry = def->property_structs.interaction_properties->eaEntries[ it ];
				if( entry->pcInteractionClass == clickableStr || entry->pcInteractionClass == destructibleStr
						|| entry->pcInteractionClass == fromDefinitionStr ) {
					interactEntry = entry;
					break;
				}
			}
		}
		if( !interactEntry ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
					"Trying to set an interact when "
					"condition on an interactable that is "
					"not a clickie nor a destructable." );
			return;
		}

		if( interactEntry->pcInteractionClass == destructibleStr ) {
			genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
					"Interact when on destructables is not supported." );
			// NOT CURRENTLY SUPPORTED
			// // Destructables don't have a "targetable when" field, so
			// // use pVisibleExpr to achieve nearly the same
			// // effect.
			// if( interactEntry->pVisibleExpr ) {
			// 	StructDestroy( parse_Expression, interactEntry->pVisibleExpr );
			// }
			// interactEntry->pVisibleExpr = StructClone( parse_Expression, interact_params->interactWhenCond );
		} else {
			if( interactEntry->pInteractCond ) {
				StructDestroy( parse_Expression, interactEntry->pInteractCond );
			}
			interactEntry->pInteractCond = StructClone( parse_Expression, interact_params->interactWhenCond );
			interactEntry->bOverrideInteract = true;
		}
	}

	if( interact_params->succeedWhenCond ) {
		if( def->property_structs.interaction_properties ) {
			/// CLICKIE SUPPORT
			WorldInteractionPropertyEntry* interactEntry = NULL;
			{
				int it;
				for( it = 0; it != eaSize( &def->property_structs.interaction_properties->eaEntries ); ++it ) {
					WorldInteractionPropertyEntry* entry = def->property_structs.interaction_properties->eaEntries[ it ];
					if( entry->pcInteractionClass == clickableStr || entry->pcInteractionClass == destructibleStr
						|| entry->pcInteractionClass == fromDefinitionStr ) {
						interactEntry = entry;
						break;
					}
				}
			}
			if( !interactEntry ) {
				genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
								   "Trying to set a succeed "
								   "when condition on an interactable that is "
								   "not a clickie nor a destructable." );
				return;
			}

			if( interactEntry->pcInteractionClass == destructibleStr ) {
				genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
								   "Succeed when on destructables is not supported." );
				// NOT CURRENTLY SUPPORTED
				// // Destructables don't have a "targetable when" field, so
				// // use pVisibleExpr to achieve nearly the same
				// // effect.
				// if( interactEntry->pVisibleExpr ) {
				// 	StructDestroy( parse_Expression, interactEntry->pVisibleExpr );
				// }
				// interactEntry->pVisibleExpr = StructClone( parse_Expression, interact_params->succeedWhenCond );
			} else {
				if( interactEntry->pSuccessCond ) {
					StructDestroy( parse_Expression, interactEntry->pSuccessCond );
				}
				interactEntry->pSuccessCond = StructClone( parse_Expression, interact_params->succeedWhenCond );
				interactEntry->bOverrideInteract = true;
			}
		} else {
			genesisRaiseError( GENESIS_ERROR, debugContext,
							   "Trying to set a succeed when "
							   "condition on something that is not a clickie, "
							   "or volume." );
		}
	}

}

/// Callback function to apply our mission-specific parameters to an instanced Challenge groupdef
void genesisApplyInstancedObjectParams(const char *zmap_name,
									   GroupDef *def, GenesisInstancedObjectParams *params, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext)
{
	if (interact_params)
		genesisApplyInteractObjectParams(zmap_name, def, interact_params, challenge_name, debugContext);

	if( params->encounterSpawnCond ) {
		if( def->property_structs.encounter_properties ) {
			/// CHALLENGE2 SUPPORT
			WorldEncounterSpawnProperties* spawnProps;
			if( !def->property_structs.encounter_properties->pSpawnProperties ) {
				ZoneMapInfo* zmapInfo = zmapGetInfo(layerGetZoneMap(def->def_lib->zmap_layer));
				WorldEncounterSpawnProperties* props = StructCreate( parse_WorldEncounterSpawnProperties );
				props->eSpawnRadiusType = encounter_GetSpawnRadiusTypeFromProperties(zmapInfo, NULL);
				props->eRespawnTimerType = encounter_GetRespawnTimerTypeFromProperties(zmapInfo, NULL);
				def->property_structs.encounter_properties->pSpawnProperties = props;
			}
			spawnProps = def->property_structs.encounter_properties->pSpawnProperties;

			spawnProps->pSpawnCond = StructClone( parse_Expression, params->encounterSpawnCond );
			spawnProps->pDespawnCond = StructClone( parse_Expression, params->encounterDespawnCond );
		} else {
			genesisRaiseError( GENESIS_ERROR, debugContext,
							   "Trying to set a spawn when "
							   "condition on something that is not an encounter." );
		}
	}

	if( params->has_patrol ) {
		if( def->property_structs.encounter_properties ) {
			/// CHALLENGE2 SUPPORT
			char patrolName[ 256 ];
			sprintf( patrolName, "%s_Patrol", challenge_name );
			def->property_structs.encounter_properties->pcPatrolRoute = StructAllocString( patrolName );
		} else {
			genesisRaiseError( GENESIS_FATAL_ERROR, debugContext,
							   "Trying to set a patrol on a non-encounter." );
		}
	}

	if (params->pContact)
	{
		WorldInteractionProperties* interactProps = NULL;
		if( def->property_structs.encounter_properties ) {
			WorldActorProperties *actor = eaGet(&def->property_structs.encounter_properties->eaActors, 0);
			if(!actor)
				eaSet(&def->property_structs.encounter_properties->eaActors, actor = StructCreate(parse_WorldActorProperties), 0);

			actor->pcName = allocAddString("Actor_1");

			if(params->pContact->contactName.pEditorCopy && !nullStr(params->pContact->contactName.pEditorCopy->pcDefaultString))
			{
				if(actor->displayNameMsg.pEditorCopy && !nullStr(actor->displayNameMsg.pEditorCopy->pcDefaultString))
					genesisRaiseError( GENESIS_ERROR, debugContext, "Setting a message on a contact when there was already one");
				else
					StructCopyAll(parse_DisplayMessage, &params->pContact->contactName, &actor->displayNameMsg);
			}

			if(!actor->pInteractionProperties)
				actor->pInteractionProperties = StructCreate(parse_WorldInteractionProperties);

			interactProps = actor->pInteractionProperties;
		} else {
			// Plain ol' object, make interactable
			if( !def->property_structs.interaction_properties ) {
				def->property_structs.interaction_properties = StructCreate( parse_WorldInteractionProperties );
			}

			interactProps = def->property_structs.interaction_properties;
		}

		if( interactProps ) {
			eaDestroyStruct(&interactProps->eaEntries, parse_WorldInteractionPropertyEntry);
			FOR_EACH_IN_EARRAY(params->pContact->eaPrompts, GenesisMissionPromptExprPair, prompt)
			{
				WorldInteractionPropertyEntry *entry = StructCreate(parse_WorldInteractionPropertyEntry);
				entry->pcInteractionClass = allocAddString("Contact");

				if( !nullStr( prompt->exprText )) {
					entry->bOverrideInteract = true;
					entry->pInteractCond = exprCreateFromString( prompt->exprText, NULL );
				}
			
				entry->pContactProperties = StructCreate(parse_WorldContactInteractionProperties);
				entry->pContactProperties->pcDialogName = StructAllocString(prompt->name);
				SET_HANDLE_FROM_STRING("ContactDef", params->pContact->pcContactFileName, entry->pContactProperties->hContactDef);
			
				eaPush(&interactProps->eaEntries, entry);
			}
			FOR_EACH_END;
		} else {
			genesisRaiseError( GENESIS_ERROR, debugContext,
							   "Trying to set a Prompt interact on something that is not handled." );
		}
	}
}

void genesisApplyInstancedAndVisibilityObjectParams(const char *zmap_name,
	GroupDef *def, GenesisInstancedObjectParams *params, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext)
{
	genesisApplyInstancedObjectParams(zmap_name, def, params, interact_params, challenge_name, debugContext);
	if( interact_params ) {
		genesisApplyObjectVisibilityParams(def, interact_params, challenge_name, debugContext);
	}
}



AUTO_RUN;
void genesisInitInstancedObjectParams(void)
{
	genesisSetInstancePropertiesFunction(genesisApplyInstancedAndVisibilityObjectParams);
}

#endif

/// For TomY ENCOUNTER_HACK only.
GenesisMissionPrompt* genesisFindPromptPEPHack( GenesisMissionDescription* missionDesc, char* prompt_name )
{
	int it;
	if( !missionDesc ) {
		return NULL;
	}
	
	for( it = 0; it != eaSize( &missionDesc->zoneDesc.eaPrompts ); ++it ) {
		GenesisMissionPrompt* prompt = missionDesc->zoneDesc.eaPrompts[ it ];
		if( stricmp( prompt->pcName, prompt_name ) == 0 ) {
			return prompt;
		}
	}

	return NULL;
}

/// Simple wrapper around genesisCreateEncounterSpawnCondText()
Expression* genesisCreateEncounterSpawnCond( GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties)
{
	char* estr = NULL;
	genesisCreateEncounterSpawnCondText( context, &estr, zmapName, properties );

	if( estr ) {
		Expression* expr = exprCreateFromString( estr, NULL );
		estrDestroy( &estr );
		return expr;
	} else {
		return NULL;
	}
}

/// An expression that is always the opposite of what
/// genesisCreateEncounterSpawnCond() would evaluate to.
///
/// This exists because encounters do not check their spawn condition
/// when spawned (unlike clickies).
Expression* genesisCreateEncounterDespawnCond( GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties)
{
	char* estr = NULL;
	genesisCreateEncounterSpawnCondText( context, &estr, zmapName, properties );

	if( estr ) {
		Expression* expr;
		
		estrInsertf( &estr, 0, "NOT (" );
		estrConcatf( &estr, ")" );
	
		expr = exprCreateFromString( estr, NULL );
		estrDestroy( &estr );
		return expr;
	} else {
		return NULL;
	}
}

/// Hack callback to generate an encounter spawn condition from a
/// GenesisProceduralEncounterProperties
///
/// TomY ENCOUNTER_HACK
void genesisCreateEncounterSpawnCondText( GenesisMissionContext* context, char** estr, const char* zmapName, GenesisProceduralEncounterProperties *properties)
{
	genesisCreateChallengeSpawnCondText( context, estr, zmapName, properties, true );
	
	if( properties->genesis_mission_num >= 0 ) {
		if( *estr ) {
			char* baseExprText;

			strdup_alloca( baseExprText, *estr );
			estrPrintf( estr, "GetMapVariableInt(\"Mission_Num\") = %d and HasNearbyPlayersForEnc() and (%s)",
						properties->genesis_mission_num, baseExprText );
		} else {
			estrPrintf( estr, "GetMapVariableInt(\"Mission_Num\") = %d and HasNearbyPlayersForEnc()",
						properties->genesis_mission_num );
		}
	}
}

/// Simple wrapper around genesisCreateChallengeSpawnCondText()
Expression* genesisCreateChallengeSpawnCond( GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties )
{
	char* estr = NULL;
	genesisCreateChallengeSpawnCondText( context, &estr, zmapName, properties, false );

	if( estr ) {
		Expression* expr = exprCreateFromString( estr, NULL );
		estrDestroy( &estr );
		return expr;
	} else {
		return NULL;
	}
}

/// Function used to generate a challenge spawn condition from a
/// GenesisProceduralEncounterProperties.
void genesisCreateChallengeSpawnCondText( GenesisMissionContext* context, char** estr, const char* zmapName, GenesisProceduralEncounterProperties *properties, bool isEncounter )
{
	const char* missionName = genesisMissionNameRaw( zmapName, properties->genesis_mission_name, properties->genesis_open_mission );
	estrClear(estr);

	if( properties->genesis_mission_num < 0 && properties->spawn_when.type != GenesisWhen_MapStart ) {
		genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextChallenge( properties->encounter_name, SAFE_MEMBER2( context, zone_mission, desc.pcName ), NULL ),
						   "Shared challenges can not have spawn when conditions." );
	} else {
		GenesisRuntimeErrorContext* debugContext = genesisMakeTempErrorContextChallenge( properties->encounter_name, SAFE_MEMBER2( context, zone_mission, desc.pcName ), NULL );
		char* exprText;
		
		exprText = genesisWhenExprTextRaw( context, zmapName,
										   (properties->genesis_open_mission ? GenesisMissionGenerationType_OpenMission : GenesisMissionGenerationType_PlayerMission),
										   properties->genesis_mission_name, properties->when_challenges,
										   &properties->spawn_when, debugContext, "SpawnWhen", isEncounter );

		estrDestroy( estr );
		*estr = exprText;
	}
}

/// Write to ESTR the GameEvent specified by EVENT.
///
/// Automatically frees EVENT for you, so you can put this on one line
/// like this:
///
/// genesisWriteText( &str, genesisCompleteChallengeEvent( challenge, ... ))
void genesisWriteText( char** estr, GameEvent* event, bool escaped )
{
	char* eventEStr = NULL;
	ParserWriteText( &eventEStr, parse_GameEvent, event, 0, 0, 0 );
	
	if( escaped ) {
		estrAppendEscaped( estr, eventEStr );
	} else {
		estrAppend( estr, &eventEStr );
	}
	estrDestroy( &eventEStr );
	StructDestroy( parse_GameEvent, event );
}

/// Create a GameEvent that will trigger when OBJECTIVE-DESC is
/// completed.
GameEvent* genesisCompleteChallengeEvent( GenesisChallengeType challengeType, const char* challengeName, bool useGroup, const char* zmapName )
{
	GameEvent* event = StructCreate( parse_GameEvent );
	char groupName[ 256 ];

	sprintf( groupName, "LogGrp_%s", challengeName );
	
	event->pchMapName = allocAddString( zmapName );
		
	switch( challengeType ) {		
		case GenesisChallenge_Encounter: case GenesisChallenge_Encounter2:
			event->type = EventType_EncounterState;
			event->encState = EncounterState_Success;
			if( useGroup ) {
				event->pchTargetEncGroupName = allocAddString( groupName );
			} else {
				event->pchTargetStaticEncName = allocAddString( challengeName );
			}

		xcase GenesisChallenge_Clickie:
			event->type = EventType_InteractSuccess;
			event->tSourceIsPlayer = TriState_Yes;
			if( useGroup ) {
				event->pchClickableGroupName = StructAllocString( groupName );
			} else {
				event->pchClickableName = StructAllocString( challengeName );
			}

		xcase GenesisChallenge_Destructible:
			event->type = EventType_Kills;
			event->tSourceIsPlayer = TriState_Yes;
			event->pchTargetEncGroupName = StructAllocString( groupName );

		xdefault:
			FatalErrorf( "not yet implemented" );
	}

	return event;
}

/// Create a GameEvent that will trigger when objective is completed.
GameEvent* genesisCompleteObjectiveEvent( GenesisMissionObjective *obj, const char* zmapName )
{
	GameEvent* ev = StructCreate( parse_GameEvent );
	
	ev->pchMapName = allocAddString( zmapName );
	ev->type = EventType_MissionState;
	ev->missionState = MissionState_Succeeded;
	ev->pchMapName = allocAddString(zmapName);
	ev->pchMissionRefString = allocAddString(obj->pcName);

	return ev;
}

/// Create a GameEvent that will trigger when ROOM-NAME for
/// MISSION-NAME is reached.
GameEvent* genesisReachLocationEvent( const char* layoutName, const char* roomOrChallengeName, const char* missionName, const char* zmapName )
{
	if (!layoutName)
		return genesisReachLocationEventRaw( zmapName, genesisMissionChallengeVolumeName( roomOrChallengeName, missionName ));
	else
		return genesisReachLocationEventRaw( zmapName, genesisMissionRoomVolumeName( layoutName, roomOrChallengeName, missionName ));
}

/// Create a GameEvent that will trigger when VOLUME-NAME is entered.
///
/// Low level wrapper around GameEvent structure.
GameEvent* genesisReachLocationEventRaw( const char* zmapName, const char* volumeName )
{
	GameEvent* event = StructCreate( parse_GameEvent );
	event->pchMapName = allocAddString( zmapName );
	event->type = EventType_VolumeEntered;
	event->tSourceIsPlayer = TriState_Yes;
	event->pchVolumeName = StructAllocString( volumeName );

	return event;
}

/// Create a GameEvent that will trigger when killing any
/// CRITTER-DEF-NAME.
GameEvent* genesisKillCritterEvent( const char* critterDefName, const char* zmapName )
{
	GameEvent* event = StructCreate( parse_GameEvent );	
	event->pchMapName = allocAddString( zmapName );
	event->type = EventType_Kills;
	event->pchTargetCritterName = StructAllocString( critterDefName );

	return event;
}

/// Create a GameEvent that will trigger when killing any critter in
/// the group CRITTER-GROUP-NAME.
GameEvent* genesisKillCritterGroupEvent( const char* critterGroupName, const char* zmapName )
{
	GameEvent* event = StructCreate( parse_GameEvent );	
	event->pchMapName = allocAddString( zmapName );
	event->type = EventType_Kills;
	event->pchTargetCritterGroupName = StructAllocString( critterGroupName );

	return event;
}

/// Create a GameEvent that will trigger when talking to a specific
/// contact.  The contact can be on any ZoneMap.
GameEvent* genesisTalkToContactEvent( char* contactName )
{
	GameEvent* event = StructCreate( parse_GameEvent );	
	event->type = EventType_InteractSuccess;
	event->tSourceIsPlayer = TriState_Yes;
	event->pchContactName = allocAddString( contactName );

	return event;
}

/// Create a GameEvent that will trigger when finishing a special
/// dialog using COSTUME-NAME and DIALOG-NAME.
GameEvent* genesisPromptEvent( char* dialogName, char* blockName, bool isComplete, const char* missionName, const char* zmapName, const char* challengeName )
{
	GameEvent* event = StructCreate( parse_GameEvent );
	if( isComplete ) {
		event->type = EventType_ContactDialogComplete;
	} else {
		event->type = EventType_ContactDialogStart;
	}
	event->pchContactName = allocAddString( genesisContactNameRaw( zmapName, missionName, challengeName ));
	event->pchDialogName = StructAllocString( genesisSpecialDialogBlockNameTemp( dialogName, blockName ));

	return event;
}

/// Create a GameEvent that will trigger when finishing a special
/// dialog using CONTACT-NAME and DIALOG-NAME.
GameEvent* genesisExternalPromptEvent( char* dialogName, char* contactName, bool isComplete )
{
	GameEvent* event = StructCreate( parse_GameEvent );
	if( isComplete ) {
		event->type = EventType_ContactDialogComplete;
	} else {
		event->type = EventType_ContactDialogStart;
	}
	event->pchContactName = allocAddString( contactName );
	event->pchDialogName = StructAllocString( dialogName );

	return event;
}

/// Version of genesisMissionName that takes each parameter
/// seperately.
const char* genesisMissionNameRaw( const char* zmapName, const char* genesisMissionName, bool isOpenMission )
{
	char missionName[256];
	char* fix = NULL;

	sprintf( missionName, "%s_Mission_%s%s",
			 zmapName, genesisMissionName,
			 (isOpenMission ? "_OpenMission" : "") );
	if( resFixName( missionName, &fix )) {
		strcpy( missionName, fix );
		estrDestroy( &fix );
	}
	return allocAddString( missionName );
}

/// Version of genesisContactName that takes each parameter
/// seperately.
const char* genesisContactNameRaw( const char* zmapName, const char* missionName, const char* challengeName )
{
	char contactName[256];
	char* fix = NULL;

	if( zmapName ) {
		if (challengeName != NULL)
			sprintf( contactName, "%s_Contact_Shared_%s",
					 zmapName, challengeName );
		else
			sprintf( contactName, "%s_Contact_%s",
					 zmapName, missionName );
	} else {
		strcpy( contactName, missionName );
	}
	
	if( resFixName( contactName, &fix )) {
		strcpy( contactName, fix );
		estrDestroy( &fix );
	}
	return allocAddString( contactName );
}

/// Convert from GENESIS-COSTUME to CONTACT-COSTUME.
void genesisMissionCostumeToContactCostume( GenesisMissionCostume* genesisCostume, ContactCostume* contactCostume )
{
	switch( genesisCostume->eCostumeType ) {
		case GenesisMissionCostumeType_Specified:
			contactCostume->eCostumeType = ContactCostumeType_Specified;
		xcase GenesisMissionCostumeType_PetCostume:
			contactCostume->eCostumeType = ContactCostumeType_PetContactList;
		xcase GenesisMissionCostumeType_CritterGroup:
			contactCostume->eCostumeType = ContactCostumeType_CritterGroup;
		xcase GenesisMissionCostumeType_Player:
			contactCostume->eCostumeType = ContactCostumeType_Player;
		xdefault:
			contactCostume->eCostumeType = ContactCostumeType_Default;
	}
	
	COPY_HANDLE( contactCostume->costumeOverride, genesisCostume->hCostume );
	COPY_HANDLE( contactCostume->hPetOverride, genesisCostume->hPetCostume );
	contactCostume->eCostumeCritterGroupType = genesisCostume->eCostumeCritterGroupType;
	COPY_HANDLE( contactCostume->hCostumeCritterGroup, genesisCostume->hCostumeCritterGroup );
	contactCostume->pchCostumeMapVar = allocAddString( genesisCostume->pchCostumeMapVar );
	contactCostume->pchCostumeIdentifier = allocAddString( genesisCostume->pchCostumeIdentifier );
}


/// Convert from CONTACT-COSTUME to GENESIS-COSTUME.
void genesisMissionCostumeFromContactCostume( GenesisMissionCostume* genesisCostume, ContactCostume* contactCostume )
{
	switch( contactCostume->eCostumeType ) {
		case ContactCostumeType_Specified:
			genesisCostume->eCostumeType = GenesisMissionCostumeType_Specified;
		case ContactCostumeType_PetContactList:
			genesisCostume->eCostumeType = GenesisMissionCostumeType_PetCostume;
		case ContactCostumeType_CritterGroup:
			genesisCostume->eCostumeType = GenesisMissionCostumeType_CritterGroup;
		case ContactCostumeType_Player:
			contactCostume->eCostumeType = GenesisMissionCostumeType_Player;
		default:
			genesisCostume->eCostumeType = GenesisMissionCostumeType_Specified;
	}
	
	COPY_HANDLE( genesisCostume->hCostume, contactCostume->costumeOverride );
	COPY_HANDLE( genesisCostume->hPetCostume, contactCostume->hPetOverride );
	genesisCostume->eCostumeCritterGroupType = contactCostume->eCostumeCritterGroupType;
	COPY_HANDLE( genesisCostume->hCostumeCritterGroup, contactCostume->hCostumeCritterGroup );
	genesisCostume->pchCostumeMapVar = StructAllocString( contactCostume->pchCostumeMapVar );
	genesisCostume->pchCostumeIdentifier = StructAllocString( contactCostume->pchCostumeIdentifier );
}

#include "GenesisMissions_h_ast.c"
#include "GenesisMissions_c_ast.c"
