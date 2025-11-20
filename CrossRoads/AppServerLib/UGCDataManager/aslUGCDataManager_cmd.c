#include "aslUGCDataManager.h"
#include "aslUGCDataManagerProject.h"

#include "stdtypes.h"
#include "utilitiesLib.h"
#include "TransactionOutcomes.h"
#include "LoggedTransactions.h"

#include "UGCCommon.h"

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "Autogen/AppServerLib_autogen_SlowFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"

#include "ResourceInfo.h"
#include "file.h"
#include "logging.h"
#include "ServerLib.h"

#include "aslPatching.h"

#include "aslUGCDataManager_cmd_c_ast.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// BEGIN Structures and Functions to send a message back to the Entity client about the results of an inter-shard transaction on UGC data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef void (*aslUGCDataManager_TransactionReturnWithSuccessCallback)(const char *pcShardName, GlobalType globalType, ContainerID containerID, ContainerID entContainerID, bool bSuccess);
typedef void (*aslUGCDataManager_TransactionReturnWithSuccessAndErrorStringCallback)(const char *pcShardName, GlobalType globalType, ContainerID containerID, ContainerID entContainerID, bool bSuccess, char *pcDetails);
typedef void (*aslUGCDataManager_TransactionReturnWithSuccessAndUserDataCallback)(const char *pcShardName, GlobalType globalType, ContainerID containerID, ContainerID entContainerID, bool bSuccess, void *structptr);

typedef struct UGCShardReturnData
{
	char *pcShardName;
	GlobalType globalType;
	ContainerID containerID;

	aslUGCDataManager_TransactionReturnWithSuccessCallback callbackWithSuccess;
	aslUGCDataManager_TransactionReturnWithSuccessAndErrorStringCallback callbackWithSuccessAndErrorString;
	aslUGCDataManager_TransactionReturnWithSuccessAndUserDataCallback callbackWithSuccessAndUserData;

	ParseTable *pti;
	void *structptr;
} UGCShardReturnData;

static UGCShardReturnData* CreateUGCShardReturnDataWithSuccess(const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessCallback callback)
{
	UGCShardReturnData *pUGCShardReturnData = NULL;
	if(globalType != GLOBALTYPE_NONE && containerID)
	{
		pUGCShardReturnData = calloc(1, sizeof(UGCShardReturnData));
		pUGCShardReturnData->pcShardName = StructAllocString(pcShardName);
		pUGCShardReturnData->globalType = globalType;
		pUGCShardReturnData->containerID = containerID;

		pUGCShardReturnData->callbackWithSuccess = callback;

		pUGCShardReturnData->pti = NULL;
		pUGCShardReturnData->structptr = NULL;
	}
	return pUGCShardReturnData;
}

static UGCShardReturnData* CreateUGCShardReturnDataWithSuccessAndErrorString(const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessAndErrorStringCallback callback)
{
	UGCShardReturnData *pUGCShardReturnData = NULL;
	if(globalType != GLOBALTYPE_NONE && containerID)
	{
		pUGCShardReturnData = calloc(1, sizeof(UGCShardReturnData));
		pUGCShardReturnData->pcShardName = StructAllocString(pcShardName);
		pUGCShardReturnData->globalType = globalType;
		pUGCShardReturnData->containerID = containerID;

		pUGCShardReturnData->callbackWithSuccessAndErrorString = callback;

		pUGCShardReturnData->pti = NULL;
		pUGCShardReturnData->structptr = NULL;
	}
	return pUGCShardReturnData;
}

static UGCShardReturnData* CreateUGCShardReturnDataWithSuccessAndUserData(const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessAndUserDataCallback callback, ParseTable *pti, void *structptr)
{
	UGCShardReturnData *pUGCShardReturnData = NULL;
	if(globalType != GLOBALTYPE_NONE && containerID)
	{
		pUGCShardReturnData = calloc(1, sizeof(UGCShardReturnData));
		pUGCShardReturnData->pcShardName = StructAllocString(pcShardName);
		pUGCShardReturnData->globalType = globalType;
		pUGCShardReturnData->containerID = containerID;

		pUGCShardReturnData->callbackWithSuccessAndUserData = callback;

		if(pti && structptr)
		{
			pUGCShardReturnData->pti = pti;
			pUGCShardReturnData->structptr = StructCloneVoid(pti, structptr);
		}
		else
		{
			pUGCShardReturnData->pti = NULL;
			pUGCShardReturnData->structptr = NULL;
		}
	}
	return pUGCShardReturnData;
}

static void aslUGCDataManager_ReturnWithSuccess_CB(TransactionReturnVal *returnVal, UGCShardReturnData *pUGCShardReturnData)
{
	if(pUGCShardReturnData)
	{
		if(pUGCShardReturnData->callbackWithSuccess && pUGCShardReturnData->pcShardName && pUGCShardReturnData->globalType != GLOBALTYPE_NONE && pUGCShardReturnData->containerID)
			pUGCShardReturnData->callbackWithSuccess(pUGCShardReturnData->pcShardName, pUGCShardReturnData->globalType, pUGCShardReturnData->containerID, pUGCShardReturnData->containerID,
				returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);

		free(pUGCShardReturnData);
	}
}

static void aslUGCDataManager_ReturnWithSuccessAndErrorString_CB(TransactionReturnVal *returnVal, UGCShardReturnData *pUGCShardReturnData)
{
	if(pUGCShardReturnData)
	{
		if(pUGCShardReturnData->callbackWithSuccessAndErrorString && pUGCShardReturnData->pcShardName && pUGCShardReturnData->globalType != GLOBALTYPE_NONE && pUGCShardReturnData->containerID)
			pUGCShardReturnData->callbackWithSuccessAndErrorString(pUGCShardReturnData->pcShardName, pUGCShardReturnData->globalType, pUGCShardReturnData->containerID, pUGCShardReturnData->containerID,
			returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS,
			(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS) ? objAutoTransactionGetResult(returnVal) : GetTransactionFailureString(returnVal));

		free(pUGCShardReturnData);
	}
}

static void aslUGCDataManager_ReturnWithSuccessAndUserData_CB(TransactionReturnVal *returnVal, UGCShardReturnData *pUGCShardReturnData)
{
	if(pUGCShardReturnData)
	{
		if(pUGCShardReturnData->callbackWithSuccessAndUserData && pUGCShardReturnData->pcShardName && pUGCShardReturnData->globalType != GLOBALTYPE_NONE && pUGCShardReturnData->containerID)
			pUGCShardReturnData->callbackWithSuccessAndUserData(pUGCShardReturnData->pcShardName, pUGCShardReturnData->globalType, pUGCShardReturnData->containerID, pUGCShardReturnData->containerID,
			returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pUGCShardReturnData->structptr);

		if(pUGCShardReturnData->pti && pUGCShardReturnData->structptr)
			StructDestroyVoid(pUGCShardReturnData->pti, pUGCShardReturnData->structptr);

		free(pUGCShardReturnData);
	}
}

static TransactionReturnVal *aslUGCDataManager_CreateManagedReturnValWithSuccess(const char *pchPrefix, const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessCallback callback)
{
	UGCShardReturnData *pUGCShardReturnData = CreateUGCShardReturnDataWithSuccess(pcShardName, globalType, containerID, callback);

	return LoggedTransactions_CreateManagedReturnVal(pchPrefix, aslUGCDataManager_ReturnWithSuccess_CB, pUGCShardReturnData);
}

static TransactionReturnVal *aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(const char *pchPrefix, const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessAndErrorStringCallback callback)
{
	UGCShardReturnData *pUGCShardReturnData = CreateUGCShardReturnDataWithSuccessAndErrorString(pcShardName, globalType, containerID, callback);

	return LoggedTransactions_CreateManagedReturnVal(pchPrefix, aslUGCDataManager_ReturnWithSuccessAndErrorString_CB, pUGCShardReturnData);
}

static TransactionReturnVal *aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserDataVoid(const char *pchPrefix, const char *pcShardName, GlobalType globalType, ContainerID containerID,
	aslUGCDataManager_TransactionReturnWithSuccessAndUserDataCallback callback, ParseTable *pti, void *structptr)
{
	UGCShardReturnData *pUGCShardReturnData = CreateUGCShardReturnDataWithSuccessAndUserData(pcShardName, globalType, containerID, callback, pti, structptr);

	return LoggedTransactions_CreateManagedReturnVal(pchPrefix, aslUGCDataManager_ReturnWithSuccessAndUserData_CB, pUGCShardReturnData);
}
#define aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(pchPrefix, globalType, pcShardName, containerID, callback, pti, structptr) \
	aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserDataVoid(pchPrefix, globalType, pcShardName, containerID, callback, pti, STRUCT_TYPESAFE_PTR(pti, structptr))

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_TempBanProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount, bool bBan)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trTemporaryBanUgcProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID, 
		pchCSRAccount,
		bBan);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_PermBanProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount, bool bBan)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trBanUgcProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID, 
		pchCSRAccount,
		bBan);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ClearNaughtyValueForProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trClearNaughtyValueForProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID, 
		pchCSRAccount);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_DisableAutoBanForProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount, bool bDisableAutoBan)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trDisableAutoBanForProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID, 
		pchCSRAccount,
		bDisableAutoBan);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ProjectReviewSetHidden(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pReviewerName, const char *pchCSRAccount, bool bHidden)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trUgcProjectReviewSetHidden(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		pReviewerName,
		pchCSRAccount,
		bHidden);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_FeaturedAddProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount, const char *pchDetails, U32 iStartTimestamp, U32 iEndTimestamp)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trUGCFeaturedAddProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		pchCSRAccount,
		pchDetails,
		iStartTimestamp,
		iEndTimestamp);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_FeaturedRemoveProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trUGCFeaturedRemoveProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		pchCSRAccount);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_FeaturedArchiveProject(const char *pcShardName, ContainerID entContainerID,
	U32 iProjectID, const char *pchCSRAccount, U32 iEndTimestamp)
{
	TransactionReturnVal *retVal = aslUGCDataManager_CreateManagedReturnValWithSuccessAndErrorString(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_CSR_Return);

	AutoTrans_trUGCFeaturedArchiveProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		pchCSRAccount,
		iEndTimestamp);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ReviewProject(const char *pcShardName, ContainerID entContainerID,
	UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed, ContainerID iProjectID, U32 iReviewerAccountID, const char *pReviewerAccountName, float fRating, const char *pComment, bool bBetaReviewing)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_gslUGC_ProjectReviewed_Return,
			parse_UGCShardReturnProjectReviewed, pUGCShardReturnProjectReviewed);

	AutoTrans_trReviewUgcProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		iReviewerAccountID,
		pReviewerAccountName,
		fRating,
		pComment,
		bBetaReviewing);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ReviewProjectSeries(const char *pcShardName, ContainerID entContainerID,
	UGCShardReturnProjectSeriesReviewed *pUGCShardReturnProjectSeriesReviewed, ContainerID iProjectSeriesID, U32 iReviewerAccountID, const char *pReviewerAccountName, float fRating, const char *pComment)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_gslUGC_ProjectSeriesReviewed_Return,
			parse_UGCShardReturnProjectSeriesReviewed, pUGCShardReturnProjectSeriesReviewed);

	AutoTrans_trReviewUgcProjectSeries(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECTSERIES, iProjectSeriesID,
		iReviewerAccountID,
		pReviewerAccountName,
		fRating,
		pComment);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ReportProject(const char *pcShardName, ContainerID entContainerID,
	UGCShardReturnProjectReviewed *pUGCShardReturnProjectReviewed, U32 iProjectID, U32 iReporterAccountID, const char *pcReporterPublicAccountName, U32 eReason, const char *pchDetails)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_gslUGC_ReportProject_Return,
			parse_UGCShardReturnProjectReviewed, pUGCShardReturnProjectReviewed);

	AutoTrans_trReportUgcProject(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		iReporterAccountID,
		pcReporterPublicAccountName,
		eReason,
		pchDetails);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_SubscribeToAuthor(const char *pcShardName, ContainerID entAccountID, UGCSubscriptionData *pUGCSubscriptionData)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCSubscriptionData->entContainerID, RemoteCommand_Intershard_gslUGC_SubscribeToAuthor_Return,
			parse_UGCSubscriptionData, pUGCSubscriptionData);

	AutoTrans_trSubscribeToAuthor(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCACCOUNT, entAccountID,
		pUGCSubscriptionData->entContainerID,
		GLOBALTYPE_UGCACCOUNT, pUGCSubscriptionData->iAuthorID);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_UnsubscribeFromAuthor(const char *pcShardName, ContainerID entAccountID, UGCSubscriptionData *pUGCSubscriptionData)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccessAndUserData(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCSubscriptionData->entContainerID, RemoteCommand_Intershard_gslUGC_UnsubscribeFromAuthor_Return,
			parse_UGCSubscriptionData, pUGCSubscriptionData);

	AutoTrans_trUnsubscribeFromAuthor(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCACCOUNT, entAccountID,
		pUGCSubscriptionData->entContainerID,
		GLOBALTYPE_UGCACCOUNT, pUGCSubscriptionData->iAuthorID);
}

void UGCAccountEnsureExistsRemote_AccountCB(UGCAccount* ignored, UGCSearchData* pUGCSearchData)
{
	RemoteCommand_Intershard_gslUGC_EnsureAccountExists_Return(pUGCSearchData->pcShardName, GLOBALTYPE_ENTITYPLAYER, pUGCSearchData->entContainerID, pUGCSearchData);

	StructDestroy(parse_UGCSearchData, pUGCSearchData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_EnsureAccountExists(ContainerID accountContainerID, UGCSearchData* pUGCSearchData)
{
	UGCAccountEnsureExists(accountContainerID, UGCAccountEnsureExistsRemote_AccountCB, StructClone(parse_UGCSearchData, pUGCSearchData));
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// END Structures and Functions to send a message back to the Entity client about the results of an inter-shard transaction on UGC data
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef struct MissionTurnInData
{
	U32 iProjectID;
	U32 iEntContainerID;
} MissionTurnInData;

static void aslUGCDataManager_MissionTurnIn_UGCAccountEnsureExists_CB(UGCAccount* pUGCAccount, MissionTurnInData* pMissionTurnInData)
{
	AutoTrans_trUGCNotifyMissionTurnedIn(LoggedTransactions_CreateManagedReturnVal("Mission-TurnIn", NULL, NULL), GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, pMissionTurnInData->iProjectID,
		GLOBALTYPE_UGCACCOUNT, pUGCAccount->accountID,
		pMissionTurnInData->iEntContainerID);

	AutoTrans_trUGCNotifyMissionPlayed(LoggedTransactions_CreateManagedReturnVal("MissionPlayedAchievementEvent", NULL, NULL), GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, pMissionTurnInData->iProjectID,
		pUGCAccount->accountID);

	free(pMissionTurnInData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_MissionTurnIn(U32 iProjectID, U32 iPlayerAccountID, U32 iEntContainerID)
{
	MissionTurnInData *pMissionTurnInData = (MissionTurnInData *)calloc(1, sizeof(MissionTurnInData));
	pMissionTurnInData->iProjectID = iProjectID;
	pMissionTurnInData->iEntContainerID = iEntContainerID;

	UGCAccountEnsureExists(iPlayerAccountID, aslUGCDataManager_MissionTurnIn_UGCAccountEnsureExists_CB, pMissionTurnInData);
}

void aslUGCDataManager_SearchPossibleUGCProjects_CB(TransactionReturnVal *returnVal, UGCProjectSearchInfo *pSearchInfo)
{
	UGCSearchResult *pSearchResult = NULL; 

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(RemoteCommandCheck_FindUGCMaps(returnVal, &pSearchResult) == TRANSACTION_OUTCOME_SUCCESS)
	{
		int it;
		pSearchInfo->pPossibleUGCProjects = StructCreate(parse_PossibleUGCProjects);
		for( it = 0; it != eaSize( &pSearchResult->eaResults ); ++it ) {
			if( pSearchResult->eaResults[ it ]->iUGCProjectID ) { 
				// The returned projects are shallow copies
				UGCProject *pActual = objGetContainerData(GLOBALTYPE_UGCPROJECT, pSearchResult->eaResults[ it ]->iUGCProjectID );
				PossibleUGCProject *pPossible = CreatePossibleUGCProject(pActual, 0);

				pPossible->iCopyID = pPossible->iID;
				pPossible->iID = 0;

				eaPush(&pSearchInfo->pPossibleUGCProjects->ppProjects, pPossible);
			}
		}
	}

	RemoteCommand_Intershard_aslLoginUGCProject_SearchPossibleUGCProjects_Return(pSearchInfo->pcShardName, GLOBALTYPE_LOGINSERVER, pSearchInfo->loginServerID,
		pSearchInfo);

	StructDestroy(parse_UGCSearchResult, pSearchResult);
	StructDestroy(parse_UGCProjectSearchInfo, pSearchInfo);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_SearchPossibleUGCProjects(UGCProjectSearchInfo *pSearchInfo)
{
	RemoteCommand_FindUGCMaps(objCreateManagedReturnVal(aslUGCDataManager_SearchPossibleUGCProjects_CB, StructClone(parse_UGCProjectSearchInfo, pSearchInfo)),
		GLOBALTYPE_UGCSEARCHMANAGER, 0, pSearchInfo);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(GAMESERVER) ACMD_IFDEF(APPSERVER);
void aslUGCDataManager_RequestAccount(const char *shard, ContainerID entContainerID, ContainerID entAccountID)
{
	UGCAccount *pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, entAccountID);

	NOCONST(UGCAccount)* clone = ugcAccountClonePersistedAndSubscribedDataOnly(CONTAINER_NOCONST(UGCAccount, pUGCAccount));

	RemoteCommand_Intershard_gslUGC_ProvideAccount(shard, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, CONTAINER_RECONST(UGCAccount, clone));

	StructDestroyNoConst(parse_UGCAccount, clone);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void CSRQueryUGCProjectReportData(const char *pcShardName, ContainerID entContainerID, ContainerID iUGCProjectID)
{
	UGCProjectReportQuery *pQuery;
	UGCProject *pProj = objGetContainerData(GLOBALTYPE_UGCPROJECT, iUGCProjectID);
	const UGCProjectVersion *pProjVersion = UGCProject_GetMostRecentPublishedVersion(pProj);

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if (!pProj)
	{
		RemoteCommand_Intershard_CSRPrintUGCReportData_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, NULL);
		return;
	}

	pQuery = StructCreate(parse_UGCProjectReportQuery);
	pQuery->iContainerID = iUGCProjectID;
	pQuery->pchProjName = strdup(UGCProject_GetVersionName(pProj, pProjVersion));
	pQuery->uOwnerAccountID = pProj->iOwnerAccountID;
	pQuery->pchOwnerAccountName = strdup(pProj->pOwnerAccountName);
	pQuery->bBanned = pProj->bBanned;
	pQuery->bTemporarilyBanned = (pProj->ugcReporting.uTemporaryBanExpireTime > 0);
	pQuery->iNaughtyValue = pProj->ugcReporting.iNaughtyValue;
	eaCopyStructs(&pProj->ugcReporting.eaReports, &pQuery->eaReports, parse_UGCProjectReport);

	RemoteCommand_Intershard_CSRPrintUGCReportData_Return(pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, entContainerID, pQuery);

	StructDestroy(parse_UGCProjectReportQuery, pQuery);
}

static void aslUGCDataManager_ClearAuthorAllowsFeatured_CB(TransactionReturnVal *pRetVal, UGCIntershardData *pUGCIntershardData)
{
	RemoteCommand_Intershard_aslLogin_ClearAuthorAllowsFeatured_Return(pUGCIntershardData->pcShardName, GLOBALTYPE_LOGINSERVER, pUGCIntershardData->loginServerID,
		pRetVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pUGCIntershardData->loginCookie);

	StructDestroy(parse_UGCIntershardData, pUGCIntershardData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ClearAuthorAllowsFeatured(ContainerID iProjectID, const char *pcShardName, U32 loginServerID, U64 loginCookie)
{
	UGCIntershardData *pUGCIntershardData = StructCreate(parse_UGCIntershardData);
	pUGCIntershardData->pcShardName = StructAllocString(pcShardName);
	pUGCIntershardData->loginServerID = loginServerID;
	pUGCIntershardData->loginCookie = loginCookie;

	AutoTrans_trSetAuthorAllowsFeatured(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, aslUGCDataManager_ClearAuthorAllowsFeatured_CB, pUGCIntershardData), GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		/*bAuthorAllowsFeatured=*/false,
		/*bErrorIfAlreadyFeatured=*/true);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_SetAuthorAllowsFeatured(ContainerID iProjectID, bool bAuthorAllowsFeatured, const char *pcShardName, ContainerID entContainerID)
{
	TransactionReturnVal *retVal =
		aslUGCDataManager_CreateManagedReturnValWithSuccess(__FUNCTION__, pcShardName, GLOBALTYPE_ENTITYPLAYER, entContainerID, RemoteCommand_Intershard_gslUGC_SetAuthorAllowsFeatured_Return);

	AutoTrans_trSetAuthorAllowsFeatured(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, iProjectID,
		/*bAuthorAllowsFeatured=*/bAuthorAllowsFeatured,
		/*bErrorIfAlreadyFeatured=*/false);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_IncrementDropCountStat(ContainerID iProjectID)
{
	AutoTrans_trIncrementDropCountStat(NULL, GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iProjectID);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_RecordCompletion(ContainerID iUGCProjectID, const char *pcVersionNameSpace, U32 uDurationInMinutes, U32 bRecordCompletion)
{
	aslUGCDataManager_ServerMonitor_RecordMissionRecentlyCompleted(pcVersionNameSpace, uDurationInMinutes);

	AutoTrans_trRecordCompletion(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iUGCProjectID, pcVersionNameSpace, uDurationInMinutes, bRecordCompletion);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_RecordMapCompletion(ContainerID iUGCProjectID, const char *pcNamespacedMapName, U32 uDurationInMinutes)
{
	char pchNameSpace[RESOURCE_NAME_MAX_SIZE], pchBaseMapName[RESOURCE_NAME_MAX_SIZE];
	if(resExtractNameSpace(pcNamespacedMapName, pchNameSpace, pchBaseMapName))
		AutoTrans_trRecordMapCompletion(LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, NULL, NULL), GetAppGlobalType(), GLOBALTYPE_UGCPROJECT, iUGCProjectID, pchBaseMapName, uDurationInMinutes);
}

static void UgcAchievementsNotify_CB(TransactionReturnVal *returnVal, UgcAchievementsNotifyData *pUgcAchievementsNotifyData)
{
	if(returnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		S32 i;
		UGCAccount *pUGCAccount = objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUgcAchievementsNotifyData->uUGCAuthorID);
		if(!pUGCAccount)
			return;

		for(i = 0; i < returnVal->iNumBaseTransactions; ++i)
			if(returnVal->pBaseReturnVals[i].returnString && strstri(UGC_ACHIEVEMENT_TRANSACTION_NOTIFY_TIME_DID_NOT_INCREASE, returnVal->pBaseReturnVals[i].returnString) == 0)
				return;

		RemoteCommand_Intershard_gslUGC_AchievementsNotifyGrants(pUgcAchievementsNotifyData->pcShardName, GLOBALTYPE_ENTITYPLAYER, pUgcAchievementsNotifyData->entContainerID,
			pUgcAchievementsNotifyData);
	}

	StructDestroy(parse_UgcAchievementsNotifyData, pUgcAchievementsNotifyData);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_AchievementsNotify(UgcAchievementsNotifyData *pUgcAchievementsNotifyData)
{
	TransactionReturnVal *retVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, (TransactionReturnCallback)UgcAchievementsNotify_CB, StructClone(parse_UgcAchievementsNotifyData, pUgcAchievementsNotifyData));
	AutoTrans_trUgcAchievementsNotify(retVal, GetAppGlobalType(),
		GLOBALTYPE_UGCACCOUNT, pUgcAchievementsNotifyData->uUGCAuthorID,
		pUgcAchievementsNotifyData->uUGCAuthorID,
		pUgcAchievementsNotifyData->uToTime);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_IncrementProjectLifetimeTips(ContainerID uUGCProjectID, U32 uTipAmount)
{
	AutoTrans_trUgcProjectIncrementLifetimeTips(NULL, GetAppGlobalType(),
		GLOBALTYPE_UGCPROJECT, uUGCProjectID,
		uTipAmount);
}

//__CATEGORY UGC settings
// If non-zero, then author initiated UGC publishing is disabled. Otherwise, enabled.
static bool s_bUGCPublishDisabled = false;
AUTO_CMD_INT(s_bUGCPublishDisabled, UGCPublishDisabled) ACMD_AUTO_SETTING(Ugc, UGCDATAMANAGER);

bool aslUGCDataManager_IsPublishDisabled()
{
	return s_bUGCPublishDisabled;
}

AUTO_COMMAND_REMOTE_SLOW(bool) ACMD_NAME(aslUGCDataManager_IsPublishDisabled);
void aslUGCDataManager_IsPublishDisabled_Command(SlowRemoteCommandID iCmdID)
{
	SlowRemoteCommandReturn_aslUGCDataManager_IsPublishDisabled(iCmdID, aslUGCDataManager_IsPublishDisabled());
}

static void aslUGCDataManager_GetAllContent_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID)
{
	SlowRemoteCommandID iCmdID = (SlowRemoteCommandID)(intptr_t)iRawCmdID;
	UGCSearchResult *pUGCSearchResult = NULL;

	RemoteCommandCheck_FindUGCMaps(pReturnVal, &pUGCSearchResult);

	SlowRemoteCommandReturn_aslUGCDataManager_GetAllContent(iCmdID, pUGCSearchResult);
}

AUTO_COMMAND_REMOTE_SLOW(UGCSearchResult *);
void aslUGCDataManager_GetAllContent(SlowRemoteCommandID iCmdID, bool bIncludeSaved, bool bIncludePublished, UGCProjectSearchInfo *pUGCProjectSearchInfo)
{
	if(pUGCProjectSearchInfo)
		RemoteCommand_FindUGCMaps(objCreateManagedReturnVal(aslUGCDataManager_GetAllContent_CB, (UserData)(intptr_t)iCmdID), GLOBALTYPE_UGCSEARCHMANAGER, SPECIAL_CONTAINERID_RANDOM,
			pUGCProjectSearchInfo);
	else
	{
		UGCSearchResult *pUGCSearchResult = StructCreate(parse_UGCSearchResult);
		ContainerID *eaiProjectIDs = NULL;
		ContainerID *eaiSeriesIDs = NULL;
		UGCProject *pUGCProject = NULL;
		int i;

		ContainerIterator iter = {0};

		objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);
		while(pUGCProject = objGetNextObjectFromIterator(&iter))
		{
			if(pUGCProject->iOwnerAccountID > 0 && !UGCProject_CanAutoDelete(pUGCProject))
			{
				// make sure there is one or more of these project version states. Otherwise, we do not care about it.
				bool bWeCare = false;
				int index;
				for(index = 0; index < eaSize(&pUGCProject->ppProjectVersions); index++) {
					UGCProjectVersionState eState = ugcProjectGetVersionStateConst(pUGCProject->ppProjectVersions[index]);
					if((bIncludeSaved && UGC_SAVED == eState)
						|| (bIncludePublished && (UGC_PUBLISHED == eState || UGC_REPUBLISHING == eState || UGC_NEEDS_REPUBLISHING == eState)))
					{
						bWeCare = true;
						break;
					}
				}

				if(bWeCare)
				{
					eaiPush(&eaiProjectIDs, pUGCProject->id);
					if(pUGCProject->seriesID)
						eaiPushUnique(&eaiSeriesIDs, pUGCProject->seriesID);
				}
			}
		}
		objClearContainerIterator(&iter);

		if(bIncludeSaved)
		{
			UGCProjectSeries *pUGCProjectSeries = NULL;

			objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECTSERIES, &iter);
			while(pUGCProjectSeries = objGetNextObjectFromIterator(&iter))
				if(pUGCProjectSeries->iOwnerAccountID > 0 && !UGCProjectSeries_CanAutoDelete(pUGCProjectSeries))
					eaiPushUnique(&eaiSeriesIDs, pUGCProjectSeries->id);
			objClearContainerIterator(&iter);
		}

		for(i = 0; i < eaiSize(&eaiProjectIDs); i++)
		{
			UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
			pUGCContentInfo->iUGCProjectID = eaiProjectIDs[i];
			eaPush(&pUGCSearchResult->eaResults, pUGCContentInfo);
		}
		for(i = 0; i < eaiSize(&eaiSeriesIDs); i++)
		{
			UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
			pUGCContentInfo->iUGCProjectSeriesID = eaiSeriesIDs[i];
			eaPush(&pUGCSearchResult->eaResults, pUGCContentInfo);
		}

		eaiDestroy(&eaiProjectIDs);
		eaiDestroy(&eaiSeriesIDs);

		SlowRemoteCommandReturn_aslUGCDataManager_GetAllContent(iCmdID, pUGCSearchResult);
	}
}

AUTO_COMMAND_REMOTE;
UGCProject *aslUGCDataManager_GetProjectContainer(ContainerID uUGCProjectID)
{
	UGCProject *pUGCProject = StructClone(parse_UGCProject, objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID));

	return pUGCProject;
}

AUTO_COMMAND_REMOTE;
UGCProjectSeries *aslUGCDataManager_GetProjectSeriesContainer(ContainerID uUGCProjectSeriesID)
{
	UGCProjectSeries *pUGCProjectSeries = StructClone(parse_UGCProjectSeries, objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, uUGCProjectSeriesID));

	return pUGCProjectSeries;
}

AUTO_COMMAND_REMOTE;
UGCPatchInfo *aslUGCDataManager_GetPatchInfo()
{
	DynamicPatchInfo *pDynamicPatchInfo = StructCreate(parse_DynamicPatchInfo);

	UGCPatchInfo *pUGCPatchInfo = StructCreate(parse_UGCPatchInfo);

	aslFillInPatchInfoForUGC(pDynamicPatchInfo, PATCHINFO_FOR_UGC_PLAYING);

	pUGCPatchInfo->server = strdup(pDynamicPatchInfo->pServer);
	pUGCPatchInfo->port = pDynamicPatchInfo->iPort;
	pUGCPatchInfo->project = strdup(pDynamicPatchInfo->pUploadProject);
	pUGCPatchInfo->shard = strdup(GetShardNameFromShardInfoString());

	StructDestroy(parse_DynamicPatchInfo, pDynamicPatchInfo);

	return pUGCPatchInfo;
}

static int s_iUGCAccountsDeleted = 0;
static int s_iUGCProjectsDeleted = 0;
static int s_iUGCProjectSeriesDeleted = 0;

static void aslUGCDataManager_DeleteAllUGC_Account_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID);
static void aslUGCDataManager_DeleteAllUGC_Project_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID);
static void aslUGCDataManager_DeleteAllUGC_Series_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID);

static void ContinueDeletingUGCContent(SlowRemoteCommandID iCmdID)
{
	UGCAccount *pUGCAccount = NULL;
	ContainerIterator iter;

	objInitContainerIteratorFromType(GLOBALTYPE_UGCACCOUNT, &iter);
	if(pUGCAccount = objGetNextObjectFromIterator(&iter))
	{
		objRequestContainerDestroy(objCreateManagedReturnVal(aslUGCDataManager_DeleteAllUGC_Account_CB, (UserData)(intptr_t)iCmdID),
			GLOBALTYPE_UGCACCOUNT, pUGCAccount->accountID, objServerType(), objServerID());
		objClearContainerIterator(&iter);
	}
	else
	{
		UGCProject *pUGCProject = NULL;

		objClearContainerIterator(&iter);
		objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECT, &iter);
		if(pUGCProject = objGetNextObjectFromIterator(&iter))
		{
			objRequestContainerDestroy(objCreateManagedReturnVal(aslUGCDataManager_DeleteAllUGC_Project_CB, (UserData)(intptr_t)iCmdID),
				GLOBALTYPE_UGCPROJECT, pUGCProject->id, objServerType(), objServerID());
			objClearContainerIterator(&iter);
		}
		else
		{
			UGCProjectSeries *pUGCProjectSeries = NULL;

			objClearContainerIterator(&iter);
			objInitContainerIteratorFromType(GLOBALTYPE_UGCPROJECTSERIES, &iter);
			if(pUGCProjectSeries = objGetNextObjectFromIterator(&iter))
			{
				objRequestContainerDestroy(objCreateManagedReturnVal(aslUGCDataManager_DeleteAllUGC_Series_CB, (UserData)(intptr_t)iCmdID),
					GLOBALTYPE_UGCPROJECTSERIES, pUGCProjectSeries->id, objServerType(), objServerID());
			}
			else
			{
				char *estrResult = NULL;
				estrPrintf(&estrResult, "Deleted %u Accounts, %u Projects, and %u Series", s_iUGCAccountsDeleted, s_iUGCProjectsDeleted, s_iUGCProjectSeriesDeleted);
				SlowRemoteCommandReturn_aslUGCDataManager_DeleteAllUGC(iCmdID, estrResult);
				estrDestroy(&estrResult);
			}

			objClearContainerIterator(&iter);
		}
	}
}

static void aslUGCDataManager_DeleteAllUGC_Series_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID)
{
	SlowRemoteCommandID iCmdID = (SlowRemoteCommandID)(intptr_t)iRawCmdID;

	if(TRANSACTION_OUTCOME_SUCCESS == pReturnVal->eOutcome)
	{
		s_iUGCProjectSeriesDeleted++;

		ContinueDeletingUGCContent(iCmdID);
	}
	else
		SlowRemoteCommandReturn_aslUGCDataManager_DeleteAllUGC(iCmdID, "Failed to delete a UGC Project Series. Stopping delete");
}

static void aslUGCDataManager_DeleteAllUGC_Project_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID)
{
	SlowRemoteCommandID iCmdID = (SlowRemoteCommandID)(intptr_t)iRawCmdID;

	if(TRANSACTION_OUTCOME_SUCCESS == pReturnVal->eOutcome)
	{
		s_iUGCProjectsDeleted++;

		ContinueDeletingUGCContent(iCmdID);
	}
	else
		SlowRemoteCommandReturn_aslUGCDataManager_DeleteAllUGC(iCmdID, "Failed to delete a UGC Project. Stopping delete");
}

static void aslUGCDataManager_DeleteAllUGC_Account_CB(TransactionReturnVal *pReturnVal, UserData iRawCmdID)
{
	SlowRemoteCommandID iCmdID = (SlowRemoteCommandID)(intptr_t)iRawCmdID;

	if(TRANSACTION_OUTCOME_SUCCESS == pReturnVal->eOutcome)
	{
		s_iUGCAccountsDeleted++;

		ContinueDeletingUGCContent(iCmdID);
	}
	else
		SlowRemoteCommandReturn_aslUGCDataManager_DeleteAllUGC(iCmdID, "Failed to delete a UGC Account. Stopping delete");
}

AUTO_COMMAND_REMOTE_SLOW(char *);
void aslUGCDataManager_DeleteAllUGC(SlowRemoteCommandID iCmdID, const char *comment)
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");	

	log_printf(LOG_UGC, "Requesting deletion of all UGC containers because deletion of all UGC content was specified during a UGCImport (comment: %s).", comment);

	s_iUGCAccountsDeleted = 0;
	s_iUGCProjectsDeleted = 0;
	s_iUGCProjectSeriesDeleted = 0;

	ContinueDeletingUGCContent(iCmdID);
}

AUTO_STRUCT;
typedef struct SendProjectContainerForImportData
{
	UGCProject *pUGCProjectToImport;

	ContainerID uUGCProjectIDNewOrExisting;

	SlowRemoteCommandID iCmdID;
} SendProjectContainerForImportData;

AUTO_STRUCT;
typedef struct SendProjectSeriesContainerForImportData
{
	UGCProjectSeries *pUGCProjectSeriesToImport;

	ContainerID uUGCProjectSeriesIDNewOrExisting;

	SlowRemoteCommandID iCmdID;
} SendProjectSeriesContainerForImportData;

static void aslUGCDataManager_SendProjectContainerForImport_FillInNewVersionForImport_CB(TransactionReturnVal *pReturn, SendProjectContainerForImportData *pSendProjectContainerForImportData)
{
	UGCProjectContainerCreateForImportData retVal;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	StructInit(parse_UGCProjectContainerCreateForImportData, &retVal);

	if(pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
		estrPrintf(&retVal.estrError, "Failed to fill in new version of UGCProject for import: %s", GetTransactionFailureString(pReturn));
	else if(pSendProjectContainerForImportData->uUGCProjectIDNewOrExisting)
	{
		retVal.pUGCProject = StructClone(parse_UGCProject,
			objGetContainerData(GLOBALTYPE_UGCPROJECT, pSendProjectContainerForImportData->uUGCProjectIDNewOrExisting));

		retVal.pDynamicPatchInfo = StructCreate(parse_DynamicPatchInfo);
		aslFillInPatchInfoForUGC(retVal.pDynamicPatchInfo, 0);
	}

	SlowRemoteCommandReturn_aslUGCDataManager_SendProjectContainerForImport(pSendProjectContainerForImportData->iCmdID, &retVal);

	StructDeInit(parse_UGCProjectContainerCreateForImportData, &retVal);

	free(pSendProjectContainerForImportData);
}

void aslUGCDataManager_SendProjectContainerForImport_UGCProjectCreate_CB(TransactionReturnVal *pReturn, SendProjectContainerForImportData *pSendProjectContainerForImportData)
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		pSendProjectContainerForImportData->uUGCProjectIDNewOrExisting = atoi(pReturn->pBaseReturnVals[0].returnString);

		AutoTrans_trUGCProjectFillInNewVersionForImport(
			objCreateManagedReturnVal(aslUGCDataManager_SendProjectContainerForImport_FillInNewVersionForImport_CB, pSendProjectContainerForImportData), GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCPROJECT, pSendProjectContainerForImportData->uUGCProjectIDNewOrExisting,
			/*bClearExistingVersions=*/true);
	}
	else
	{
		UGCProjectContainerCreateForImportData retVal;
		StructInit(parse_UGCProjectContainerCreateForImportData, &retVal);

		estrPrintf(&retVal.estrError, "Failed to create UGCProject for import: %s", GetTransactionFailureString(pReturn));

		SlowRemoteCommandReturn_aslUGCDataManager_SendProjectContainerForImport(pSendProjectContainerForImportData->iCmdID, &retVal);

		StructDeInit(parse_UGCProjectContainerCreateForImportData, &retVal);

		free(pSendProjectContainerForImportData);
	}
}

AUTO_COMMAND_REMOTE_SLOW(UGCProjectContainerCreateForImportData *);
void aslUGCDataManager_SendProjectContainerForImport(SlowRemoteCommandID iCmdID, UGCProject *pUGCProject, const char *previousShard, const char *comment, bool forceDelete)
{
	ContainerID *eauUGCProjectIDs = NULL;
	UGCProject *pUGCProjectExisting = NULL;
	int index;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	GetUGCProjectsByUGCAccount(pUGCProject->iOwnerAccountID, &eauUGCProjectIDs, NULL);
	for(index = 0; index < eaiSize(&eauUGCProjectIDs); index++)
	{
		ContainerID uUGCProjectID = eauUGCProjectIDs[index];
		UGCProject *pUGCProjectTest = objGetContainerData(GLOBALTYPE_UGCPROJECT, uUGCProjectID);
		if(pUGCProjectTest->strPreviousShard && 0 == stricmp(pUGCProjectTest->strPreviousShard, previousShard) && pUGCProjectTest->iIdOnPreviousShard == pUGCProject->id)
		{
			pUGCProjectExisting = pUGCProjectTest;
			break;
		}
	}
	eaiDestroy(&eauUGCProjectIDs);

	// If we have an existing import of this project
	if(pUGCProjectExisting)
	{
		if(forceDelete)
		{
			log_printf(LOG_UGC, "Requesting deletion of UGCProject container %u (%s) (owned by %u %s) because it was previously imported from %s and we are once again importing it with force delete option (comment: %s).",
				pUGCProjectExisting->id, pUGCProjectExisting->pIDString, pUGCProjectExisting->iOwnerAccountID, pUGCProjectExisting->pOwnerAccountName, pUGCProjectExisting->strPreviousShard, comment);

			objRequestContainerDestroy(NULL, GLOBALTYPE_UGCPROJECT, pUGCProjectExisting->id, objServerType(), objServerID());
		}
		else
		{
			// TODO: Implement option for importing another copy

			// Right now, we just report success with a message and bail
			UGCProjectContainerCreateForImportData retVal;
			StructInit(parse_UGCProjectContainerCreateForImportData, &retVal);

			estrPrintf(&retVal.estrError, "Already imported");
			SlowRemoteCommandReturn_aslUGCDataManager_SendProjectContainerForImport(iCmdID, &retVal);

			StructDeInit(parse_UGCProjectContainerCreateForImportData, &retVal);

			return;
		}
	}

	// Do the import
	{
		NOCONST(UGCProject) *pUGCProjectNoVersions = StructCloneDeConst(parse_UGCProject, pUGCProject);

		SendProjectContainerForImportData *pSendProjectContainerForImportData = calloc(sizeof(SendProjectContainerForImportData), 1);
		pSendProjectContainerForImportData->pUGCProjectToImport = StructClone(parse_UGCProject, pUGCProject);
		pSendProjectContainerForImportData->iCmdID = iCmdID;

		eaDestroy(&pUGCProjectNoVersions->ppProjectVersions);

		pUGCProjectNoVersions->id = 0;
		pUGCProjectNoVersions->seriesID = 0;
		pUGCProjectNoVersions->strPreviousShard = strdup(previousShard);
		pUGCProjectNoVersions->iIdOnPreviousShard = pUGCProject->id;
		pUGCProjectNoVersions->bNewlyImported = true;
		pUGCProjectNoVersions->strImportComment = strdup(comment);

		// This is here because we may be importing from a shard that was not yet computing fAdjustedRatingUsingConfidence because it did not yet have this code.
		pUGCProjectNoVersions->ugcReviews.fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(&pUGCProjectNoVersions->ugcReviews);

		if(!objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCProjectNoVersions->iOwnerAccountID))
		{
			NOCONST(UGCAccount) *pUGCAccount = StructCreateNoConst(parse_UGCAccount);
			pUGCAccount->accountID = pUGCProjectNoVersions->iOwnerAccountID;
			objRequestContainerCreate(NULL, GLOBALTYPE_UGCACCOUNT, pUGCAccount, GetAppGlobalType(), GetAppGlobalID());
		}

		objRequestContainerCreate(objCreateManagedReturnVal(aslUGCDataManager_SendProjectContainerForImport_UGCProjectCreate_CB, pSendProjectContainerForImportData),
			GLOBALTYPE_UGCPROJECT, pUGCProjectNoVersions, GetAppGlobalType(), GetAppGlobalID());

		StructDestroyNoConst(parse_UGCProject, pUGCProjectNoVersions);
	}
}

static void aslUGCDataManager_SendProjectSeriesContainerForImport_FillInForImport_CB(TransactionReturnVal *pReturn, SendProjectSeriesContainerForImportData *pSendProjectSeriesContainerForImportData)
{
	UGCProjectSeriesContainerCreateForImportData retVal;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	StructInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

	if(pReturn->eOutcome != TRANSACTION_OUTCOME_SUCCESS)
		estrPrintf(&retVal.estrError, "Failed to fill in UGCProjectSeries for import: %s", GetTransactionFailureString(pReturn));
	else if(pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting)
		retVal.pUGCProjectSeries = StructClone(parse_UGCProjectSeries,
			objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting));

	SlowRemoteCommandReturn_aslUGCDataManager_SendProjectSeriesContainerForImport(pSendProjectSeriesContainerForImportData->iCmdID, &retVal);

	StructDeInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

	free(pSendProjectSeriesContainerForImportData);
}

void aslUGCDataManager_SendProjectSeriesContainerForImport_UGCProjectSeriesCreate_CB(TransactionReturnVal *pReturn, SendProjectSeriesContainerForImportData *pSendProjectSeriesContainerForImportData)
{
	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	if(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS) 
	{
		UGCProjectSeries *pUGCProjectSeries = NULL;

		pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting = atoi(pReturn->pBaseReturnVals[0].returnString);

		AutoTrans_trUGCProjectSeriesFillInForImport(
			objCreateManagedReturnVal(aslUGCDataManager_SendProjectSeriesContainerForImport_FillInForImport_CB, pSendProjectSeriesContainerForImportData), GLOBALTYPE_UGCDATAMANAGER,
			GLOBALTYPE_UGCPROJECTSERIES, pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting);

		pUGCProjectSeries = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting);
		if(eaSize(&pUGCProjectSeries->eaVersions))
		{
			int index;
			const UGCProjectSeriesVersion *pUGCProjectSeriesVersion = pUGCProjectSeries->eaVersions[eaSize(&pUGCProjectSeries->eaVersions) - 1];
			if(UGC_PUBLISHED == pUGCProjectSeriesVersion->eState)
			{
				for(index = 0; index < eaSize(&pUGCProjectSeriesVersion->eaChildNodes); index++)
				{
					UGCProjectSeriesNode *pUGCProjectSeriesNode = pUGCProjectSeriesVersion->eaChildNodes[index];
					AutoTrans_trUGCProjectFixupSeriesForImport(NULL, GLOBALTYPE_UGCDATAMANAGER,
						GLOBALTYPE_UGCPROJECT, pUGCProjectSeriesNode->iProjectID,
						pSendProjectSeriesContainerForImportData->uUGCProjectSeriesIDNewOrExisting);
				}
			}
		}
	}
	else
	{
		UGCProjectSeriesContainerCreateForImportData retVal;
		StructInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

		estrPrintf(&retVal.estrError, "Failed to create UGCProjectSeries for import: %s", GetTransactionFailureString(pReturn));

		SlowRemoteCommandReturn_aslUGCDataManager_SendProjectSeriesContainerForImport(pSendProjectSeriesContainerForImportData->iCmdID, &retVal);

		StructDeInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

		free(pSendProjectSeriesContainerForImportData);
	}
}

AUTO_COMMAND_REMOTE_SLOW(UGCProjectSeriesContainerCreateForImportData *);
void aslUGCDataManager_SendProjectSeriesContainerForImport(SlowRemoteCommandID iCmdID, UGCProjectSeries *pUGCProjectSeries, const char *previousShard, const char *comment,
	bool forceDelete)
{
	ContainerID *eauUGCProjectIDs = NULL;
	ContainerID *eauUGCProjectSeriesIDs = NULL;
	UGCProjectSeries *pUGCProjectSeriesExisting = NULL;
	int index;

	ASSERT_CONTAINER_DATA_UGC("A non-UGCDataManager server tried to get a UGC container!");

	GetUGCProjectsByUGCAccount(pUGCProjectSeries->iOwnerAccountID, &eauUGCProjectIDs, &eauUGCProjectSeriesIDs);

	for(index = 0; index < eaiSize(&eauUGCProjectSeriesIDs); index++)
	{
		ContainerID uUGCProjectSeriesID = eauUGCProjectSeriesIDs[index];
		UGCProjectSeries *pUGCProjectSeriesTest = objGetContainerData(GLOBALTYPE_UGCPROJECTSERIES, uUGCProjectSeriesID);
		if(pUGCProjectSeriesTest->strPreviousShard && 0 == stricmp(pUGCProjectSeriesTest->strPreviousShard, previousShard) && pUGCProjectSeriesTest->iIdOnPreviousShard == pUGCProjectSeries->id)
		{
			pUGCProjectSeriesExisting = pUGCProjectSeriesTest;
			break;
		}
	}

	if(pUGCProjectSeriesExisting)
	{
		if(forceDelete)
		{
			log_printf(LOG_UGC, "Requesting deletion of UGCProjectSeries container %u (owned by %u %s) because it was previously imported from %s and we are once again importing it with force delete option (comment: %s).",
				pUGCProjectSeriesExisting->id, pUGCProjectSeriesExisting->iOwnerAccountID, pUGCProjectSeriesExisting->strOwnerAccountName, pUGCProjectSeriesExisting->strPreviousShard, comment);

			objRequestContainerDestroy(NULL, GLOBALTYPE_UGCPROJECTSERIES, pUGCProjectSeriesExisting->id, objServerType(), objServerID());
		}
		else
		{
			// TODO: Implement option for importing another copy

			// Right now, we just report success with a message and bail
			UGCProjectSeriesContainerCreateForImportData retVal;
			StructInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

			estrPrintf(&retVal.estrError, "Already imported");
			SlowRemoteCommandReturn_aslUGCDataManager_SendProjectSeriesContainerForImport(iCmdID, &retVal);

			StructDeInit(parse_UGCProjectSeriesContainerCreateForImportData, &retVal);

			return;
		}
	}

	{
		NOCONST(UGCProjectSeries) *pUGCProjectSeriesForImport = StructCloneDeConst(parse_UGCProjectSeries, pUGCProjectSeries);
		NOCONST(UGCProjectSeriesVersion) *pUGCProjectSeriesVersion = NULL;

		SendProjectSeriesContainerForImportData *pSendProjectSeriesContainerForImportData = calloc(sizeof(SendProjectSeriesContainerForImportData), 1);
		pSendProjectSeriesContainerForImportData->pUGCProjectSeriesToImport = StructClone(parse_UGCProjectSeries, pUGCProjectSeries);
		pSendProjectSeriesContainerForImportData->iCmdID = iCmdID;

		pUGCProjectSeriesForImport->id = 0;
		pUGCProjectSeriesForImport->strPreviousShard = strdup(previousShard);
		pUGCProjectSeriesForImport->iIdOnPreviousShard = pUGCProjectSeries->id;
		pUGCProjectSeriesForImport->strImportComment = strdup(comment);

		// This is here because we may be importing from a shard that was not yet computing fAdjustedRatingUsingConfidence because it did not yet have this code.
		pUGCProjectSeriesForImport->ugcReviews.fAdjustedRatingUsingConfidence = ugcReviews_ComputeAdjustedRatingUsingConfidence(&pUGCProjectSeriesForImport->ugcReviews);

		if(eaSize(&pUGCProjectSeriesForImport->eaVersions))
		{
			pUGCProjectSeriesVersion = StructCloneNoConst(parse_UGCProjectSeriesVersion, pUGCProjectSeriesForImport->eaVersions[eaSize(&pUGCProjectSeriesForImport->eaVersions) - 1]);
			eaDestroyStructNoConst(&pUGCProjectSeriesForImport->eaVersions, parse_UGCProjectSeriesVersion);
			eaPush(&pUGCProjectSeriesForImport->eaVersions, pUGCProjectSeriesVersion);
		}

		if(pUGCProjectSeriesVersion)
		{
			for(index = eaSize(&pUGCProjectSeriesVersion->eaChildNodes) - 1; index >= 0; index--)
			{
				NOCONST(UGCProjectSeriesNode) *pUGCProjectSeriesNode = pUGCProjectSeriesVersion->eaChildNodes[index];
				int index2;
				UGCProject *pUGCProjectImported = NULL;
				for(index2 = 0; index2 < eaiSize(&eauUGCProjectIDs); index2++)
				{
					UGCProject *pUGCProjectTest = objGetContainerData(GLOBALTYPE_UGCPROJECT, eauUGCProjectIDs[index2]);
					if(pUGCProjectTest->strPreviousShard && 0 == stricmp(pUGCProjectTest->strPreviousShard, previousShard) && pUGCProjectTest->iIdOnPreviousShard == pUGCProjectSeriesNode->iProjectID)
					{
						pUGCProjectImported = pUGCProjectTest;
						break;
					}
				}

				if(pUGCProjectImported)
					pUGCProjectSeriesNode->iProjectID = pUGCProjectImported->id;
				else
					eaRemove(&pUGCProjectSeriesVersion->eaChildNodes, index);
			}

			if(pUGCProjectSeriesVersion->eState == UGC_PUBLISHED)
			{
				UGCProjectSeriesVersion *pUGCProjectSeriesVersionReconst = CONTAINER_RECONST(UGCProjectSeriesVersion, pUGCProjectSeriesVersion);
				ugc_trh_UGCProjectSeries_UpdateCache(ATR_EMPTY_ARGS, &pUGCProjectSeriesForImport->ugcSearchCache, pUGCProjectSeriesVersionReconst);
			}
		}

		if(!objGetContainerData(GLOBALTYPE_UGCACCOUNT, pUGCProjectSeriesForImport->iOwnerAccountID))
		{
			NOCONST(UGCAccount) *pUGCAccount = StructCreateNoConst(parse_UGCAccount);
			pUGCAccount->accountID = pUGCProjectSeriesForImport->iOwnerAccountID;
			objRequestContainerCreate(NULL, GLOBALTYPE_UGCACCOUNT, pUGCAccount, GetAppGlobalType(), GetAppGlobalID());
		}

		objRequestContainerCreate(objCreateManagedReturnVal(aslUGCDataManager_SendProjectSeriesContainerForImport_UGCProjectSeriesCreate_CB, pSendProjectSeriesContainerForImportData),
			GLOBALTYPE_UGCPROJECTSERIES, pUGCProjectSeriesForImport, GetAppGlobalType(), GetAppGlobalID());

		StructDestroyNoConst(parse_UGCProjectSeries, pUGCProjectSeriesForImport);
	}

	eaiDestroy(&eauUGCProjectIDs);
	eaiDestroy(&eauUGCProjectSeriesIDs);
}

#include "aslUGCDataManager_cmd_c_ast.c"
