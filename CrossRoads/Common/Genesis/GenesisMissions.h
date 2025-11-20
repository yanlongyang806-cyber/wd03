//// The Genesis system -- Mission generation
#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

typedef struct ContactCostume ContactCostume;
typedef struct Expression Expression;
typedef struct GenesisEpisode GenesisEpisode;
typedef struct GenesisInstancedObjectParams GenesisInstancedObjectParams;
typedef struct GenesisInteractObjectParams GenesisInteractObjectParams;
typedef struct GenesisMapDescription GenesisMapDescription;
typedef struct GenesisMissionChallenge GenesisMissionChallenge;
typedef struct GenesisMissionContext GenesisMissionContext;
typedef struct GenesisMissionCostume GenesisMissionCostume;
typedef struct GenesisMissionDescription GenesisMissionDescription;
typedef struct GenesisMissionPrompt GenesisMissionPrompt;
typedef struct GenesisMissionRequirements GenesisMissionRequirements;
typedef struct GenesisMissionZoneChallenge GenesisMissionZoneChallenge;
typedef struct GenesisProceduralEncounterProperties GenesisProceduralEncounterProperties;
typedef struct GenesisRuntimeErrorContext GenesisRuntimeErrorContext;
typedef struct GenesisZoneMapData GenesisZoneMapData;
typedef struct GenesisZoneMission GenesisZoneMission;
typedef struct GroupDef GroupDef;
typedef struct ImageMenuItemOverride ImageMenuItemOverride;
typedef struct InteractableOverride InteractableOverride;
typedef struct MissionDef MissionDef;
typedef struct MissionOfferOverride MissionOfferOverride;
typedef struct WorldGameActionProperties WorldGameActionProperties;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo; 

AUTO_STRUCT;
typedef struct GenesisMissionAdditionalParams {
	InteractableOverride** eaInteractableOverrides;
	MissionOfferOverride** eaMissionOfferOverrides;
	ImageMenuItemOverride** eaImageMenuItemOverrides;
	WorldGameActionProperties** eaSuccessActions;
} GenesisMissionAdditionalParams;
extern ParseTable parse_GenesisMissionAdditionalParams[];
#define TYPE_parse_GenesisMissionAdditionalParams GenesisMissionAdditionalParams

#ifndef NO_EDITORS

GenesisZoneMission* genesisTransmogrifyMission(ZoneMapInfo* zmap_info, GenesisMapDescription* map_desc, int mission_num);
GenesisMissionZoneChallenge** genesisTransmogrifySharedChallenges(ZoneMapInfo* zmap_info, GenesisMapDescription* map_desc);
void genesisTransmogrifyChallengePEPHack( GenesisMapDescription* map_desc, int mission_num, GenesisMissionChallenge* challenge, GenesisProceduralEncounterProperties*** outPepList );

// genesisApplyInstancedObjectParams will automatically call genesisApplyInteractObjectParams.
// Call genesisApplyInteractObjectParams only when the object is not instanced. (Used in UGC)
void genesisApplyInstancedObjectParams(const char *zmap_name, GroupDef *def, GenesisInstancedObjectParams *params, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext); 
void genesisApplyInteractObjectParams(const char *zmap_name, GroupDef *def, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext);
void genesisApplyObjectVisibilityParams(GroupDef *def, GenesisInteractObjectParams *interact_params, char* challenge_name, GenesisRuntimeErrorContext* debugContext);

void genesisDeleteMissions(const char* zmap_filename);
GenesisMissionRequirements* genesisGenerateMission(ZoneMapInfo* zmap_info, int mission_num, GenesisZoneMission* mission, GenesisMissionAdditionalParams* additionalParams, bool is_ugc, const char *project_prefix, bool write_mission);

void genesisGenerateEpisodeMission(const char* episode_root, GenesisEpisode* episode);
const char* genesisEpisodePartMapName( GenesisEpisode* episode, const char* mapDescName );

#endif

// TomY ENCOUNTER_HACK support -- needed even with NO_EDITORS
GenesisMissionPrompt* genesisFindPromptPEPHack( GenesisMissionDescription* missionDesc, char* prompt_name );
Expression* genesisCreateEncounterSpawnCond(GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties);
Expression* genesisCreateEncounterDespawnCond(GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties);
void genesisCreateEncounterSpawnCondText(GenesisMissionContext* context, char** estr, const char* zmapName, GenesisProceduralEncounterProperties *properties);
Expression* genesisCreateChallengeSpawnCond(GenesisMissionContext* context, const char* zmapName, GenesisProceduralEncounterProperties *properties);
void genesisCreateChallengeSpawnCondText(GenesisMissionContext* context, char** estr, const char* zmapName, GenesisProceduralEncounterProperties *properties, bool isEncounter);

void genesisMissionMessageFillKeys( MissionDef * accum, const char* root_mission_name );

#ifndef NO_EDITORS

GenesisMissionZoneChallenge* genesisFindZoneChallenge( GenesisZoneMapData* zmap_data, GenesisZoneMission* zone_mission, const char* challenge_name );
GenesisMissionZoneChallenge* genesisFindZoneChallengeRaw( GenesisZoneMapData* zmap_data, GenesisZoneMission* zone_mission, GenesisMissionZoneChallenge** override_challenges, const char* challenge_name );

#endif

// Structure conversion functions
void genesisMissionCostumeToContactCostume( GenesisMissionCostume* genesisCostume, ContactCostume* contactCostume );
void genesisMissionCostumeFromContactCostume( GenesisMissionCostume* genesisCostume, ContactCostume* contactCostume );

// Exposed for UGC:
const char* genesisMissionNameRaw( const char* zmapName, const char* genesisMissionName, bool isOpenMission );
const char* genesisContactNameRaw( const char* zmapName, const char* missionName, const char* challengeName );
