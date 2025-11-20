#include "gslUGCTransactions.h"

#include "Error.h"
#include "earray.h"
#include "Entity.h"
#include "Entity_h_ast.h"
#include "EntityLib.h"
#include "estring.h"
#include "serverLib.h"
#include "gameServerLib.h"
#include "localTransactionManager.h"
#include "AutoGen/GameServerLib_autogen_remotefuncs.h"
#include "Autogen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/ServerLib_autogen_RemoteFuncs.h"
#include "objTransactions.h"

#include "UGCProjectCommon.h"
#include "UGCCommon.h"
#include "staticWorld/worldGridPrivate.h"
#include "UGCProjectCommon_h_ast.h"
#include "pcl_client.h"
#include "UUID.h"
#include "JobManagerSupport.h"
#include "../../crossroads/appServerLib/pub/aslJobManagerPub.h"
#include "aslJobManagerPub_h_Ast.h"
#include "logging.h"
#include "mapdescription_h_ast.h"
#include "RemoteCommandGRoup.h"
#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "StringCache.h"
#include "Player.h"
#include "Player_h_ast.h"
#include "StringFormat.h"
#include "SubStringSearchTree.h"
#include "ticketnet.h"
#include "ticketenums.h"
#include "tokenstore.h"
#include "gslUGC.h"
#include "wlUGC.h"
#include "gslEditor.h"
#include "ContinuousBuilderSupport.h"
#include "AutoTransDefs.h"
#include "gameStringFormat.h"
#include "fileutil2.h"
#include "StringUtil.h"
#include "GameAccountData\GameAccountData.h"
#include "AutoGen/GameAccountData_h_ast.h"
#include "UGCAchievements.h"

extern ParseTable parse_TicketData[];
#define TYPE_parse_TicketData TicketData

static bool sbDoSyncAtEndOfPublishing = false;
AUTO_CMD_INT(sbDoSyncAtEndOfPublishing, DoSyncAtEndOfPublishing);

static bool sbVerbosePublishUpdatesToClient = false;
AUTO_CMD_INT(sbVerbosePublishUpdatesToClient, VerbosePublishUpdatesToClient);

static bool sbPublishSkipBeaconizing = false;
AUTO_CMD_INT(sbPublishSkipBeaconizing, PublishSkipBeaconizing);

// #define UGC_QUERY_DEBUG_OUTPUT 1


static bool gslUGC_UploadResources(const char *pNameSpace, bool bPublishData, char **estrErrorMessage, char *pComment_in, ...)
{
	char** files = NULL;
	char ns_root[MAX_PATH];
	char ns_root_full[MAX_PATH];
	char *pCommentToUse = NULL;

	if (!isProductionEditMode())
	{
		return false;
	}

	estrGetVarArgs(&pCommentToUse, pComment_in);

	loadstart_printf("Saving Resources for %s to UGC PatchDB...\n", pNameSpace);
	sprintf(ns_root, "%s:/", pNameSpace);
	fileLocateWrite(ns_root, ns_root_full);
	if (bPublishData)
	{
		eaPush(&files, strdupf("%s/maps", ns_root_full));
		eaPush(&files, strdupf("%s/ai", ns_root_full));
	}
	eaPush(&files, strdupf("%s/defs", ns_root_full));
	eaPush(&files, strdupf("%s/autosave", ns_root_full));
	eaPush(&files, strdupf("%s/project", ns_root_full));

	TellControllerToLog( __FUNCTION__ ": About to call ServerLibPatchUpload" );
	if (!ServerLibPatchUpload(files, "GameServer", estrErrorMessage, "UploadResources for ns %s(%s)", pNameSpace, pCommentToUse))
	{
		TellControllerToLog( __FUNCTION__ ": ServerLibPatchUpload failed." );
		return false;
	}
	loadend_printf("Saving Resources for %s to UGC PatchDB...done.\n", pNameSpace);
	TellControllerToLog( __FUNCTION__ ": ServerLibPatchUpload succeeded." );

	eaDestroyEx(&files, NULL);
	estrDestroy(&pCommentToUse);
	return true;
}

bool gslUGC_UploadAutosave(const char *pNameSpace, char **estrErrorMessage)
{
	char** files = NULL;
	char ns_root[MAX_PATH];
	char ns_root_full[MAX_PATH];

	if (!isProductionEditMode())
	{
		return false;
	}

	loadstart_printf("Auto-saving Resources for %s to UGC PatchDB...\n", pNameSpace);
	sprintf(ns_root, "%s:/", pNameSpace);
	fileLocateWrite(ns_root, ns_root_full);
	eaPush(&files, strdupf("%s/autosave", ns_root_full));

	TellControllerToLog( __FUNCTION__ ": About to call ServerLibPatchUpload" );
	if (!ServerLibPatchUpload(files, "GameServer", estrErrorMessage, "UploadAutosave for ns %s", pNameSpace))
	{
		TellControllerToLog( __FUNCTION__ ": ServerLibPatchUpload failed." );
		return false;
	}
	loadend_printf("Auto-saving Resources for %s to UGC PatchDB...done.\n", pNameSpace);
	TellControllerToLog( __FUNCTION__ ": ServerLibPatchUpload succeeded." );

	eaDestroyEx(&files, NULL);
	return true;
}

bool gslUGC_ForkNamespace(UGCProjectData *pData, InfoForUGCProjectSaveOrPublish *pInfoForPublish, bool bUpload)
{
	char *pcError = NULL;
	if (pInfoForPublish->pPublishNameSpace)
		resNameSpaceGetOrCreate(pInfoForPublish->pPublishNameSpace);
	gslUGC_RenameProjectNamespace(pData, pInfoForPublish->pPublishNameSpace);

	TellControllerToLog( __FUNCTION__ ": About to generate." );
	if (!ugcProjectGenerateOnServer(pData))
	{
		AssertOrAlert("UGC_PUBLISH_GENERATE_FAILED", "There were unexpected errors in ugcProjectGenerateOnServer during a publish - aborting.");
		TellControllerToLog( __FUNCTION__ ": Generate failed." );
		return false;
	}
	TellControllerToLog( __FUNCTION__ ": Generate succeeded." );

	// To find all the places you need to update to add a per
	// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
	//
	// Here set fields that are generated by ugcProjectGenerateOnServer
	{
		char* strInitialMapName = NULL;
		char* strInitialSpawnPoint = NULL;

		ugcProjectDataGetInitialMapAndSpawn( &strInitialMapName, &strInitialSpawnPoint, pData );
		pInfoForPublish->strInitialMapName = StructAllocString( strInitialMapName );
		pInfoForPublish->strInitialSpawnPoint = StructAllocString( strInitialSpawnPoint );
	}


	if (bUpload)
	{
		char strFilename[MAX_PATH];

		// Save the project data as one zipped file
		UGC_GetProjectZipFileName(pInfoForPublish->pPublishNameSpace, SAFESTR(strFilename));
		if (!ParserWriteZippedTextFile(strFilename, parse_UGCProjectData, pData, 0, 0))
		{
			AssertOrAlert("UGC_RESOURCE_SAVE_FAILED", "Can't save project resource: Can't save project resource file.");
			gslUGC_DeleteNamespaceDataFiles(pInfoForPublish->pPublishNameSpace);
			return false;
		}

		if (!gslUGC_UploadResources(pInfoForPublish->pPublishNameSpace, true, &pcError, "Publishing project %u", UGCProject_GetContainerIDFromUGCNamespace( pInfoForPublish->sProjectInfo.pcName )))
		{
			AssertOrAlert("UGC_RESOURCE_SAVE_FAILED", "Can't save project resource: Error uploading resources to ugcmaster: %s", pcError);
		}

		// Clear files from disk
		gslUGC_DeleteNamespaceDataFiles(pInfoForPublish->pPublishNameSpace);
	}
	return true;
}

bool gslUGC_DoSaveForProject(UGCProject *pProject, UGCProjectData *data, InfoForUGCProjectSaveOrPublish *pInfo, ContainerID *pID, bool bPublish, char **pcOutError, const char* strReason)
{
	const char *pNameSpace;
	char *pcError = NULL;
	UGCProjectAutosaveData pWriteData = { 0 };
	char filename[MAX_PATH];
	const UGCProjectVersion *pPrevVersion = NULL;

	if(!pProject)
	{
		AssertOrAlert("NO_UGC_PROJECT", "Can't save project when there is no UGCProject, Reason: %s", strReason);
		if (pcOutError)
			estrAppend2(pcOutError, "There is no active project");
		return false;
	}

	pNameSpace = UGCProject_GetMostRecentNamespace(pProject);
	pPrevVersion = UGCProject_GetMostRecentVersion(pProject);

	if (stricmp(pNameSpace, ugcProjectDataGetNamespace(data)) != 0)
	{
		AssertOrAlert("UGC_BAD_NAMESPACE", "Attempting to save a UGCProject with the wrong namespace: %s (should be %s) Reason: %s", ugcProjectDataGetNamespace(data), pNameSpace, strReason);
		if (pcOutError)
			estrAppend2(pcOutError, "Project has incorrect namespace");
		return false;
	}

	TellControllerWeMayBeStallyForNSeconds(giUGCSaveStallySeconds, "UGCSave");


	SAFE_FREE(gServerLibState.pcEditingNamespace);
	gServerLibState.pcEditingNamespace = strdup(pNameSpace);

	// Write an *empty* autosave file
	sprintf(filename, "ns/%s/autosave/autosave.ugcproject", pNameSpace);
	ParserWriteTextFile(filename, parse_UGCProjectAutosaveData, &pWriteData, 0, 0);

	// Save the project data as one zipped file
	UGC_GetProjectZipFileName(pNameSpace, SAFESTR(filename));
	if (!ParserWriteZippedTextFile(filename, parse_UGCProjectData, data, 0, 0))
	{
		AssertOrAlert("UGC_RESOURCE_SAVE_FAILED", "Can't save project resource: Can't save project resource file.");
		gslUGC_DeleteNamespaceDataFiles(pNameSpace);
		return false;
	}

	// Send resources up to patchserver
	if (!gslUGC_UploadResources(pNameSpace, bPublish, &pcError, "Saving project %u", pProject->id))
	{
		AssertOrAlert("UGC_RESOURCE_SAVE_FAILED", "Can't save project resource: Error uploading resources to ugcmaster: %s", pcError);
		if (pcOutError)
			estrPrintf(pcOutError, "Resource upload failed: %s", pcError);

		// Clear files from disk
		gslUGC_DeleteNamespaceDataFiles(pNameSpace);
		return false;
	}

	// Clear the autosave dirty flag
	gslUGC_ClearUGCProjectCopyDirtyFlag();

	// Clear files from disk
	gslUGC_DeleteNamespaceDataFiles(pNameSpace);

	if (pInfo)
	{
		pInfo->ppMapNames = ugcProjectDataGetMaps( data );

		// To find all the places you need to update to add a per
		// UGCProjectVersion field, search for this: {{UGCPROJECTVERSION}}
		StructCopy(parse_UGCProjectInfo, ugcProjectDataGetProjectInfo( data ), &pInfo->sProjectInfo, 0, 0, 0);
		{
			const char* strCostumeName = NULL;
			const char* strPetCostumeName = NULL;
			const char* bodyText = NULL;
			ugcProjectDataGetSTOGrantPrompt( &strCostumeName, &strPetCostumeName, &bodyText, data );
			
			pInfo->pCostumeOverride = StructAllocString( strCostumeName );
			pInfo->pPetOverride = StructAllocString( strPetCostumeName );
			pInfo->pBodyText = ugcAllocSMFString( bodyText, true );
		}

		if (bPublish)
		{
			UUID_t *pUUID;
			char uuidStr[40];

			pUUID = uuidGenerateV4();
			uuidStringShort(pUUID, uuidStr, sizeof(uuidStr));
			free(pUUID);

			pInfo->pPublishUUID = strdup(uuidStr);
			UGCProject_MakeNamespace(&pInfo->pPublishNameSpace, pProject->id, uuidStr);
			estrCopy2(&pInfo->pEditingNameSpace, pNameSpace);

			pInfo->pProjectHeaderCopy = UGCProject_CreateHeaderCopy(pProject, false);
		}
	}

	if (pID)
		*pID = pProject->id;

	return true;
}

bool gslUGC_DoSave(UGCProjectData *data, InfoForUGCProjectSaveOrPublish *pInfo, ContainerID *pID, bool bPublish, char **pcOutError, const char* strReason)
{
	return gslUGC_DoSaveForProject(GET_REF(gGSLState.hUGCProjectFromSubscription), data, pInfo, pID, bPublish, pcOutError, strReason);
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".pOwnerAccountName, .pOwnerAccountName_ForSearching, .Ppprojectversions, .bFlaggedAsCryptic");
enumTransactionOutcome trSaveUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, InfoForUGCProjectSaveOrPublish *pSaveInfo, const char *pComment, const char *pOwnerAccountName)
{
	NOCONST(UGCProjectVersion) *pMostRecent;

	if (!eaSize(&pUGCProject->ppProjectVersions))
	{
		TRANSACTION_RETURN_LOG_FAILURE("UGC project has no versions");
	}

	pMostRecent = eaTail(&pUGCProject->ppProjectVersions);
	switch (ugcProjectGetVersionState(pMostRecent))
	{
	case UGC_NEW:
	case UGC_SAVED:
	case UGC_PUBLISH_FAILED:
		ugcProjectSetVersionState(pMostRecent, UGC_SAVED, pComment);
		UGCProject_ApplySaveOrPublishInfoToVersion(pMostRecent, pSaveInfo);
		break;

	default:
		TRANSACTION_RETURN_LOG_FAILURE("UGC project is corrupt... last version is not saved/new");
	}

	UGCProject_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProject, pOwnerAccountName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Ppprojectversions");
enumTransactionOutcome trSaveUgcProjectForNeedsFirstPublish(ATR_ARGS, NOCONST(UGCProject) *pProject, int bNeedsFirstPublishState, InfoForUGCProjectSaveOrPublish *pSaveInfo, char *pComment)
{
	NOCONST(UGCProjectVersion) *pMostRecent;

	if (!eaSize(&pProject->ppProjectVersions))
	{
		TRANSACTION_RETURN_LOG_FAILURE("UGC project has no versions");
	}

	pMostRecent = eaTail(&pProject->ppProjectVersions);
	switch (ugcProjectGetVersionState(pMostRecent))
	{
	case UGC_NEW:
	case UGC_SAVED:
	case UGC_NEEDS_FIRST_PUBLISH:
		ugcProjectSetVersionState(pMostRecent, bNeedsFirstPublishState ? UGC_NEEDS_FIRST_PUBLISH : UGC_SAVED, pComment);
		UGCProject_ApplySaveOrPublishInfoToVersion(pMostRecent, pSaveInfo);
		break;

	default:
		TRANSACTION_RETURN_LOG_FAILURE("UGC project is corrupt... last version is not saved/new");
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(pUGCProject, ".Ppprojectversions, .Id, .bAuthorAllowsFeatured, .pFeatured, .bUGCFeaturedCopyProjectInProgress, .pOwnerAccountName, .pOwnerAccountName_ForSearching, .bFlaggedAsCryptic");
enumTransactionOutcome trPublishUgcProject(ATR_ARGS, NOCONST(UGCProject) *pUGCProject, InfoForUGCProjectSaveOrPublish *pPublishInfo, const char *pComment, const char *pOwnerAccountName)
{
	NOCONST(UGCProjectVersion) *pMostRecentPlayable;
	NOCONST(UGCProjectVersion) *pMostRecent;
	NOCONST(UGCProjectVersion) *pNewVersion;

	int i;
	static char *pNameErrorString = NULL;

	estrClear(&pNameErrorString);

	if (UGCProject_trh_BeingPublished(ATR_RECURSE, pUGCProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project already being published.  " PUBLISH_FAIL_NOALERT_STRING);
	}

	if (UGCProject_trh_AnyVersionNeedsAttention(ATR_RECURSE, pUGCProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project needs republish.");
	}

	if (!UGCProject_ValidatePotentialName(pPublishInfo->sProjectInfo.pcPublicName, false, &pNameErrorString))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Invalid UGC project name: %s (%s)", pPublishInfo->sProjectInfo.pcPublicName, pNameErrorString);
	}

	if (!eaSize(&pUGCProject->ppProjectVersions))
	{
		TRANSACTION_RETURN_LOG_FAILURE("UGC project has no versions");
	}

	if (!pPublishInfo->pPublishNameSpace || !pPublishInfo->pPublishNameSpace[0])
	{
		TRANSACTION_RETURN_LOG_FAILURE("No new namespace");
	}
	if (!pPublishInfo->pPublishUUID || !pPublishInfo->pPublishUUID[0])
	{
		TRANSACTION_RETURN_LOG_FAILURE("No new UUID");
	}
	if (!pPublishInfo->pPublishJobName || !pPublishInfo->pPublishJobName[0])
	{
		TRANSACTION_RETURN_LOG_FAILURE("No job name");
	}
	if( !pUGCProject->bUGCFeaturedCopyProjectInProgress ) {
		if( pUGCProject->pFeatured ) {
			TRANSACTION_RETURN_LOG_FAILURE( "Featured project can not be published" );
		}
	}
	if( ugcDefaultsAuthorAllowsFeaturedBlocksEditing() && pUGCProject->bAuthorAllowsFeatured ) {
		TRANSACTION_RETURN_LOG_FAILURE( "Author allows featured project can not be published" );
	}

	for (i=0; i < eaSize(&pUGCProject->ppProjectVersions); i++)
	{
		if (stricmp(pUGCProject->ppProjectVersions[i]->pNameSpace, pPublishInfo->pPublishNameSpace) == 0)
		{
			TRANSACTION_RETURN_LOG_FAILURE("Namespace name collision... how is this possible?")
		}
	}

	for (i=0; i < eaSize(&pUGCProject->ppProjectVersions); i++)
	{
		if (ugcProjectGetVersionState(pUGCProject->ppProjectVersions[i]) == UGC_REPUBLISHING)
		{
			QueueRemoteCommand_CancelJobGroup(ATR_RESULT_SUCCESS, GLOBALTYPE_JOBMANAGER, 0, pUGCProject->ppProjectVersions[i]->pPublishJobName, "User started publish, cancelling queued republish.");
			ugcProjectSetVersionState(pUGCProject->ppProjectVersions[i], UGC_UNPLAYABLE, "User started publish, cancelling queued republish.");
		}
		if(ugcProjectGetVersionState(pUGCProject->ppProjectVersions[i]) == UGC_PUBLISHED || ugcProjectGetVersionState(pUGCProject->ppProjectVersions[i]) == UGC_WITHDRAWN)
		{
			pMostRecentPlayable = pUGCProject->ppProjectVersions[i];
		}
	}

	pMostRecent = eaTail(&pUGCProject->ppProjectVersions);	//this is always the editing copy.
	switch (ugcProjectGetVersionState(pMostRecent))
	{
	case UGC_NEW:
	case UGC_SAVED:
	case UGC_PUBLISH_FAILED:
		ugcProjectSetVersionState(pMostRecent, UGC_PUBLISH_BEGUN, pComment);

		UGCProject_ApplySaveOrPublishInfoToVersion(pMostRecent, pPublishInfo);

		pMostRecent->pPublishJobName = strdup(pPublishInfo->pPublishJobName);

		pNewVersion = UGCProject_CreateEmptyVersion(ATR_RECURSE, pUGCProject, pMostRecent->pUUID, pPublishInfo->pEditingNameSpace);
		ugcProjectSetVersionState(pNewVersion, UGC_SAVED, "Creating new saved version when publishing");
		UGCProject_ApplySaveOrPublishInfoToVersion(pNewVersion, pPublishInfo);
		eaPush(&pUGCProject->ppProjectVersions, pNewVersion);

		SAFE_FREE(pMostRecent->pUUID);
		pMostRecent->pUUID = strdup(pPublishInfo->pPublishUUID);
		estrCopy2(&pMostRecent->pNameSpace, pPublishInfo->pPublishNameSpace);

		break;

	default:
		TRANSACTION_RETURN_LOG_FAILURE("UGC project is corrupt... last version is not saved/new");
	}

	if(pOwnerAccountName && pUGCProject->pOwnerAccountName != pOwnerAccountName && 0 != stricmp(pUGCProject->pOwnerAccountName, pOwnerAccountName))
		UGCProject_trh_SetOwnerAccountName(ATR_PASS_ARGS, pUGCProject, pOwnerAccountName);

	return TRANSACTION_OUTCOME_SUCCESS;
}

void PublishJobBegunCB(TransactionReturnVal *pReturn, InfoForUGCProjectSaveOrPublish *pInfoForPublish)
{
	Entity *pEntity = pInfoForPublish->entContainerID ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pInfoForPublish->entContainerID) : NULL;
	if (pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		printf("Publish job begun\n");
		if(pEntity) ClientCmd_UGCEditorUpdatePublishStatus(pEntity, true, "");
	}
	else
	{
		printf("Publish job couldn't begin... doing whatToDoIfPublishJobDoesntStart");
		if(pInfoForPublish->pWhatToDoIfPublishJobDoesntStart)
		{
			ExecuteAndFreeRemoteCommandGroup(pInfoForPublish->pWhatToDoIfPublishJobDoesntStart, NULL, NULL);
			pInfoForPublish->pWhatToDoIfPublishJobDoesntStart = NULL;
		}

		if(pEntity) ClientCmd_UGCEditorUpdatePublishStatus(pEntity, false, "Failed to start publish job");
	}

	StructDestroy(parse_InfoForUGCProjectSaveOrPublish, pInfoForPublish);
}

AUTO_TRANSACTION
ATR_LOCKS(pProject, ".Id, .Ppprojectversions, .bAuthorAllowsFeatured, .pFeatured")
ATR_LOCKS(pProjectSeries, ".ugcSearchCache.eaPublishedProjectIDs");
enumTransactionOutcome trCancelUGCPublish(ATR_ARGS, int bWithdraw, NOCONST(UGCProject) *pProject, NOCONST(UGCProjectSeries) *pProjectSeries, char *pComment)
{
	if (!UGCProject_CanBeWithdrawn(pProject))
	{
		TRANSACTION_RETURN_LOG_FAILURE("Project was not in a state that could be withdrawn");
	}
	
	if( !UGCProject_WithdrawProject(ATR_RECURSE, pProject, pProjectSeries, bWithdraw, pComment)) {
		return TRANSACTION_OUTCOME_FAILURE;
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

void gslUGC_DoProjectPublishPostTransaction(Entity *pEntity, InfoForUGCProjectSaveOrPublish *pInfoForPublish)
{
	//start job manager job
	JobManagerJobGroupDef *pJobGroupDef;
	DynamicPatchInfo *pPatchInfo;

	char *pPatchInfoText = NULL;
	char *pPatchInfoTextSuperEsc = NULL;

	if (0 == eaSize(&pInfoForPublish->ppMapNames) && !sbDoSyncAtEndOfPublishing)
	{
		// There are no jobs to do, so publish succeeds automatically
		RemoteCommand_aslUGCDataManager_UpdateUGCProjectPublishStatus(NULL, GLOBALTYPE_UGCDATAMANAGER, 0, pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace, 1, "No jobs to do, publish succeeding automatically");
		ClientCmd_UGCEditorUpdatePublishStatus(pEntity, true, "");

		if(g_isContinuousBuilder)
			RemoteCommand_ControllerScript_Succeeded_RC(NULL, GLOBALTYPE_JOBMANAGER, 0);

		return;
	}

	pJobGroupDef = StructCreate(parse_JobManagerJobGroupDef);
	pPatchInfo = StructClone(parse_DynamicPatchInfo, ServerLib_GetPatchInfo());

	assertmsgf(pPatchInfo, "In the middle of UGC publish, we suddenly have no dynamic patch info");

	pPatchInfo->pPrefix = strdupf("/data/ns/%s", pInfoForPublish->pPublishNameSpace);
	ParserWriteText(&pPatchInfoText, parse_DynamicPatchInfo, pPatchInfo, 0, 0, 0);
	estrSuperEscapeString(&pPatchInfoTextSuperEsc, pPatchInfoText);
	StructDestroy(parse_DynamicPatchInfo, pPatchInfo);

	pJobGroupDef->pComment = strdupf("Publish job for ugc proj %u NS %s", pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace);

	pJobGroupDef->pGroupTypeName = allocAddString("UGCPublish");
	pJobGroupDef->pJobGroupName = strdup(pInfoForPublish->pPublishJobName);
	pJobGroupDef->owner.pPlayerOwnerAccountName = strdup(pInfoForPublish->pProjectHeaderCopy->pOwnerAccountName);
	pJobGroupDef->owner.iPlayerAccountID = pInfoForPublish->pProjectHeaderCopy->iOwnerAccountID;
	
	pJobGroupDef->bAlertOnFailure = true;

	if (sbVerbosePublishUpdatesToClient)
	{
		pJobGroupDef->eServerTypeForLogUpdates = GetAppGlobalType();
		pJobGroupDef->iServerIDForLogUpdates = GetAppGlobalID();
		pJobGroupDef->iUserDataForLogUpdates = pInfoForPublish->entContainerID;
		pJobGroupDef->pNameForLogUpdates = strdupf("Publish Update: %s", pInfoForPublish->pPublishNameSpace);
	}

	if(eaSize(&pInfoForPublish->ppMapNames))
	{
		JobManagerJobDef *pServerBinJob = StructCreate(parse_JobManagerJobDef);
		JobManagerJobDef *pClientBinJob = StructCreate(parse_JobManagerJobDef);

		// Keep this synchronized with Republish_CB() in aslMapManagerUGCProject.c
		pServerBinJob->pJobName = strdup("Server binning");
		pServerBinJob->eType = JOB_SERVER_W_CMD_LINE;
		pServerBinJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
		pServerBinJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_SERVERBINNER;
		pServerBinJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-NoSharedMemory -ProductionEdit -loadUserNameSpaces %s -makebinsAndExitForNameSpace %s -PatchInfo %s",
																   pInfoForPublish->pPublishNameSpace, pInfoForPublish->pPublishNameSpace, pPatchInfoTextSuperEsc);
		eaPush(&pJobGroupDef->ppJobs, pServerBinJob);

		// Keep this synchronized with Republish_CB() in aslMapManagerUGCProject.c
		pClientBinJob->pJobName = strdup("Client binning");
		pClientBinJob->eType = JOB_SERVER_W_CMD_LINE;
		pClientBinJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
		pClientBinJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_CLIENTBINNER;
		pClientBinJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-nameSpaceForClient %s -PatchInfo %s -CmdLineClient hvDisable 1",
															   pInfoForPublish->pPublishNameSpace, pPatchInfoTextSuperEsc);
		eaPush(&pClientBinJob->ppJobsIDependOn, strdup("Server binning"));
		eaPush(&pJobGroupDef->ppJobs, pClientBinJob);

		if (!sbPublishSkipBeaconizing && !ugc_DevMode)
		{
			JobManagerJobDef *pBeaconBinJob = StructCreate(parse_JobManagerJobDef);
			pBeaconBinJob->pJobName = strdup("Beaconizing");
			pBeaconBinJob->eType = JOB_SERVER_W_CMD_LINE;
			pBeaconBinJob->pServerWCmdLineDef = StructCreate(parse_JobManagerServerWCmdLineDef);
			pBeaconBinJob->pServerWCmdLineDef->eServerTypeToLaunch = GLOBALTYPE_BCNSUBSERVER;
			pBeaconBinJob->pServerWCmdLineDef->pExtraCmdLine = strdupf("-NoSharedMemory -LoadUserNamespaces %s -bcnReqProcessProject %s -PatchInfo %s",
																	   pInfoForPublish->pPublishNameSpace, pInfoForPublish->pPublishNameSpace, pPatchInfoTextSuperEsc);
			eaPush(&pBeaconBinJob->ppJobsIDependOn, strdup("Server binning"));
			eaPush(&pJobGroupDef->ppJobs, pBeaconBinJob);
		}
	}

	if (sbDoSyncAtEndOfPublishing)
	{
		JobManagerJobDef *pSyncJob = StructCreate(parse_JobManagerJobDef);
		pSyncJob->pJobName = strdup("WaitForSync");
		pSyncJob->eType = JOB_REMOTE_CMD;
		pSyncJob->pRemoteCmdDef = StructCreate(parse_JobManagerRemoteCommandDef);
		pSyncJob->pRemoteCmdDef->bSlow = true;
		pSyncJob->pRemoteCmdDef->eTypeForCommand = GLOBALTYPE_JOBMANAGER;
		pSyncJob->pRemoteCmdDef->iIDForCommand = 0;
		pSyncJob->pRemoteCmdDef->iInitialCommandTimeout = 3600;
		pSyncJob->pRemoteCmdDef->pCommandString = strdupf("QueryPatchCompletion_ForJob \"%s\" \"$JOBNAME$\"", pInfoForPublish->pPublishNameSpace);

		if(eaSize(&pInfoForPublish->ppMapNames))
		{
			eaPush(&pSyncJob->ppJobsIDependOn, strdup("Client binning"));
			if (!sbPublishSkipBeaconizing && !ugc_DevMode)
			{
				eaPush(&pSyncJob->ppJobsIDependOn, strdup("Beaconizing"));
			}
		}
		eaPush(&pJobGroupDef->ppJobs, pSyncJob);
	}


	pJobGroupDef->pWhatToDoOnSuccess = CreateEmptyRemoteCommandGroup();
	AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnSuccess, GLOBALTYPE_UGCDATAMANAGER, 0, true,
								   "aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 1 gslUGC_DoProjectPublishPostTransaction", pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace);
	
	if (g_isContinuousBuilder)
	{
		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnSuccess, GLOBALTYPE_JOBMANAGER, 0, false,
			"ControllerScript_Succeeded_RC");
	}
	else
	{
		char *pHeader = NULL;
		char *pBody = NULL;

		entFormatGameMessageKey(pEntity, &pHeader, "UGC.PublishSucceededHeader",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		entFormatGameMessageKey(pEntity, &pBody, "UGC.PublishSucceededBody",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);
	
		if (isProductionMode())
		{
			AddCommandToRemoteCommandGroup_SentNPCEmail(pJobGroupDef->pWhatToDoOnSuccess, pInfoForPublish->pProjectHeaderCopy->iOwnerAccountID, GAMESERVER_VSHARD_ID,
														pInfoForPublish->pProjectHeaderCopy->pOwnerAccountName, TranslateMessageKeyDefault("UGC.TheFoundry", "The Foundry"), pHeader , pBody);
		}

		estrDestroy(&pHeader);
		estrDestroy(&pBody);
	}
		

	pJobGroupDef->pWhatToDoOnFailure = CreateEmptyRemoteCommandGroup();
	AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnFailure, GLOBALTYPE_UGCDATAMANAGER, 0, true,
								   "aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0 gslUGC_DoProjectPublishPostTransaction_Failure", pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace);
	
	if (g_isContinuousBuilder)
	{
		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnFailure, GLOBALTYPE_JOBMANAGER, 0, false,
			"ControllerScript_Failed_RC JobFailed");
	}
	else
	{
		char *pHeader = NULL;
		char *pBody = NULL;

		entFormatGameMessageKey(pEntity, &pHeader, "UGC.PublishFailedHeader",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		entFormatGameMessageKey(pEntity, &pBody, "UGC.PublishFailedBody",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		if (isProductionMode())
		{	
			AddCommandToRemoteCommandGroup_SentNPCEmail(pJobGroupDef->pWhatToDoOnFailure, pInfoForPublish->pProjectHeaderCopy->iOwnerAccountID, GAMESERVER_VSHARD_ID,
													pInfoForPublish->pProjectHeaderCopy->pOwnerAccountName, TranslateMessageKeyDefault("UGC.TheFoundry", "The Foundry"), pHeader, pBody);
		}

		estrDestroy(&pHeader);
		estrDestroy(&pBody);

	}

	pJobGroupDef->pWhatToDoOnJobManagerCrash = CreateEmptyRemoteCommandGroup();
	AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnJobManagerCrash, GLOBALTYPE_UGCDATAMANAGER, 0, true,
								   "aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0 gslUGC_DoProjectPublishPostTransaction_JMCrash", pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace);

	if (g_isContinuousBuilder)
	{
		AddCommandToRemoteCommandGroup(pJobGroupDef->pWhatToDoOnJobManagerCrash, GLOBALTYPE_JOBMANAGER, 0, false,
			"ControllerScript_Failed_RC JobManagerCrash");
	}
	else
	{
		char *pHeader = NULL;
		char *pBody = NULL;

		entFormatGameMessageKey(pEntity, &pHeader, "UGC.PublishCrashedHeader",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		entFormatGameMessageKey(pEntity, &pBody, "UGC.PublishCrashedBody",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		if (isProductionMode())
		{
			AddCommandToRemoteCommandGroup_SentNPCEmail(pJobGroupDef->pWhatToDoOnJobManagerCrash, pInfoForPublish->pProjectHeaderCopy->iOwnerAccountID, GAMESERVER_VSHARD_ID,
												pInfoForPublish->pProjectHeaderCopy->pOwnerAccountName, TranslateMessageKeyDefault("UGC.TheFoundry", "The Foundry"), pHeader, pBody);
		}

		estrDestroy(&pHeader);
		estrDestroy(&pBody);
	}

	pInfoForPublish->pWhatToDoIfPublishJobDoesntStart = CreateEmptyRemoteCommandGroup();
	AddCommandToRemoteCommandGroup(pInfoForPublish->pWhatToDoIfPublishJobDoesntStart, GLOBALTYPE_UGCDATAMANAGER, 0, true,
								   "aslUGCDataManager_UpdateUGCProjectPublishStatus %u \"%s\" 0 gslUGC_DoProjectPublishPostTransaction_NoStart", pInfoForPublish->pProjectHeaderCopy->id, pInfoForPublish->pPublishNameSpace);
	if (g_isContinuousBuilder)
	{

		AddCommandToRemoteCommandGroup(pInfoForPublish->pWhatToDoIfPublishJobDoesntStart, GLOBALTYPE_JOBMANAGER, 0, false,
			"ControllerScript_Failed_RC JobDoesntStart");
	}
	else
	{
		char *pHeader = NULL;
		char *pBody = NULL;

		entFormatGameMessageKey(pEntity, &pHeader, "UGC.PublishNoStartHeader",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		entFormatGameMessageKey(pEntity, &pBody, "UGC.PublishNoStartBody",
			STRFMT_STRING("ProjName", pInfoForPublish->sProjectInfo.pcPublicName),
			STRFMT_STRING("ProjID", pInfoForPublish->pProjectHeaderCopy->pIDString),
			STRFMT_END);

		if (isProductionMode())
		{
			AddCommandToRemoteCommandGroup_SentNPCEmail(pInfoForPublish->pWhatToDoIfPublishJobDoesntStart, pInfoForPublish->pProjectHeaderCopy->iOwnerAccountID, GAMESERVER_VSHARD_ID,
												pInfoForPublish->pProjectHeaderCopy->pOwnerAccountName, TranslateMessageKeyDefault("UGC.TheFoundry", "The Foundry"), pHeader, pBody);
		}
	
		estrDestroy(&pHeader);
		estrDestroy(&pBody);

	}

	// Send off job last, after letting the client know it's been started
	RemoteCommand_BeginNewJobGroup(objCreateManagedReturnVal(PublishJobBegunCB, pInfoForPublish), GLOBALTYPE_JOBMANAGER, 0, pJobGroupDef);
	StructDestroy(parse_JobManagerJobGroupDef, pJobGroupDef);

	estrDestroy(&pPatchInfoText);
	estrDestroy(&pPatchInfoTextSuperEsc);
}
