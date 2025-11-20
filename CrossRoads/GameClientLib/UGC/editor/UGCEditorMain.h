//// This is the main UGC editor header file to include from outside
//// the UGC editor system. Declarations go in here that are needed by
//// other systems to hook into the UGC editor.
#pragma once

typedef struct MEField MEField;
typedef struct NOCONST(UGCProjectReviews) NOCONST(UGCProjectReviews);
typedef struct UGCPlayResult UGCPlayResult;
typedef struct UGCProjectAutosaveData UGCProjectAutosaveData;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProjectReviews UGCProjectReviews;
typedef struct UGCProjectStatusQueryInfo UGCProjectStatusQueryInfo;
typedef struct UIButton UIButton;
typedef struct UIPane UIPane;
typedef struct UIWidget UIWidget;
typedef struct UGCAchievementEvent UGCAchievementEvent;
typedef void UIAnyWidget;

typedef void (*UIActivationFunc)(UIAnyWidget *, UserData);

extern int ugcNeverTimeout;
#define UGC_SAVE_STATUS_TIMEOUT (ugcNeverTimeout ? 9001 : 60) // Timeout after 60 seconds
#define UGC_PLAY_TIMEOUT (ugcNeverTimeout ? 9001 : 180) // Timeout after 180 seconds
#define UGC_PUBLISH_STATUS_TIMEOUT (ugcNeverTimeout ? 9001 : 180) // Timeout after 180 seconds
#define UGC_AUTHOR_ALLOWS_FEATURED_TIMEOUT (ugcNeverTimeout ? 9001 : 30)
#define UGC_AUTOSAVE_DURATION (60) // Autosave to server every minute (if there are changes)
#define UGC_VERSION_STRING "Beta v0.8"

void ugcEditorShowEULA(UIActivationFunc yesCB);
void ugcEditorSetCamera(void);
bool ugcEditorUpdateReviews( NOCONST(UGCProjectReviews)* pReviews, int* piPageNumber, const UGCProjectReviews* pNewReviews, int iNewPageNumber );

// Loading dialogs
typedef enum UGCLoadingState
{
	// default state -- nothing being loaded
	UGC_LOAD_NONE,

	UGC_LOAD_INIT,
	UGC_LOAD_WAITING_IN_QUEUE,
	UGC_LOAD_WAITING_FOR_SERVER,

	// TODO: Unify in the loading for the resources
	//UGC_LOAD_CONNECTED_TO_SERVER,
	//UGC_LOAD_DONE,
} UGCLoadingState;

void ugcLoadingUpdateState(UGCLoadingState state, int queuePos);

// Quick + dirty modal dialogs
void ugcModalDialogPrepare( const char* title,
							const char* button1, UIActivationFunc button1Fn, const char* button2, UIActivationFunc button2Fn,
							UserData data );
void ugcModalDialogAddWidget( UIWidget* widget );
void ugcModalDialogAddField( MEField* field );
void ugcModalDialogShow( int width, int height );
void ugcModalDialogClose( UIButton* ignored, UserData ignored2 );
UIWidget* ugcModalDialogContentParent( void );
UIWidget* ugcModalDialogButton1( void );


//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in STOUGCEditorMain.c or
// NWUGCEditorMain.c:
//
// (These are in roughly lifecycle order)
void ugcTakeObjectPreviewPhotos( void );
bool ugcEditorIsActive(void);
void ugcEditorToggleSkin(bool bEnabled);

void ugcEditorSetStartupData(UGCProjectData *project_data, UGCProjectAutosaveData *autosave_data);
void ugcEditorStartup(void);

void ugcEditMode(int enabled);
void ugcEditorOncePerFrame( void );
void ugcEditorDrawGhosts( void );
bool ugcEditorQueryLogout(bool quit, bool choosePrevious);
void ugcEditorReceiveMoreReviews( U32 iProjectID, U32 iSeriesID, int iPageNumber, UGCProjectReviews *pReviews );

void ugcEditorShutdown( void );

// Server -> Client commands
void UGCEditorDoUGCPublishDisabled( bool bUGCPublishDisabled );
void gclUGCDoProcessPlayResult( UGCPlayResult* result );
void UGCEditorDoAutosaveDeletionCompleted(int iAutosaveType);
void UGCEditorDoUpdateSaveStatus(bool succeeded, const char *error);
void UGCEditorDoWaitForResourcesComplete(void);
void DoReceiveCryptKeyForSafeProjectExport(char *pKey, int iExportID);
void DoSafeImportBufferResult(int iID, int iResult);
void UGCEditorDoUpdatePublishStatus(bool succeeded, const char *pDisplayString);
void ugcEditorDoAuthorAllowsFeaturedChanged( bool bSucceeded );
void DoUGCProjectJobStatus(UGCProjectStatusQueryInfo *pInfo);

void gclUGC_SendAchievementEvent(UGCAchievementEvent *pUGCAchievementEvent);
