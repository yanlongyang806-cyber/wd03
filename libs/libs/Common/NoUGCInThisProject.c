//// This project has no UGC editor.
////
//// If you want to add UGC to a project, you need to copy the UGC
//// files from one of the projects using UGC (StarTrek, NNO, etc).
////
//// Provide all the C functions and parse tables needed to link
//// correctly.  None of them should do anything.

#include "EString.h"
#include "cmdparse.h"

typedef enum MissionPlayType MissionPlayType;
typedef struct ResourceInfo ResourceInfo;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct UGCEditorProjectDetails UGCEditorProjectDetails;
typedef struct UGCKillCreditLimit UGCKillCreditLimit;
typedef struct UGCKillCreditLimit2 UGCKillCreditLimit2;
typedef struct UGCPlayIDEntryName UGCPlayIDEntryName;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectInfo UGCProjectInfo;
typedef struct UGCProjectSeriesGroup UGCProjectSeriesGroup;
typedef struct UGCResourceInfo UGCResourceInfo;
typedef struct UGCRoomInfo UGCRoomInfo;

AUTO_STRUCT;
typedef struct UGCProjectData {
	int iAmNotARealStruct;
} UGCProjectData;

AUTO_STRUCT;
typedef struct UGCFreezeProjectInfo {
	int iAmNotARealStruct;
} UGCFreezeProjectInfo;

// (Ab)use that C doesn't care about parameters matching if you don't
// use any of them.  This gives the freedom to change the parameter
// list and only update the projects using UGC.
const char* ugcDefaultsGetAllegianceRestriction()
{
	return NULL;
}

bool ugcDefaultsAuthorAllowsFeaturedBlocksEditing()
{
	return false;
}

UGCProjectData *ugcProjectLoadFromDir()
{
	return NULL;
}

void ugcResourceInfoPopulateDictionary()
{
}

bool ugcValidateErrorfIfStatusHasErrors()
{
	return false;
}

void ugcValidateSeries()
{
}

bool ugcDefaultsSearchFiltersByPlayerLevel()
{
	return false;
}

void gslUGCValidateProjects()
{
}

void ugcPlatformDictionaryLoad()
{
}

void gslUGC_RenameProjectNamespace()
{
}

UGCProjectData *gslUGC_LoadProjectDataWithInfo()
{
	return NULL;
}

MissionPlayType ugcDefaultsGetNonCombatType()
{
	return 0;
}

const char * ugcProjectDataGetNamespace()
{
	return NULL;
}

void ugcMissionStartObjective()
{
}

bool ugcProjectGenerateOnServerEx()
{
	return false;
}

UGCProjectInfo* ugcProjectDataGetProjectInfo()
{
	return NULL;
}

ResourceInfo* ugcResourceGetInfo()
{
	return NULL;
}

void ugcEditorOncePerFrame()
{
}

void ugcEditorDrawGhosts()
{
}

void ugcEditorShutdown()
{
}

void ugcEditMode()
{
}

void ugcRoomFreeRoomInfo()
{
}

UGCRoomInfo* ugcRoomAllocRoomInfo()
{
	return NULL;
}

bool ugcEditorQueryLogout()
{
	return false;
}

void ugcTakeObjectPreviewPhotos()
{
}

void ugcEditorStartup()
{
}

bool ugcEditorIsActive()
{
	return false;
}

void ugcEditorSetStartupData()
{
}

void ugcLoadEditingData()
{
}

char * ugcAllocSMFString()
{
	return NULL;
}

void ugcLoadDictionaries()
{
}

bool ugcIsAllegianceEnabled()
{
	return false;
}

void gslUGCPlayPreprocess()
{
}

void ugcResourceLoadLibrary()
{
}

void ugcResource_GetAudioAssets()
{
}

void ugcValidateProject()
{
}

int ugcProjectFixupDeprecated()
{
	return 0;
}

void ugcEditorFixupProjectData()
{
}

void gslUGC_DoPlayDialogTree()
{
}

void gslUGC_DoRespecCharacter()
{
}

void DoFreezeUGCProject()
{
}

void ugcProjectDataGetInitialMapAndSpawn()
{
}

void ugcProjectDataGetSTOGrantPrompt()
{
}

char **ugcProjectDataGetMaps()
{
	return NULL;
}

void UGCEditorDoUGCPublishEnabled()
{
}

void gclUGCDoProcessPlayResult()
{
}

void UGCEditorDoAutosaveDeletionCompleted()
{
}

void UGCEditorDoUpdateSaveStatus()
{
}

void UGCEditorDoWaitForResourcesComplete()
{
}

void DoReceiveCryptKeyForSafeProjectExport()
{
}

void DoSafeImportBufferResult()
{
}

void UGCEditorDoUpdatePublishStatus()
{
}

void ugcEditorDoAuthorAllowsFeaturedChanged()
{
}

void DoUGCProjectJobStatus()
{
}

void ugcSeriesGroup_Destroy()
{
}

void ugcProjectManager_FreeEditorProjectDetails()
{
}

void ugcProjectManagerRefresh()
{
}

UGCEditorProjectDetails* ugcProjectManager_AllocEditorProjectDetails()
{
	return NULL;
}

void ugcSeriesGroup_Refresh()
{
}

UGCProjectSeriesGroup* ugcSeriesGroup_Create()
{
	return NULL;
}

bool ugcIsFixedLevelEnabled()
{
	return false;
}

bool ugcDefaultsIsSeriesEditorEnabled()
{
	return false;
}

void ugcDefaultsFillAllegianceList()
{
}

void ugcSeriesGroup_CloseModalDialog()
{
}

void ugcEditorReceiveMoreReviews()
{
}

void UGCProductViewer_SetPurchaseResult()
{
}

void UGCProductViewer_Refresh()
{
}

void UGCProductViewer_Destroy()
{
}

void UGCProductViewer_OncePerFrame()
{
}

void ugcProjectChooserInit()
{
}

void ugcProjectChooserFree()
{
}

bool ugcProjectChooser_IsOpen()
{
	return false;
}

void ugcProjectChooserShow()
{
}

void ugcProjectChooserHide()
{
}

void ugcProjectChooserSetPossibleProjects()
{
}

void ugcProjectChooserSetImportProjects()
{
}

void ugcProjectChooserReceiveMoreReviews()
{
}

void ugcProjectChooser_FinishedLoading()
{
}

void ugcProjectChooser_SetMode()
{
}

void ugcEditorToggleSkin()
{
}

void ugcSeriesEditor_ProjectSeriesCreate_Result()
{
}

void ugcSeriesEditor_ProjectSeriesUpdate_Result()
{
}

AUTO_STARTUP(UGC);
void ugcStartup(void)
{
}

void ugcProjectDataNameSpaceChange()
{
}

void gslUGC_ProjectAddPlayComponentData()
{
}

void gslUGC_DoPlayingEditorHideComponent()
{
}

void ugcLoadingUpdateState()
{
}


bool ugcPowerPropertiesIsUsedInUGC()
{
	return false;
}

const char* ugcGetDefaultMapName()
{
	return "Emptymap";
}

void gslUGC_DoRespawnAtFullHealth()
{
}

bool gslUGC_ProjectBudgetAllowsGenerate()
{
	return false;
}

void ugcEditorDoForceUpdateAutosave( void )
{
}

void UGCTagFillAllKeysAndValues()
{
}

#include "NoUGCInThisProject_c_ast.c"
