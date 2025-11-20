//// Shared GameServer interface from the rest of the code to UGC.
////
//// Deals with stuff like how to call generation, upload resources to
//// UGCMaster, etc.
#pragma once

typedef struct Entity Entity;
typedef struct UGCPlayIDEntryName UGCPlayIDEntryName;
typedef struct UGCProjectAutosaveData UGCProjectAutosaveData;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProjectInfo UGCProjectInfo;

// How much to stall for some UGC operations
extern int giUGCLeavePreviewStallySeconds;
extern int giUGCPreviewTransferStallySeconds;
extern int giUGCEnterPreviewStallySeconds;
extern int giUGCPublishStallySeconds;
extern int giUGCSaveStallySeconds;

// If set, print out debugging info
extern bool g_bUGCEnableDebugOutput;

// Project to import on loading
extern char gUGCImportProjectName[128];


// Used by republish w/ regenerate
bool gslUGC_ServerBinnerGenerateProject(char *ns, bool *regenerate);

// Clean up files that were written to disk
void gslUGC_DeleteNamespaceDataFiles(const char *ns);

// If UGC playing is available or not
bool gslUGC_PlayingIsEnabled(void);

// If UGC reviewing is available or not
bool gslUGC_ReviewingIsEnabled(void);

// Get detail data about the project
void gslUGC_DoRequestDetails(SA_PARAM_NN_VALID Entity *pEntity, U32 iProjectID, U32 iSeriesID, S32 iRequesterID);

// Set the autosave copy, sholud be done before any potential crash
// situtation.
void gslUGC_SetUGCProjectCopy(UGCProjectData *data);

// Clear the dirty flag, will prevent autosave (until a new copy comes)
void gslUGC_ClearUGCProjectCopyDirtyFlag(void);

// Call right before any potentialy server crash operation
void gslUGC_SendAutosaveIfNecessary(bool force);

// Set the UGC trivia data, should be done before any potential crash /
// error situation.
void gslUGC_AddTriviaData( UGCProjectData* data );

// Clear the UGC trivia data
void gslUGC_RemoveTriviaData( void );

// Validate the UGCInfo for this map, make sure that every object
// referenced has a display name and details set.
void gslUGC_MapValidate( void );

// Used during preview to map transfer
void gslUGC_TransferToMapWithDelay(Entity *pEntity, const char *map_name, const char *spawn_point, const Vec3 spawn_pos, const Vec3 spawn_pyr, UGCProjectData* start_objective_proj, int start_objective_id);

// Call this when the server finishes loading everything.
void gslUGC_ServerIsDoneLoading(void);

// Play the project, with an overried map_name / objective_id / spawn_pos.
void gslUGC_DoPlay( Entity *pEntity, UGCProjectData* ugc_proj, const char *map_name, U32 objective_id, Vec3 spawn_pos, Vec3 spawn_rot, bool only_reload );

// Call this after generation to start playing (exposed so DoPlayDialogTree can be implemented game-specific)
void gslUGC_DoPlayCB( bool succeeded, UserData entityRef );

// Call this once per frame
void gslUGC_Tick(void);

// Call when the player disconnects
void gslUGC_HandleUserDisconnect(void);

UGCProjectAutosaveData *gslUGC_LoadAutosave(const char *namespace);


//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in gslSTOUGC.c or gslNWUGC.c
void gslUGC_RenameProjectNamespace(UGCProjectData *pData, const char *strNamespace);
UGCProjectData *gslUGC_LoadProjectDataWithInfo(const char *namespace, UGCProjectInfo *pProjectInfo);
void gslUGCPlayPreprocess( UGCProjectData* ugc_proj, const char **map_name, U32* objective_id, const char **spawn_name );

// Validate a list of projects based on the file
//
// MJF Oct/6/2012 -- Not sure if this is ever used.
void gslUGCValidateProjects(const char *filename);

typedef void (*MissionStartObjectiveCB)( bool succeeded, UserData data );
void ugcMissionStartObjective( Entity* playerEnt, UGCProjectData* proj, const char* map_name, U32 objectiveID, MissionStartObjectiveCB fn, UserData userData );

#define ugcProjectGenerateOnServer(ugcProj) ugcProjectGenerateOnServerEx( ugcProj,NULL,NULL,NULL )
bool ugcProjectGenerateOnServerEx( UGCProjectData* ugcProj, const char *override_spawn_map, Vec3 override_spawn_pos, Vec3 override_spawn_rot );
void gslUGC_DoPlayDialogTree( Entity* pEntity, UGCProjectData* ugc_proj, U32 dialog_tree_id, int prompt_id );
void gslUGC_ProjectAddPlayIDEntryNames( UGCProjectData* ugcProj, UGCPlayIDEntryName*** peaIDEntryNames );
void gslUGC_DoPlayingEditorHideComponent( Entity* ent, int componentID );
