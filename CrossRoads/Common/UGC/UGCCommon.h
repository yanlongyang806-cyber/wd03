//// UGC common routines, cross-game
////
//// Functions used by the shared backend, client, gameserver, and
//// appserver go here.
#pragma once

typedef U32 ContainerID;
typedef enum MissionPlayType MissionPlayType;
typedef struct AllegianceList AllegianceList;
typedef struct FSMExternVar FSMExternVar;
typedef struct GroupDef GroupDef;
typedef struct NOCONST(UGCProject) NOCONST(UGCProject);
typedef struct NOCONST(UGCProjectReviews) NOCONST(UGCProjectReviews);
typedef struct NOCONST(UGCProjectVersion) NOCONST(UGCProjectVersion);
typedef struct NOCONST(UGCProjectVersionRestrictionProperties) NOCONST(UGCProjectVersionRestrictionProperties);
typedef struct NOCONST(UGCTimeStamp) NOCONST(UGCTimeStamp);
typedef struct ResourceInfo ResourceInfo;
typedef struct UGCCostumeMetadata UGCCostumeMetadata;
typedef struct UGCGroupDefMetadata UGCGroupDefMetadata;
typedef struct UGCKillCreditLimit UGCKillCreditLimit;
typedef struct UGCMapLocation UGCMapLocation;
typedef struct UGCProject UGCProject;
typedef struct UGCProjectData UGCProjectData;
typedef struct UGCProjectInfo UGCProjectInfo;
typedef struct UGCProjectReviews UGCProjectReviews;
typedef struct UGCProjectSearchInfo UGCProjectSearchInfo;
typedef struct UGCProjectSeries UGCProjectSeries;
typedef struct UGCProjectSeriesVersion UGCProjectSeriesVersion;
typedef struct UGCProjectVersion UGCProjectVersion;
typedef struct UGCProjectVersionMapLocation UGCProjectVersionMapLocation;
typedef struct UGCProjectVersionRestrictionProperties UGCProjectVersionRestrictionProperties;
typedef struct UGCRuntimeStatus UGCRuntimeStatus;
typedef struct UGCSingleReview UGCSingleReview;
typedef struct UGCTimeStampPlusShardName UGCTimeStampPlusShardName;
typedef struct WorldPowerVolumeProperties WorldPowerVolumeProperties;
typedef struct WorldUGCProperties WorldUGCProperties;
typedef struct WorldUGCRestrictionProperties WorldUGCRestrictionProperties;
typedef struct ZoneMapInfo ZoneMapInfo;

AUTO_ENUM;
typedef enum UGCPlayStatus
{
	UGC_PLAY_SUCCESS,
	UGC_PLAY_WRONG_FACTION,
	UGC_PLAY_NO_OBJECTIVE_MAP,
	UGC_PLAY_GENESIS_GENERATION_ERROR,
	UGC_PLAY_NO_DIALOG_TREE,
	UGC_PLAY_UNKNOWN_ERROR,
} UGCPlayStatus;

// Basically a LibFileLoad, but without the STARTTOK/ENDTOK craziness so we can send over the wire
AUTO_STRUCT;
typedef struct UGCPlayLayerData
{
	const char *filename;					AST(FILENAME POOL_STRING)
	GroupDef **eaDefs;						AST(NAME("LayerData") LATEBIND)
} UGCPlayLayerData;
extern ParseTable parse_UGCPlayLayerData[];
#define TYPE_parse_UGCPlayLayerData UGCPlayLayerData

AUTO_STRUCT;
typedef struct UGCPlayIDEntryName
{
	int componentID;						AST(NAME("ComponentID"))
	const char* entryName;					AST(NAME("EntryName") POOL_STRING)
} UGCPlayIDEntryName;
extern ParseTable parse_UGCPlayIDEntryName[];
#define TYPE_parse_UGCPlayIDEntryName UGCPlayIDEntryName

AUTO_STRUCT;
typedef struct UGCPlayResult
{
	UGCPlayStatus eStatus;					AST(NAME("Status"))
	ZoneMapInfo *pInfo;						AST(NAME("ZoneMap") LATEBIND)
	const char *fstrFilename;				AST(NAME("Filename") POOL_STRING)
	UGCPlayLayerData **eaLayerDatas;		AST(NAME("LayerData"))
	UGCPlayIDEntryName** eaIDEntryNames;	AST(NAME("IDEntryName"))
	char **eaSkyDefs;						AST(NAME("SkyDef"))
} UGCPlayResult;

AUTO_ENUM;
typedef enum UGCChangeReason {
	UGC_CHANGE_AUTOWITHDRAW,
	UGC_CHANGE_CSR_BAN,
	UGC_CHANGE_TEMP_AUTOBAN,
	UGC_CHANGE_PERMANENT_AUTOBAN,
} UGCChangeReason;
extern StaticDefineInt UGCChangeReasonEnum[];

// Autosave file
AUTO_STRUCT;
typedef struct UGCProjectAutosaveData
{
	U32 iTimestamp;
	UGCProjectData *pData;				AST(LATEBIND)
} UGCProjectAutosaveData;
extern ParseTable parse_UGCProjectAutosaveData[];
#define TYPE_parse_UGCProjectAutosaveData UGCProjectAutosaveData

AUTO_ENUM;
typedef enum UGCProjectReportReason
{
	kUGCProjectReportReason_MissionInappropriate,			ENAMES(MissionInappropriate)
	kUGCProjectReportReason_MAX, EIGNORE
} UGCProjectReportReason;
extern StaticDefineInt UGCProjectReportReasonEnum[];

// Description of how the auto-reporting works
AUTO_STRUCT;
typedef struct UGCProjectReportingDef
{
	int iNaughtyThreshold;				AST(NAME(NaughtyThreshold) DEFAULT(10))
		// Naughty value threshold that results in a temporary ban
	int iNaughtyIncrement;				AST(NAME(NaughtyIncrement) DEFAULT(2))
		// Naughty increment
	U32 uTemporaryBanTimer;				AST(NAME(TemporaryBanTimer) DEFAULT(86400))
		// Length of a temporary ban (in seconds)
	int iTemporaryBanCountResultsInBan;	AST(NAME(TemporaryBanCountResultsInBan) DEFAULT(3))
		// Number of temporary bans that result in a permanent ban
	U32 uNaughtyDecayInterval;			AST(NAME(NaughtyDecayInterval) DEFAULT(21600))
		// Time interval to perform naughty decay (in seconds)
	int iNaughtyDecayValue;				AST(NAME(NaughtyDecayValue) DEFAULT(1))
		// Naughty decay value
	int iMaxReportsPerProject;			AST(NAME(MaxReportsPerProject) DEFAULT(20))
		// Number of detailed reports to keep on a single project
} UGCProjectReportingDef;
extern ParseTable parse_UGCProjectReportingDef[];
#define TYPE_parse_UGCProjectReportingDef UGCProjectReportingDef

AUTO_STRUCT;
typedef struct UGCKillCreditLimit
{
	S32* piExpLimitPerLevel; AST(NAME(ExpLimitPerLevel))
		// The max amount of experience that the player can get in the specified time interval before kill credit stops counting (per level)

	U32 uTimeInterval; AST(NAME(TimeInterval) DEFAULT(86400))
		// The time interval in which to apply this limit (in seconds). Default is one day.
} UGCKillCreditLimit;
extern ParseTable parse_UGCKillCreditLimit[];
#define TYPE_parse_UGCKillCreditLimit UGCKillCreditLimit

extern UGCProjectReportingDef g_ReportingDef;

// Creates a UGCProjectInfo from the container
UGCProjectInfo *ugcCreateProjectInfo(UGCProject *active_project, const UGCProjectVersion *pVersion);

// Create a UGCMapLocation from the container
UGCMapLocation* ugcCreateMapLocation( UGCProjectVersionMapLocation* pMapLocation );

void UGC_GetProjectZipFileName(const char *namespace, char *outStr, size_t outStr_size);
UGCProjectData *UGC_LoadProjectData(const char *namespace, const char *dirPrefix);

void UGCProject_FillInTimestamp(NOCONST(UGCTimeStamp) *pTimeStamp);
void UGCProject_FillInTimestampPlusShardName(UGCTimeStampPlusShardName *pTimeStampPlusShardName);
char *UGCProject_GetTimestampPlusShardNameStringEscaped(void);
bool UGCProject_TimeStampPlusShardNameIsValidForSafeImport(UGCTimeStampPlusShardName *pTimeStampPlusShardName);

//returns true if the search filter is legal
//Note that this may do minor fixup, such as removing characters from strings that will be ignored
//during the search
bool UGCProject_ValidateAndFixupSearchInfo(UGCProjectSearchInfo *pSearchInfo, const char** pastrErrorMessageKey);

// Useful function for checking if a UGCProject is in a state where it will be auto-deleted dome time in the future. Returns true if this is a zombie project.
bool UGCProject_CanAutoDelete(const UGCProject *pUGCProject);
bool UGCProjectSeries_CanAutoDelete(const UGCProjectSeries *pUGCProjectSeries);

const char *UGCProject_GetMostRecentNamespace(const UGCProject *pProject);
const UGCProjectVersion *UGCProject_GetMostRecentVersion(const UGCProject *pProject);
bool UGCProject_IsPublishedAndNotDeleted(const UGCProject *pProject);
const UGCProjectVersion *UGCProject_GetMostRecentPublishedVersion(const UGCProject *pProject);
const char* UGCProject_GetVersionName(const UGCProject *pProject, const UGCProjectVersion* pVersion);
bool UGCProject_ValidatePotentialName(const char *pName, bool bIsSeries, char **ppErrorString);
void UGCProject_GetBanStatusString(ContainerID uProjectID, ContainerID uOwnerAccountID, const char* pchOwnerAccountName, const char* pchCSRAccountName, S32 iNaughtyValue, U32 uTemporaryBanExpireTime, bool bBanState, bool bTempoaryBan, char** pestrResult);

const UGCProjectSeriesVersion *UGCProjectSeries_GetMostRecentPublishedVersion(const UGCProjectSeries *pSeries);
const char* UGCProjectSeries_GetVersionName(const UGCProjectSeries *pProjectSeries, const UGCProjectSeriesVersion* pVersion);

NOCONST(UGCProjectVersion) *UGCProject_trh_GetSpecificVersion(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, const char *pNameSpace);
static __forceinline const UGCProjectVersion *UGCProject_GetSpecificVersion(const UGCProject *pProject, const char *pNameSpace)
{
	return  (UGCProjectVersion*) UGCProject_trh_GetSpecificVersion(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject), pNameSpace);
}

//where in the list of versions is the version for this namespace, currently
int UGCProject_GetVersionIndex(const UGCProject *pProject, const char *pNameSpace);

//////////////////////////////////////////////////////////////////////
// Reporting functions
int UGCProject_trh_FindReportByAccountID(ATH_ARG NOCONST(UGCProject)* pProject, U32 uAccountID);
#define UGCProject_FindReportByAccountID(pProject, uAccountID) UGCProject_trh_FindReportByAccountID(CONTAINER_NOCONST(UGCProject, pProject), uAccountID)

bool UGCProject_trh_CanMakeReport(ATR_ARGS, ATH_ARG NOCONST(UGCProject)* pProject, U32 uAccountID, U32 eReason, const char* pchDetails);
#define UGCProject_CanMakeReport(pProject, uAccountID, eReason, pchDetails) UGCProject_trh_CanMakeReport(ATR_EMPTY_ARGS, CONTAINER_NOCONST(UGCProject, pProject), uAccountID, eReason, pchDetails)


//////////////////////////////////////////////////////////////////////
// Review functions
int ugcReviews_SortByTimestamp( const UGCSingleReview **review1, const UGCSingleReview **review2 );
void ugcReviews_GetForPage( const UGCProjectReviews* pReviews, S32 iPageNumber, NOCONST(UGCProjectReviews)* out_pReviews );
S32 ugcReviews_GetPageCount( const UGCProjectReviews* pReviews );
S32 ugcReviews_GetRatingCount( ATH_ARG NOCONST(UGCProjectReviews)* pReviews );
F32 ugcReviews_ComputeAdjustedRatingUsingConfidence( ATH_ARG NOCONST(UGCProjectReviews)* pReviews );
S32 ugcReviews_FindBucketForRating( F32 fRating );


//////////////////////////////////////////////////////////////////////
// Restrictions functions
void ugcRestrictionsIntersect(WorldUGCRestrictionProperties* prop_accum, const WorldUGCRestrictionProperties* prop);
void ugcRestrictionsIntersectContainer(WorldUGCRestrictionProperties* prop_accum, const UGCProjectVersionRestrictionProperties* prop);
void ugcRestrictionsWLFromContainer(WorldUGCRestrictionProperties* out_prop, const UGCProjectVersionRestrictionProperties* prop);
void ugcRestrictionsContainerFromWL(NOCONST(UGCProjectVersionRestrictionProperties)* out_prop, const WorldUGCRestrictionProperties* prop);
bool ugcRestrictionsIsValid(WorldUGCRestrictionProperties* prop);

//////////////////////////////////////////////////////////////////////
// Resource Infos -- external resources info for UGC
void ugcResourceInfoPopulateDictionary( void );

ResourceInfo* ugcResourceGetInfo(const char *dictName, const char *objName);
ResourceInfo* ugcResourceGetInfoInt(const char *dictName, int objName);
const WorldUGCProperties *ugcResourceGetUGCProperties(const char *dictName, const char *objName);
const WorldUGCProperties* ugcResourceGetUGCPropertiesInt(const char* dictName, int objName);

//////////////////////////////////////////////////////////////////////
// Republishing flags
void ugcSetIsRepublishing(bool republishing);
bool ugcGetIsRepublishing(void);

//////////////////////////////////////////////////////////////////////
// Per-game functions exposed, in STOUGCCommon.c or NWUGCCommon.c:

// Configs
const char* ugcDefaultsGetAllegianceRestriction( void );
bool ugcDefaultsAuthorAllowsFeaturedBlocksEditing( void );
MissionPlayType ugcDefaultsGetNonCombatType( void );
const UGCKillCreditLimit* ugcDefaultsGetKillCreditLimit( void );
bool ugcDefaultsSearchFiltersByPlayerLevel( void );
bool ugcDefaultsIsSeriesEditorEnabled(void);
bool ugcIsAllegianceEnabled(void);
bool ugcIsFixedLevelEnabled(void);
void ugcDefaultsFillAllegianceList(AllegianceList *list);

// Dictionary loading
void ugcLoadDictionaries( void );
void ugcPlatformDictionaryLoad( void );
void ugcResourceLoadLibrary( void );

// Validate / Fixup
void ugcValidateProject( UGCProjectData* ugcProj );
void ugcValidateSeries( const UGCProjectSeries* ugcSeries );
bool ugcValidateErrorfIfStatusHasErrors( UGCRuntimeStatus* status );
void ugcEditorFixupProjectData(UGCProjectData* ugcProj, int* out_numDialogsDeleted, int* out_numCostumesReset, int* out_numObjectivesReset, int* out_fixupFlags);
void ugcEditorFixupMaps( UGCProjectData* ugcProj );
int ugcProjectFixupDeprecated( UGCProjectData* ugcProj, bool fixup );

// UGCProjectData utilities
UGCProjectInfo* ugcProjectDataGetProjectInfo( UGCProjectData* ugcProj );
const char* ugcProjectDataGetNamespace( UGCProjectData* ugcProj );
void ugcProjectDataGetInitialMapAndSpawn( const char** out_strInitialMapName, const char** out_strInitialSpawnPoint, UGCProjectData* ugcProj );
char **ugcProjectDataGetMaps( UGCProjectData* ugcProj );

// Misc utilities
UGCProjectData *ugcProjectLoadFromDir( const char *dir );
char* ugcAllocSMFString( const char* str, bool allowComplex );
void ugcResource_GetAudioAssets(const char **ppcType, const char ***peaStrings, U32 *puiNumData, U32 *puiNumDataWithAudio);
bool ugcPowerPropertiesIsUsedInUGC( WorldPowerVolumeProperties* props );

// This function is used only by STO.
void ugcProjectDataGetSTOGrantPrompt( const char** out_strCostumeName, const char** out_strPetCostume, const char** out_strBodyText, UGCProjectData* ugcProj );

void ugcEditorImportProjectSwitchNamespace(const char **res_name, const char *new_ns);

void ugcProjectDataNameSpaceChange(UGCProjectData *pUGCProjectData, const char *new_namespace);

// UGCProjectData is game-specific
extern ParseTable parse_UGCProjectData[];
#define TYPE_parse_UGCProjectData UGCProjectData
