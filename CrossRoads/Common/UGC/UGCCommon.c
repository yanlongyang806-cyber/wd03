#include "UGCCommon.h"

#include "AutoTransDefs.h"
#include "FolderCache.h"
#include "ResourceInfo.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "SubStringSearchTree.h"
#include "TextFilter.h"
#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"
#include "WorldLib.h"
#include "fileutil.h"
#include "mathutil.h"
#include "structInternals.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "wlBeacon.h"
#include "wlUGC.h"
#include "statistics.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

// Version numbers stored in TimeStamp. Can be used to
// force a project rebuild of a particular version.
#define UGC_SYSTEM_VERSION_MAJOR 1
#define UGC_SYSTEM_VERSION_MINOR 1

UGCProjectReportingDef g_ReportingDef = {0};

static bool g_UGCIsRepublishing = false;

static void ugcReporting_Load(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading UGCProjectReportingDef...");

	StructReset(parse_UGCProjectReportingDef, &g_ReportingDef);

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles(NULL,
					"defs/UGCReporting.def",
					"UGCReporting.bin",
					PARSER_OPTIONALFLAG,
					parse_UGCProjectReportingDef,
					&g_ReportingDef);

	loadend_printf(" done.");
}

AUTO_STARTUP(UGCReporting);
void ugcReportingStartup(void)
{
	if (IsServer())
	{
		ugcReporting_Load(NULL, 0);

		if (isDevelopmentMode())
		{
			FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/UGCReporting.def", ugcReporting_Load);
		}
	}
}

UGCProjectInfo *ugcCreateProjectInfo(UGCProject *active_project, const UGCProjectVersion *pVersion)
{
	char buf[MAX_PATH];
	const char *ns;
	UGCProjectInfo *info = NULL;

	if (!pVersion)
	{
		int latest_ver = eaSize(&active_project->ppProjectVersions)-1;
		assert(latest_ver >= 0);
		pVersion = active_project->ppProjectVersions[latest_ver];
	}

	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
	info = StructCreate(parse_UGCProjectInfo);
	ns = pVersion->pNameSpace;
	sprintf(buf, "%s:%s", ns, ns);
	info->pcName = allocAddString(buf);
	sprintf(buf, NAMESPACE_PATH "%s/UGC/%s.project", ns, ns);
	info->pcFilename = StructAllocString(buf);
	info->pOwnerAccountName = StructAllocString(active_project->pOwnerAccountName);
	info->pcPublicName = StructAllocString(UGCProject_GetVersionName(active_project, pVersion));
	info->iCreationTime = active_project->iCreationTime;
	info->bFromContainer = true;
	info->strDescription = StructAllocString(pVersion->pDescription);
	info->strNotes = StructAllocString(pVersion->pNotes);
	info->pMapLocation = ugcCreateMapLocation( pVersion->pMapLocation );
	if( pVersion->pLocation ){
		info->strSearchLocation = StructAllocString( pVersion->pLocation );
	}

	info->eLanguage = pVersion->eLanguage;
	eaCopyStructs(&active_project->ppProjectPermissions, &info->eaPermissions, parse_UGCProjectPermission);
	if( pVersion->pRestrictions ) {
		info->pRestrictionProperties = StructCreate( parse_WorldUGCRestrictionProperties );
		ugcRestrictionsWLFromContainer( info->pRestrictionProperties, pVersion->pRestrictions );
	}
	info->uLifetimeTipsReceived = active_project->ugcLifetimeTips;
	info->fAverageRating = active_project->ugcReviews.fAverageRating;

	return info;
}

UGCMapLocation* ugcCreateMapLocation( UGCProjectVersionMapLocation* pVersionMapLocation )
{
	if( pVersionMapLocation ) {
		UGCMapLocation* pMapLocation = StructCreate( parse_UGCMapLocation );
		pMapLocation->positionX = pVersionMapLocation->positionX;
		pMapLocation->positionY = pVersionMapLocation->positionY;
		pMapLocation->astrIcon = allocAddString( pVersionMapLocation->astrIcon );

		return pMapLocation;
	} else {
		return NULL;
	}
}

//////////////////////////////////////////////////////////////////////
//
//	UGC FILE LOADING
//    There are three ways we might load a project. In order:
//  1) Short name .gz.  Located in ns/<NameSpace>/project/<ShortName>.gz
//  2) Long name .gz.   Locaged in ns/<Namespace>/project/<NameSpace>.gz
//  3) Directory.		No unified file. Load from directory ns/<NameSpace>
//

void UGC_GetProjectZipFileName(const char *namespace, char *outStr, size_t outStr_size)
{
	char strIDStr[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];
	ContainerID iProjectID = UGCProject_GetContainerIDFromUGCNamespace(namespace);
	UGCIDString_IntToString(iProjectID, /*isSeries=*/false, strIDStr);
	sprintf_s(SAFESTR2(outStr), "ns/%s/project/%s.gz", namespace, strIDStr);
}

// Old-style .gz name. Used in STO. Possibly throughout Release 4.
static void UGC_GetProjectZipFileNameLegacy(const char *namespace, char *outStr, size_t outStr_size)
{
	sprintf_s(SAFESTR2(outStr), "ns/%s/project/%s.gz", namespace, namespace);
}

UGCProjectData *UGC_LoadProjectData(const char *namespace, const char *dirPrefix)
{
	char strFilename[MAX_PATH];
	char strFilenameLegacy[MAX_PATH];
	char fullFilename[MAX_PATH];
	UGCProjectData *pProjectData = NULL;

	UGC_GetProjectZipFileName(namespace, SAFESTR(strFilename));
	snprintf_s(fullFilename, MAX_PATH, "%s%s",dirPrefix,strFilename);
	
	if (!fileExists(fullFilename))
	{
		// Could not read file for some reason. Perhaps we need to look for the legacy file
		
		UGC_GetProjectZipFileNameLegacy(namespace, SAFESTR(strFilenameLegacy));
		snprintf_s(fullFilename, MAX_PATH, "%s%s",dirPrefix,strFilenameLegacy);
		
		if (!fileExists(fullFilename))
		{
			fullFilename[0]=0;
		}
	}

	if (fullFilename[0]!=0)
	{
		// Read a .gz file
		int iFileReadResult;
		pProjectData = StructCreate(parse_UGCProjectData);
		iFileReadResult = ParserReadZippedTextFile(fullFilename, parse_UGCProjectData, pProjectData, 0);
		if (iFileReadResult==0)
		{
			// Could not read file for some reason. No project
			StructDestroy(parse_UGCProjectData, pProjectData);
			pProjectData=NULL;
		}
	}
	else
	{
		// No .gz exists. Try reading the individual files from the directories
		char dirname[MAX_PATH];
		snprintf_s(dirname, MAX_PATH, "%sns/%s/ugc", dirPrefix, namespace);
		pProjectData=ugcProjectLoadFromDir(dirname);
	}

	return(pProjectData);
}

void UGCProject_FillInTimestamp(NOCONST(UGCTimeStamp) *pTimeStamp)
{
	pTimeStamp->iMajorVer = UGC_SYSTEM_VERSION_MAJOR;
	pTimeStamp->iMinorVer = UGC_SYSTEM_VERSION_MINOR;
	pTimeStamp->iTimestamp = timeSecondsSince2000();
	pTimeStamp->iWorldCellOverrideCRC = worldCellGetOverrideRebinCRC();
	pTimeStamp->pBuildVer = GetUsefulVersionString();
	pTimeStamp->iBeaconProcessVersion = beaconFileGetProcVersion();
}

void UGCProject_FillInTimestampPlusShardName(UGCTimeStampPlusShardName *pTimeStampPlusShardName)
{
	UGCProject_FillInTimestamp(CONTAINER_NOCONST(UGCTimeStamp, &pTimeStampPlusShardName->timeStamp));
	pTimeStampPlusShardName->pShardName = strdup(GetShardNameFromShardInfoString());
}

char *UGCProject_GetTimestampPlusShardNameStringEscaped(void)
{
	static char *pRetString = NULL;
	if (!pRetString)
	{
		UGCTimeStampPlusShardName temp = {0};
		StructInit(parse_UGCTimeStampPlusShardName, &temp);
		UGCProject_FillInTimestampPlusShardName(&temp);
		ParserWriteTextEscaped(&pRetString, parse_UGCTimeStampPlusShardName, &temp, 0, 0, 0);
		StructDeInit(parse_UGCTimeStampPlusShardName, &temp);
	}

	return pRetString;
}

bool UGCProject_TimeStampPlusShardNameIsValidForSafeImport(UGCTimeStampPlusShardName *pTimeStamp)
{
	return false;
}


static bool UGCProject_ValidateAndFixupFilterForSimpleStringField(UGCProjectSearchFilter *pFilter,
																  const char** pastrErrorMessageKey,
																  int iMinLength)
{
	static char *pInternalString = NULL;

	if (!pFilter->pStrValue || !pFilter->pStrValue[0])
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_RequiresStringValue" );
		return false;
	}

	estrClear(&pInternalString);
	SSSTree_InternalizeString(&pInternalString, pFilter->pStrValue);

	if (stricmp(pInternalString, pFilter->pStrValue) != 0)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_NonSSSTreeInternalizedSearchString" );
		return false;
	}

	if ((int)estrLength(&pInternalString) < iMinLength)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_MinCharacterLimit" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_CONTAINS:
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	return true;
}



static bool UGCProject_ValidateAndFixupFilterForStringField(UGCProjectSearchFilter *pFilter,
															const char **pastrErrorMessageKey,
															int iMinLength,
															bool bForSSSTree)
{
	static char *pInternalString = NULL;

	if (!pFilter->pStrValue || !pFilter->pStrValue[0])
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_RequiresStringValue" );
		return false;
	}

	estrClear(&pInternalString);

	if (bForSSSTree)
	{
		SSSTree_InternalizeString(&pInternalString, pFilter->pStrValue);
	}
	else
	{
		estrCopy2(&pInternalString, pFilter->pStrValue);
	}

	if ((int)estrLength(&pInternalString) < iMinLength)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_MinCharacterLimit" );
		return false;
	}

	if (pFilter->eType != UGCFILTER_STRING)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_RequiresStringField" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_CONTAINS:
	case UGCCOMPARISON_NOTCONTAINS:
	case UGCCOMPARISON_BEGINSWITH:
	case UGCCOMPARISON_ENDSWITH:
		//these are all OK
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	estrCopy(&pFilter->pStrValue, &pInternalString);

	return true;
}




static bool UGCProject_ValidateAndFixupFilterForTagsField(UGCProjectSearchFilter *pFilter,
														  const char **pastrErrorMessageKey)
{
	if (pFilter->eType != UGCFILTER_TAGS)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_InvalidTagsField" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_N_TAGS_ON:
	case UGCCOMPARISON_N_TAGS_OFF:
		if (!estrLength(&pFilter->pStrValue) || pFilter->uIntValue < 1)
		{
			*pastrErrorMessageKey = allocAddString( "UGCSearchError_InvalidTagOrValue" );
			return false;
		}
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	return true;
}

static bool UGCProject_ValidateAndFixupFilterForRatingField(UGCProjectSearchFilter *pFilter,
															const char **pastrErrorMessageKey)
{
	if (pFilter->fFloatValue<0 ||pFilter->fFloatValue>1)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_FilterOutOfBounds" );
		return false;
	}

	if (pFilter->eType != UGCFILTER_RATING)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_InvalidRatingField" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_LESSTHAN:
	case UGCCOMPARISON_GREATERTHAN:
		//these are all OK
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	return true;
}


static bool UGCProject_ValidateAndFixupFilterForAveragePlaytime(UGCProjectSearchFilter *pFilter,
																const char **pastrErrorMessageKey)
{
	if (pFilter->fFloatValue<0)
	{
		*pastrErrorMessageKey = "UGCSearchError_FilterOutOfBounds";
		return false;
	}

	if (pFilter->eType != UGCFILTER_AVERAGEPLAYTIME)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_InvalidDurationField" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_LESSTHAN:
	case UGCCOMPARISON_GREATERTHAN:
		//these are all OK
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	return true;
}


static bool UGCProject_ValidateAndFixupFilterForPermissionsField(UGCProjectSearchFilter *pFilter,
																 const char **pastrErrorMessageKey)
{
	if (pFilter->uIntValue<0 ||pFilter->uIntValue>1)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_FilterOutOfBounds" );
		return false;
	}

	if (pFilter->eType != UGCFILTER_PERMISSIONS)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_InvalidPermissionField" );
		return false;
	}

	switch (pFilter->eComparison)
	{
	case UGCCOMPARISON_LESSTHAN:
	case UGCCOMPARISON_GREATERTHAN:
		//these are all OK
		break;
	default:
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnsupportedSearchType" );
		return false;
	}

	return true;
}

bool UGCProject_ValidateAndFixupSearchInfo(UGCProjectSearchInfo *pSearchInfo, const char** pastrErrorMessageKey)
{
	int i;

	if (pSearchInfo->iPlayerLevelMax && pSearchInfo->iPlayerLevelMin > pSearchInfo->iPlayerLevelMax)
	{
		*pastrErrorMessageKey = allocAddString( "UGCSearchError_FilterOutOfBounds");
		return false;
	}

	for (i=0; i < eaSize(&pSearchInfo->ppFilters); i++)
	{
		UGCProjectSearchFilter *pFilter = pSearchInfo->ppFilters[i];

		if (pFilter->eType == UGCFILTER_SIMPLESTRING)
		{
			if (!UGCProject_ValidateAndFixupFilterForSimpleStringField(pFilter, pastrErrorMessageKey, UGCPROJ_MIN_SIMPLE_SEARCH_STRING_LEN))
			{
				return false;
			}
			else
			{
				continue;
			}
		}

		if (stricmp_safe(pFilter->pField, "name") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForStringField(pFilter, pastrErrorMessageKey, UGCPROJ_MIN_NAME_SEARCH_STRING_LEN, true))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "description") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForStringField(pFilter, pastrErrorMessageKey, UGCPROJ_MIN_DESCRIPTION_SEARCH_STRING_LEN, false))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "author") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForStringField(pFilter, pastrErrorMessageKey, UGCPROJ_MIN_AUTHOR_NAME_SEARCH_STRING_LEN, true))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "rating") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForRatingField(pFilter, pastrErrorMessageKey))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "ReadPermission") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForPermissionsField(pFilter, pastrErrorMessageKey))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "tags") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForTagsField(pFilter, pastrErrorMessageKey))
			{
				return false;
			}
		}
		else if (stricmp_safe(pFilter->pField, "AveragePlaytime") == 0)
		{
			if (!UGCProject_ValidateAndFixupFilterForAveragePlaytime(pFilter, pastrErrorMessageKey))
			{
				return false;
			}
		}
		else
		{
			*pastrErrorMessageKey = allocAddString( "UGCSearchError_UnknownField" );
			return false;
		}
	}

	if (pSearchInfo->pSimple_Raw && pSearchInfo->pSimple_Raw[0])
	{
		static char *pSSSTreeString = NULL;

		estrClear(&pSSSTreeString);
		SSSTree_InternalizeString(&pSSSTreeString, pSearchInfo->pSimple_Raw);

		if (estrLength(&pSSSTreeString) < UGCPROJ_MIN_AUTHOR_NAME_SEARCH_STRING_LEN)
		{
			*pastrErrorMessageKey = "UGCSearchError_SimpleStringTooShort";
			return false;
		}

		SAFE_FREE(pSearchInfo->pSimple_SSSTree);
		pSearchInfo->pSimple_SSSTree = strdup(pSSSTreeString);
	}

	return true;
}

bool UGCProject_CanAutoDelete(const UGCProject *pUGCProject)
{
	return pUGCProject->iDeletionTime || pUGCProject->bUGCFeaturedCopyProjectInProgress;
}

bool UGCProjectSeries_CanAutoDelete(const UGCProjectSeries *pUGCProjectSeries)
{
	return eaSize(&pUGCProjectSeries->eaVersions) == 0 || pUGCProjectSeries->iDeletionTime != 0;
}

const char *UGCProject_GetMostRecentNamespace(const UGCProject *pProject)
{
	UGCProjectVersion *pMostRecentVersion = eaTail(&pProject->ppProjectVersions);

	if (!pMostRecentVersion)
	{
		AssertOrAlert("UGC_BAD_PROJECT", "UGC project has no versions");
		return "";
	}

	return pMostRecentVersion->pNameSpace;
}

const UGCProjectVersion *UGCProject_GetMostRecentVersion(const UGCProject *pProject)
{
	UGCProjectVersion *pMostRecentVersion = eaTail(&pProject->ppProjectVersions);

	if (!pMostRecentVersion)
	{
		AssertOrAlert("UGC_BAD_PROJECT", "UGC project has no versions");
		return NULL;
	}

	return pMostRecentVersion;
}

bool UGCProject_IsPublishedAndNotDeleted(const UGCProject *pProject)
{
	return UGCProject_GetMostRecentPublishedVersion(pProject) && !UGCProject_CanAutoDelete(pProject);
}

const UGCProjectVersion *UGCProject_GetMostRecentPublishedVersion(const UGCProject *pProject)
{
	int i;

	if( pProject )
	{
		for (i=eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
		{
			if (ugcProjectGetVersionStateConst(pProject->ppProjectVersions[i]) == UGC_PUBLISHED)
			{
				return pProject->ppProjectVersions[i];
			}
		}
	}

	return NULL;
}

const char* UGCProject_GetVersionName(const UGCProject *pProject, const UGCProjectVersion* pVersion)
{
	if( pVersion && !nullStr( pVersion->pName )) {
		return pVersion->pName;
	}
	if( pProject && !nullStr( pProject->pPublishedVersionName )) {
		return pProject->pPublishedVersionName;
	}
	return NULL;
}


bool UGCProject_ValidatePotentialName(const char *pName, bool bIsSeries, char **ppErrorString)
{
	static char *pSSSName = NULL;
	static char *pNameMutable = NULL;

	// Check for pName being NULL as the estrPrintf will actually return a valid string in that case
	if (pName==NULL)
	{
		estrPrintf(ppErrorString, "Name was not specified.");
		return false;
	}

	estrClear(&pSSSName);
	estrPrintf(&pNameMutable, "%s", pName);

	estrTrimLeadingAndTrailingWhitespace(&pNameMutable);

	if (strlen(pNameMutable) < UGCPROJ_MIN_NAME_LENGTH)
	{
		estrPrintf(ppErrorString, "Names must be at least %d characters long.", UGCPROJ_MIN_NAME_LENGTH);
		return false;
	}

	if (strlen(pNameMutable) > UGCPROJ_MAX_NAME_LENGTH)
	{
		estrPrintf(ppErrorString, "Names cannot be more than %d characters long.", UGCPROJ_MAX_NAME_LENGTH);
		return false;
	}

	if(IsAnyProfane(pNameMutable))
	{
		estrPrintf(ppErrorString, "Names cannot contain any profanity.");
		return false;
	}

	else if(IsAnyRestricted(pNameMutable))
	{
		estrPrintf(ppErrorString, "Names cannot contain any restricted words.");
		return false;
	}

	SSSTree_InternalizeString(&pSSSName, pNameMutable);

	if (estrLength(&pSSSName) < UGCPROJ_MIN_NAME_SEARCH_STRING_LEN)
	{
		estrPrintf(ppErrorString, "Name has too few alphanumeric characters.");
		return false;
	}

	return true;
}

// Get log information about a project ban as a string
AUTO_TRANS_HELPER_SIMPLE;
void UGCProject_GetBanStatusString(ContainerID uProjectID,
								   ContainerID uOwnerAccountID,
								   const char* pchOwnerAccountName,
								   const char* pchCSRAccountName,
								   S32 iNaughtyValue,
								   U32 uTemporaryBanExpireTime,
								   bool bBanState,
								   bool bTempoaryBan,
								   char** pestrResult)
{
	char strIdString[UGC_IDSTRING_LENGTH_BUFFER_LENGTH];
	const char* pchBanState;

	UGCIDString_IntToString(uProjectID, /*isSeries=*/false, strIdString);

	if (bBanState && bTempoaryBan) {
		pchBanState = "temporarily banned";
	} else if (bBanState) {
		pchBanState = "permanently banned";
	} else if (bTempoaryBan) {
		pchBanState = "unbanned (temporary ban)";
	} else {
		pchBanState = "unbanned (permanent ban)";
	}
	estrConcatf(pestrResult, "Project %s(%d) owned by account %s(%d) was %s",
		strIdString, uProjectID, pchOwnerAccountName, uOwnerAccountID, pchBanState);

	if (pchCSRAccountName && pchCSRAccountName[0]) {
		estrConcatf(pestrResult, " by %s. ", pchCSRAccountName);
	} else if (bBanState) {
		estrConcatf(pestrResult, " automatically. ");
	} else {
		estrConcatf(pestrResult, ". ");
	}
	if (bBanState && uTemporaryBanExpireTime > 0) {
		estrConcatf(pestrResult, " Ban expiration time: %s. ",
			timeGetDateStringFromSecondsSince2000(uTemporaryBanExpireTime));
	}
	estrConcatf(pestrResult, "Current naughty value: %d.", iNaughtyValue);
}

const UGCProjectSeriesVersion *UGCProjectSeries_GetMostRecentPublishedVersion(const UGCProjectSeries *pSeries)
{
	if( pSeries ) {
		int it;
		for( it = eaSize( &pSeries->eaVersions ) - 1; it >= 0; --it ) {
			const UGCProjectSeriesVersion* pVersion = pSeries->eaVersions[ it ];
			if( pVersion->eState == UGC_PUBLISHED ) {
				return pVersion;
			}
		}
	}

	return NULL;
}

const char* UGCProjectSeries_GetVersionName(const UGCProjectSeries *pProjectSeries, const UGCProjectSeriesVersion* pVersion)
{
	if( pVersion && !nullStr( pVersion->strName )) {
		return pVersion->strName;
	}
	return NULL;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Ppprojectversions");
NOCONST(UGCProjectVersion) *UGCProject_trh_GetSpecificVersion(ATR_ARGS, ATH_ARG NOCONST(UGCProject) *pProject, const char *pNameSpace)
{
	int i;

	for (i = eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
	{
		if (stricmp(pProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			return pProject->ppProjectVersions[i];
		}
	}
	return NULL;
}

int UGCProject_GetVersionIndex(const UGCProject *pProject, const char *pNameSpace)
{
	int i;

	for (i = eaSize(&pProject->ppProjectVersions) - 1; i >= 0; i--)
	{
		if (stricmp(pProject->ppProjectVersions[i]->pNameSpace, pNameSpace) == 0)
		{
			return i;
		}
	}

	return -1;
}

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Ugcreporting.Eareports");
int UGCProject_trh_FindReportByAccountID(ATH_ARG NOCONST(UGCProject)* pProject, U32 uAccountID)
{
	int i;
	for (i = eaSize(&pProject->ugcReporting.eaReports)-1; i >= 0; i--)
	{
		NOCONST(UGCProjectReport)* pReport = pProject->ugcReporting.eaReports[i];

		if (pReport->uAccountID == uAccountID)
		{
			return i;
		}
	}
	return -1;
}

int ugcReviews_SortByTimestamp( const UGCSingleReview **review1, const UGCSingleReview **review2 )
{
	if(!review1 || !(*review1)) {
		if(!review2 || !(*review2)) {
			return 0;
		} else {
			return 1;
		}
	} else {
		if(!review2 || !(*review2)) {
			return -1;
		} else {
			if((*review1)->iTimestamp == (*review2)->iTimestamp) {
				return 0;
			} else if((*review1)->iTimestamp < (*review2)->iTimestamp) {
				return 1;
			} else {
				return -1;
			}
		}
	}
}

// MJF TODO: remove this function eventually
#define UGC_REVIEWS_PER_PAGE 10
void ugcReviews_GetForPage( const UGCProjectReviews* pReviews, S32 iPageNumber, NOCONST(UGCProjectReviews)* out_pReviews )
{
	S32 i, iStart, iNumPages;
	UGCSingleReview** eaReviews = NULL;
	eaCopy(&eaReviews, &pReviews->ppReviews);

	// Only consider reviews with comments
	for (i = eaSize(&eaReviews)-1; i >= 0; i--)
	{
		UGCSingleReview* pReview = eaReviews[i];
		// The following condition should match the one in the following function GetReviewPageCount
		if (pReview->bHidden || !pReview->pComment || !pReview->pComment[0])
		{
			eaRemove(&eaReviews, i);
		}
	}
	iNumPages = (S32)ceil( eaSize( &eaReviews ) / (F32)UGC_REVIEWS_PER_PAGE );

	// Sort the reviews from newest to oldest
	eaQSort( eaReviews, ugcReviews_SortByTimestamp );

	// Remove all reviews except those residing on the requested page
	if( iPageNumber >= 0 ) {
		iStart = UGC_REVIEWS_PER_PAGE * MIN(iPageNumber, iNumPages);
		if (iStart)
		{
			eaRemoveRange(&eaReviews, 0, iStart);
		}
		eaRemoveTail(&eaReviews, UGC_REVIEWS_PER_PAGE);
		eaCopyStructsDeConst(&eaReviews, &out_pReviews->ppReviews, parse_UGCSingleReview);
		eaDestroy(&eaReviews);
	}

	eaiCopy( &out_pReviews->piNumRatings, &pReviews->piNumRatings );
	out_pReviews->iNumReviewPagesCached = ugcReviews_GetPageCount( pReviews );
	out_pReviews->iNumRatingsCached = ugcReviews_GetRatingCount( CONTAINER_NOCONST( UGCProjectReviews, pReviews ));
}

S32 ugcReviews_GetPageCount( const UGCProjectReviews* pReviews )
{
	S32 i, iNumReviews;

	// Only consider reviews with comments
	iNumReviews = 0;
	for( i = eaSize( &pReviews->ppReviews )-1; i >= 0; i--) {
		const UGCSingleReview* pReview = pReviews->ppReviews[i];
		// The following condition should match the one in the preceding function GetForPage
		if (!(pReview->bHidden || !pReview->pComment || !pReview->pComment[0])) {
			++iNumReviews;
		}
	}

	return (S32)ceil(iNumReviews/(F32)UGC_REVIEWS_PER_PAGE);
}

AUTO_TRANS_HELPER;
S32 ugcReviews_GetRatingCount(ATH_ARG NOCONST(UGCProjectReviews) *pReviews)
{
	S32 i, iCount = 0;
	for (i = ea32Size(&pReviews->piNumRatings)-1; i >= 0; i--) {
		iCount += pReviews->piNumRatings[i];
	}
	return iCount;
}

static F32 g_bUGCConfidenceValueForLowerBoundOfConfidenceInterval = 0.95f; // if you adjust the default, please adjust the default of g_bUGCZValueForLowerBoundOfConfidenceInterval
AUTO_CMD_FLOAT(g_bUGCConfidenceValueForLowerBoundOfConfidenceInterval, UGCConfidenceValueForLowerBoundOfConfidenceInterval) ACMD_CALLBACK(UGCConfidenceValueForLowerBoundOfConfidenceIntervalChanged);

static F32 g_bUGCZValueForLowerBoundOfConfidenceInterval = 1.96f; // if you adjust the default, please adjust the default of g_bUGCConfidenceValueForLowerBoundOfConfidenceInterval
AUTO_CMD_FLOAT(g_bUGCZValueForLowerBoundOfConfidenceInterval, UGCZValueForLowerBoundOfConfidenceInterval) ACMD_CALLBACK(UGCZValueForLowerBoundOfConfidenceIntervalChanged);

void UGCConfidenceValueForLowerBoundOfConfidenceIntervalChanged(void)
{
	g_bUGCZValueForLowerBoundOfConfidenceInterval = statisticsPNormalDist(1 - (1 - g_bUGCConfidenceValueForLowerBoundOfConfidenceInterval) / 2);
}

void UGCZValueForLowerBoundOfConfidenceIntervalChanged(void)
{
	g_bUGCConfidenceValueForLowerBoundOfConfidenceInterval = 1 + 2 * (statisticsNormalDist(g_bUGCZValueForLowerBoundOfConfidenceInterval) - 1);
}

AUTO_TRANS_HELPER;
F32 ugcReviews_ComputeAdjustedRatingUsingConfidence( ATH_ARG NOCONST(UGCProjectReviews)* pReviews )
{
	S32 total_ratings = ugcReviews_GetRatingCount(pReviews);
	if(total_ratings > 0)
	{
		F32 phat = pReviews->fAverageRating;
		F32 z = g_bUGCZValueForLowerBoundOfConfidenceInterval;
		F32 z_sqr = z * z;

		return (phat + z_sqr / (2 * total_ratings) - z * sqrt((phat * (1 - phat) + z_sqr / (4 * total_ratings)) / total_ratings)) / (1 + z_sqr / total_ratings);
	}
	else
		return 0.0f;
}

// Find the bucket that this rating belongs in
S32 ugcReviews_FindBucketForRating(F32 fRating)
{
	if (fRating > 0.0f)
	{
		F32 fBucketInterval = 1/(F32)UGCPROJ_NUM_RATING_BUCKETS;
		S32 i;
		for (i = 0; i < UGCPROJ_NUM_RATING_BUCKETS; i++)
		{
			if (fRating < fBucketInterval*(i+1)+0.001f)
			{
				return i;
			}
		}
	}
	return -1;
}

static const char* ugcFactionRestrictionName( const WorldUGCFactionRestrictionProperties* faction )
{
	return faction->pcFaction;
}

/// Returns false if there is no intersection
void ugcRestrictionsIntersect(WorldUGCRestrictionProperties* prop_accum, const WorldUGCRestrictionProperties* prop)
{
	if( prop->iMinLevel > prop_accum->iMinLevel || prop_accum->iMinLevel == 0 ) {
		prop_accum->iMinLevel = prop->iMinLevel;
	}

	// Don't allow the restrictions to become invalid
	if( prop->iMaxLevel < prop_accum->iMaxLevel || prop_accum->iMaxLevel == 0 ) {
		prop_accum->iMaxLevel = prop->iMaxLevel;
	}

	if( eaSize( &prop->eaFactions )) {
		if( eaSize( &prop_accum->eaFactions ) == 0 ) {
			eaCopyStructs( &prop->eaFactions, &prop_accum->eaFactions, parse_WorldUGCFactionRestrictionProperties );
		} else {
			WorldUGCFactionRestrictionProperties** intersection = NULL;
			WorldUGCFactionRestrictionProperties** oldProperties = prop_accum->eaFactions;
			eaIntersectAddrEx( &prop_accum->eaFactions, ugcFactionRestrictionName, (void***)&prop->eaFactions, ugcFactionRestrictionName,
							   &intersection );
			prop_accum->eaFactions = NULL;

			if( eaSize( &intersection ) == 0 ) {
				eaPush( &prop_accum->eaFactions, StructCreate( parse_WorldUGCFactionRestrictionProperties ));
			} else {
				eaCopyStructs( &intersection, &prop_accum->eaFactions, parse_WorldUGCFactionRestrictionProperties );
			}
			eaDestroyStruct( &oldProperties, parse_WorldUGCFactionRestrictionProperties );
		}
	}
}

void ugcRestrictionsIntersectContainer(WorldUGCRestrictionProperties* prop_accum, const UGCProjectVersionRestrictionProperties* prop)
{
	WorldUGCRestrictionProperties wlProp = { 0 };

	ugcRestrictionsWLFromContainer( &wlProp, prop );
	ugcRestrictionsIntersect( prop_accum, &wlProp );
	StructReset( parse_WorldUGCRestrictionProperties, &wlProp );
}

void ugcRestrictionsWLFromContainer(WorldUGCRestrictionProperties* out_prop, const UGCProjectVersionRestrictionProperties* prop)
{
	StructReset( parse_WorldUGCRestrictionProperties, out_prop );
	if( !prop ) {
		return;
	}

	out_prop->iMinLevel = prop->iMinLevel;
	out_prop->iMaxLevel = prop->iMaxLevel;
	{
		int it;
		for( it = 0; it != eaSize( &prop->eaFactions ); ++it ) {
			WorldUGCFactionRestrictionProperties* out_faction = StructCreate( parse_WorldUGCFactionRestrictionProperties );
			out_faction->pcFaction = allocAddString( prop->eaFactions[ it ]->pcFaction );
			eaPush( &out_prop->eaFactions, out_faction );
		}
	}
}

void ugcRestrictionsContainerFromWL(NOCONST(UGCProjectVersionRestrictionProperties)* out_prop, const WorldUGCRestrictionProperties* prop)
{
	StructResetNoConst( parse_UGCProjectVersionRestrictionProperties, out_prop );

	if( !prop ) {
		return;
	}

	out_prop->iMinLevel = prop->iMinLevel;
	out_prop->iMaxLevel = prop->iMaxLevel;
	{
		int it;
		for( it = 0; it != eaSize( &prop->eaFactions ); ++it ) {
			NOCONST(UGCProjectVersionFactionRestrictionProperties)* out_faction = StructCreateNoConst( parse_UGCProjectVersionFactionRestrictionProperties );
			out_faction->pcFaction = StructAllocString( prop->eaFactions[ it ]->pcFaction );
			eaPush( &out_prop->eaFactions, out_faction );
		}
	}
}

bool ugcRestrictionsIsValid(WorldUGCRestrictionProperties* prop)
{
	if( prop->iMinLevel && prop->iMaxLevel && prop->iMinLevel > prop->iMaxLevel ) {
		return false;
	}
	if( eaSize( &prop->eaFactions ) > 0 && prop->eaFactions[ 0 ]->pcFaction == NULL ) {
		return false;
	}

	return true;
}

void ugcSetIsRepublishing(bool republishing)
{
	g_UGCIsRepublishing = republishing;
}

bool ugcGetIsRepublishing(void)
{
	return g_UGCIsRepublishing;
}

#define UGC_MAX_REPORT_DETAILS_LENGTH 100

AUTO_TRANS_HELPER
ATR_LOCKS(pProject, ".Iowneraccountid, .Bbanned, .Ugcreporting.Eareports");
bool UGCProject_trh_CanMakeReport(ATR_ARGS,
	ATH_ARG NOCONST(UGCProject)* pProject,
	U32 uAccountID,
	U32 eReason,
	const char* pchDetails)
{
	const char* pchReason = StaticDefineIntRevLookup(UGCProjectReportReasonEnum, eReason);
	if (!pchReason || !pchReason[0])
	{
		return false;
	}
	if (pchDetails && strlen(pchDetails) > UGC_MAX_REPORT_DETAILS_LENGTH)
	{
		return false;
	}
	if (NONNULL(pProject))
	{
		if (pProject->iOwnerAccountID == uAccountID || pProject->bBanned)
		{
			return false;
		}
		if (UGCProject_trh_FindReportByAccountID(pProject, uAccountID) >= 0)
		{
			return false;
		}
	}
	return true;
}

void ugcEditorImportProjectSwitchNamespace(const char **res_name, const char *new_ns)
{
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE], buf[RESOURCE_NAME_MAX_SIZE];
	resExtractNameSpace(*res_name, ns, base);
	sprintf(buf, "%s:%s", new_ns, base);
	*res_name = allocAddString(buf);
}

#include "UGCCommon_h_ast.c"
