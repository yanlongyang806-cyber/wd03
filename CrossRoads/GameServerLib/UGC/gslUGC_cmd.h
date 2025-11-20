//// Container for all Client -> Server communication for UGC.
////
//// Let's have a real API here!
#pragma once

typedef struct Entity Entity;
typedef struct UGCFreezeProjectInfo UGCFreezeProjectInfo;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProject UGCProject;
typedef struct TransactionReturnVal TransactionReturnVal;

void gslUGC_PlayProjectNonEditor(Entity *pEntity, const char *pcNamespace, const char *pcCostumeOverride, const char *pcPetOverride, const char *pcBodyText, bool bPlayingAsBetaReviewer);
void QueryUGCProjectStatus(Entity *pEntity);
void SaveAndPublishUGCProject(Entity *pEntity, UGCProjectData *data);

//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in gslSTOUGC_cmd.c or gslNWUGC_cmd.c
void gslUGC_DoRespecCharacter( Entity* ent, int allegianceDefaultsIndex, const char* className, int levelValue );
void DoFreezeUGCProject(Entity *pEntity, UGCProjectData *data, UGCFreezeProjectInfo *pInfo);

U32 gslUGC_FeaturedTimeFromString( const char* timeStr );

void gslUGC_RequestAccountThrottled(Entity *pEntity);

char *UGCImport_Save(UGCProject *pUGCProject, UGCProjectData *pUGCProjectData, bool bPublish, TransactionReturnVal *pReturnVal);
