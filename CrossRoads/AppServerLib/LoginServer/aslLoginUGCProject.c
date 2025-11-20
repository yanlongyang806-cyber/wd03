#include "aslLoginUGCProject.h"

#include "AccountProxyCommon.h"
#include "GameAccountData/GameAccountData.h"
#include "LoginCommon.h"
#include "Player.h"
#include "StringUtil.h"
#include "StructNet.h"
#include "Textparser.h"
#include "UGCCommon.h"
#include "UGCProjectUtils.h"
#include "VirtualShard.h"
#include "aslLoginServer.h"
#include "UGCError.h"
#include "instancedStateMachine.h"
#include "message.h"
#include "ugcProjectCommon.h"
#include "ugcProjectCommon_h_ast.h"
#include "wlGroupPropertyStructs.h"
#include "aslLoginServer_h_ast.h"
#include "Logging.h"
#include "GamePermissionsCommon.h"
#include "LoggedTransactions.h"
#include "AutoTransDefs.h"
#include "utilitiesLib.h"
#include "aslLogin2_ClientComm.h"
#include "aslLogin2_StateMachine.h"
#include "EntityLib.h"
#include "Login2ServerCommon.h"
#include "Team.h"
#include "aslLogin2_Error.h"
#include "Entity.h"
#include "EntitySavedData.h"

#include "file.h"
#include "ServerLib.h"

#include "AutoGen/AppServerLib_autogen_remotefuncs.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/aslLogin2_StateMachine_h_ast.h"

void HereIsEditQueueCookie(int iLoginCookie, U32 iQueueCookie);

U32 GetUGCVirtualShardID(void)
{
	RefDictIterator iterator;
	VirtualShard *pVirtualShard;
	static U32 iRetVal = 0;
	if (iRetVal)
	{
		return iRetVal;
	}

	RefSystem_InitRefDictIterator(GlobalTypeToCopyDictionaryName(GLOBALTYPE_VIRTUALSHARD), &iterator);

	while ((pVirtualShard = RefSystem_GetNextReferentFromIterator(&iterator)))
	{
		if (pVirtualShard->bUGCShard)
		{
			iRetVal = pVirtualShard->id;
			break;
		}
	}

	return iRetVal;

}

bool aslLoginIsUGCAllowed(void)
{
	// TomY TODO - enable this on non-UGC shards per account with AccountProxyFindValueFromKeyInList?
	if (!gConf.bUserContent)
		return false; // UGC not enabled for this project.
	return gbUGCEnabled;
}

bool aslLoginCheckAccountPermissionsForUgcPublishBanned(GameAccountData *gameAccountData)
{
	if (!gameAccountData)
	{
		return false;
	}
	
    if (AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, GetAccountUgcPublishBanKey()))
	{
		return true;
	}

	return false;
}

static bool PlayerHasPermissionToDeleteProject(GameAccountData *gameAccountData, PossibleUGCProject *pProject)
{
	//can't delete "create new project" nor "copy" projects
	if (pProject->iID == 0)
	{
		return false;
	}

	//for now, any map that a player has permission to edit, he has permission to destroy. Might change later
	//with shared maps and so forth
	return true;
}

static void aslUGCPreprocessProjects(GameAccountData *gameAccountData, PossibleUGCProjects *pPossibleUGCProjects)
{
	int i;

	if (!pPossibleUGCProjects)
	{
		return;
	}
	
	//if the player is publish-banned, set bNoPublishing for all the possible projects
	if (aslLoginCheckAccountPermissionsForUgcPublishBanned(gameAccountData))
	{
		for (i=0; i < eaSize(&pPossibleUGCProjects->ppProjects); i++)
		{
			pPossibleUGCProjects->ppProjects[i]->iPossibleUGCProjectFlags |= POSSIBLEUGCPROJECT_FLAG_NOPUBLISHING;
		}
	}

	for (i=0; i < eaSize(&pPossibleUGCProjects->ppProjects); i++)
	{
		if (!pPossibleUGCProjects->ppProjects[i]->iVirtualShardID)
		{
			pPossibleUGCProjects->ppProjects[i]->iVirtualShardID = GetUGCVirtualShardID();
		}

		if (PlayerHasPermissionToDeleteProject(gameAccountData, pPossibleUGCProjects->ppProjects[i]))
		{
			pPossibleUGCProjects->ppProjects[i]->iPossibleUGCProjectFlags |= POSSIBLEUGCPROJECT_FLAG_CANDELETE;
		}
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLoginUGCProject_GetPossibleUGCProjects_Return(UGCProjectSearchInfo *pSearchInfo)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(pSearchInfo->loginCookie);
    GameAccountData *gameAccountData;

	if (!loginState)
	{
		return;
	}

	if (!loginState->netLink)
	{
		return;
	}

	if (!ISM_IsStateActive(LOGIN2_STATE_MACHINE, loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
	{
		aslLogin2_FailLogin(loginState, "LoginServer_DataWrongState");
		return;
	}

	StructDestroySafe(parse_PossibleUGCProjects, &loginState->possibleUGCProjects);
	loginState->possibleUGCProjects = StructClone(parse_PossibleUGCProjects, pSearchInfo->pPossibleUGCProjects);

    gameAccountData = GET_REF(loginState->hGameAccountData);
	if(loginState->possibleUGCProjects && gameAccountData)
	{
		loginState->editQueueCookie = 0;

		aslUGCPreprocessProjects(gameAccountData, loginState->possibleUGCProjects);

        aslLogin2_SendPossibleUGCProjects(loginState, loginState->possibleUGCProjects);
	}
	else
	{
		aslLogin2_FailLogin(loginState, "LoginServer_UGCProjectPacketCorruption");
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLoginUGCProject_SearchPossibleUGCProjects_Return(UGCProjectSearchInfo *pSearchInfo)
{
	Login2State *loginState = aslLogin2_GetActiveLoginState(pSearchInfo->loginCookie);
    GameAccountData *gameAccountData;

	if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
		return;
    }

	StructDestroySafe(parse_PossibleUGCProjects, &loginState->possibleUGCImports);
	loginState->possibleUGCImports = StructClone(parse_PossibleUGCProjects, pSearchInfo->pPossibleUGCProjects);

    gameAccountData = GET_REF(loginState->hGameAccountData);
    if ( gameAccountData && loginState->possibleUGCImports )
    {
	    aslUGCPreprocessProjects(gameAccountData, loginState->possibleUGCImports);

        aslLogin2_SendPossibleUGCImports(loginState, loginState->possibleUGCImports);
    }
}

U32 aslLoginUGCGetBaseEditSlots(GameAccountData *pGameAccount)
{
    S32 iVal;
    if(gamePermission_Enabled())
    {
        if(!GetGamePermissionValueUncached(pGameAccount, GAME_PERMISSION_UGC_PROJECT_SLOTS, &iVal))
        {
            iVal = 0;
        }
    }
    else
    {
	    iVal = 8;
    }
    return iVal;
}

static UGCProjectSearchFilter* aslLoginUGCCreateReadFilter(void)
{
	UGCProjectSearchFilter *pFilter = StructCreate(parse_UGCProjectSearchFilter);

	pFilter->eType = UGCFILTER_PERMISSIONS;
	pFilter->pField = StructAllocString("ReadPermission");
	pFilter->eComparison = UGCCOMPARISON_GREATERTHAN;
	pFilter->uIntValue = 0;

	return pFilter;
}

U32 aslLoginUGCGetProjectMaxSlots(GameAccountData *gameAccountData)
{
    char *pExtraMapCountValue = AccountProxyFindValueFromKeyContainer(gameAccountData->eaAccountKeyValues, GetAccountUgcProjectExtraSlotsKey());
	int iExtraMapSlots = (pExtraMapCountValue ? atoi(pExtraMapCountValue) : 0);
	int iExtraGADSlots = gad_GetAttribInt(gameAccountData, GetAccountUgcProjectExtraSlotsKey());

	return (U32)(aslLoginUGCGetBaseEditSlots(gameAccountData) + iExtraMapSlots + iExtraGADSlots);
}

U32 aslLoginUGCGetSeriesMaxSlots(GameAccountData *gameAcountData)
{
    char *pExtraMapCountValue = AccountProxyFindValueFromKeyContainer(gameAcountData->eaAccountKeyValues, GetAccountUgcProjectSeriesExtraSlotsKey());
	int iExtraMapSlots = (pExtraMapCountValue ? atoi(pExtraMapCountValue) : 0);
	int iExtraGADSlots = gad_GetAttribInt(gameAcountData, GetAccountUgcProjectSeriesExtraSlotsKey());

	return (U32)(aslLoginUGCGetBaseEditSlots(gameAcountData) + iExtraMapSlots + iExtraGADSlots);
}

static void aslLoginUGCFillInSearchInfo(GameAccountData *gameAccountData, ContainerID accountID, U64 loginCookie, S32 clientAccessLevel, UGCProjectSearchInfo *pSearchInfo)
{
	pSearchInfo->iOwnerAccountID = accountID;
	pSearchInfo->iAccessLevel = clientAccessLevel;
	pSearchInfo->iMaxProjectSlots = aslLoginUGCGetProjectMaxSlots(gameAccountData);
	pSearchInfo->iMaxSeriesSlots = aslLoginUGCGetSeriesMaxSlots(gameAccountData);
	pSearchInfo->loginCookie = loginCookie;
	pSearchInfo->pcShardName = GetShardNameFromShardInfoString();
	pSearchInfo->loginServerID = GetAppGlobalID();
}

void aslLoginSendUGCProjects(Login2State *loginState)
{
	UGCProjectSearchInfo searchOwned = {0};
	UGCProjectSearchInfo *searchInfo = NULL;
	int maxEditSlots = 0;
    GameAccountData *gameAccountData = GET_REF(loginState->hGameAccountData);

	aslLoginUGCFillInSearchInfo(gameAccountData, loginState->accountID, loginState->loginCookie, loginState->clientAccessLevel, &searchOwned);

	// Grab my projects for editing
	RemoteCommand_Intershard_aslUGCDataManager_GetPossibleUGCProjects(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0, &searchOwned);

	// Cache GameAccountData-based values
	loginState->prevSentToClientUGCProjectMaxSlots = searchOwned.iMaxProjectSlots;
	loginState->prevSentToClientUGCSeriesMaxSlots = searchOwned.iMaxSeriesSlots;
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD ACMD_IFDEF(APPSERVER);
void aslLoginUGCProject_RequestReviewsForPageReturn(UGCDetails *pDetails, S32 iPageNumber, U64 iLoginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(iLoginCookie);

	// Invalid login link, bail
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }

	aslLogin2_SendReviews(loginState, SAFE_MEMBER2(pDetails, pProject, id), SAFE_MEMBER2(pDetails, pSeries, id), iPageNumber, &pDetails->pProject->ugcReviews);
}

void aslLoginHandleRequestReviewsForPage(Packet *pak, Login2State *loginState)
{
	ContainerID uProjectID = pktGetU32(pak);
	ContainerID uSeriesID = pktGetU32(pak);
	S32 iPageNumber = (S32)pktGetU32(pak);
		
	if (!GetUGCVirtualShardID())
	{
		aslLogin2_FailLogin(loginState, "Login2_NoUGCShard");
		return;
	}
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }
	if (!aslLoginIsUGCAllowed())
	{
        aslLogin2_FailLogin(loginState, "Login2_UGCEditingNotAllowed");
		return;
	}

	RemoteCommand_Intershard_aslUGCDataManager_RequestReviewsForPageFromLoginServer(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		uProjectID, uSeriesID, iPageNumber, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLogin_ClearAuthorAllowsFeatured_Return(bool bSuccess, U64 loginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);

    // Invalid login link, bail
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }

	if(bSuccess)
    {
        loginState->authorAllowsFeaturedCleared = true;
    }
	else
    {
		aslLogin2_FailLogin(loginState, "Login2_CantEditFeaturedProjects");
    }
}

void aslLoginHandleAcceptedUGCCreateProjectEULA(Packet *pak, Login2State *loginState)
{
	U32 eulaCRC = pktGetU32(pak);
	char *eulaKey = GetAccountUgcCreateProjectEULAKey();
	char crc[50];

	sprintf(crc, "%d", eulaCRC);

	// VAS 010613: Hi Andy! In order for the EULAs to work, you need this if/else statement that either sets a key value or GAD attrib, depending on what's allowed
	if (gConf.bDontAllowGADModification)
		APSetKeyValueSimple(loginState->accountID, eulaKey, eulaCRC, 0, NULL, NULL);
	else
		AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_GAMEACCOUNTDATA, loginState->accountID, eulaKey, crc);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void UGCProject_Delete_Return(bool bSuccess, U64 loginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);

    // Invalid login link, bail
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
		return;
    }

	if(bSuccess)
    {
		aslLoginSendUGCProjects(loginState);
    }
}

void aslLoginHandleReadyToChooseUGCProject(Packet *pak, Login2State *loginState)
{
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }

	if(!ugc_DevMode && loginState->clientAccessLevel)
	{
		HereIsEditQueueCookie(Login2_ShortenToken(loginState->loginCookie), U32_MAX);
	}
	else
	{
		RemoteCommand_EnterUGCEditQueue(GLOBALTYPE_MAPMANAGER, 0, GetAppGlobalID(), Login2_ShortenToken(loginState->loginCookie));
	}
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslUGCDataManager_ProjectSeries_Return(bool success, U64 loginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);

    // Invalid login link, bail
    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }

	aslLoginSendUGCProjects(loginState);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLogin_ProjectSeries_Create_Return(U64 loginCookie, ContainerID seriesID)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);
	if(loginState)
	{
        //LOGIN2UGC - logic here seems a bit weird.
        if(aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
        {
			if(seriesID)
            {
				aslLoginSendUGCProjects(loginState);
            }
        }

        aslLogin2_SendUGCProjectSeriesCreateResult(loginState, seriesID);
	}
}

void aslLoginUGCProjectSeriesCreate( Packet* pak, Login2State* loginState )
{
	UGCProjectSeries* series = pktGetStructFromUntrustedSource( pak, parse_UGCProjectSeries );

	if( !loginState->possibleUGCProjects || !loginState->possibleUGCProjects->bNewProjectSeries ) {
        aslLogin2_SendUGCProjectSeriesCreateResult(loginState, 0);
	} else {
		RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Create(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
			series, loginState->accountID, loginState->accountDisplayName, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);
	}
	StructDestroy( parse_UGCProjectSeries, series );
}

void aslLoginUGCProjectSeriesDestroy( Packet* pak, Login2State* loginState )
{
	ContainerID seriesID = pktGetU32( pak );
	RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Destroy(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		seriesID, loginState->accountID, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);
}

static void aslLoginUGCProjectSeriesUpdate_Result( Login2State *loginState, bool success, const char* errorMsg )
{
    aslLogin2_SendUGCProjectSeriesUpdateResult(loginState, success, errorMsg);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLogin_ProjectSeries_Update_Return(bool success, U64 loginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);

    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        return;
    }

	if(success)
	{
		aslLoginSendUGCProjects(loginState);
		aslLoginUGCProjectSeriesUpdate_Result(loginState, true, NULL);
	}
	else
		aslLoginUGCProjectSeriesUpdate_Result(loginState, false, "Transaction Failed");
}

void aslLoginUGCProjectSeriesUpdate( Packet* pak, Login2State* loginState )
{
	UGCProjectSeries* series = pktGetStructFromUntrustedSource( pak, parse_UGCProjectSeries );

	// Validate the series
	{
		UGCRuntimeStatus* status = StructCreate( parse_UGCRuntimeStatus );
		
		ugcSetStageAndAdd( status, "UGC Series Validate" );
		ugcValidateSeries( series );

		if( ugcValidateErrorfIfStatusHasErrors( status )) {
			char buffer[256];
			sprintf(buffer, "Campaign has errors");
			aslLoginUGCProjectSeriesUpdate_Result( loginState, false, buffer );
			StructDestroySafe( parse_UGCRuntimeStatus, &status );
			return;
		}

		StructDestroySafe( parse_UGCRuntimeStatus, &status );
	}

	RemoteCommand_Intershard_aslUGCDataManager_ProjectSeries_Update(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		series, loginState->accountID, loginState->accountDisplayName, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);

	StructDestroy( parse_UGCProjectSeries, series );
}

// NOTE: Keep this in sync with the GSL-Search function
AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLoginUGCSearch_Return(UGCProjectSearchInfo* pSearch)
{
	Login2State *loginState = aslLogin2_GetActiveLoginState(pSearch->loginCookie);
	Packet* pak = NULL;

	if(!loginState)
		return;

	if(!pSearch->pUGCSearchResult)
	{
		pSearch->pUGCSearchResult = StructCreate(parse_UGCSearchResult); // ???: Is this a memory leak because RemoteCommandCheck will allocate even if failed? <NPK 2010-04-13>
		SET_HANDLE_FROM_STRING("Message", "UGC.Search_GenericError", pSearch->pUGCSearchResult->hErrorMessage);
	}

    aslLogin2_SendUGCSearchResults(loginState, pSearch->pUGCSearchResult);

}

void aslLoginUGCSetPlayerIsReviewer( Packet* pak, Login2State* loginState )
{
	bool bSet = pktGetBool( pak );
	
	// VAS 010613: Hi Andy! In order for the EULAs to work, you need this if/else statement that either sets a key value or GAD attrib, depending on what's allowed
	if (gConf.bDontAllowGADModification)
		APSetKeyValueSimple(loginState->accountID, GetAccountUgcReviewerKey(), bSet ? 1 : 0, 0, NULL, NULL);
	else
		AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_GAMEACCOUNTDATA, loginState->accountID, GetAccountUgcReviewerKey(), bSet ? "1" : "0");
}

void aslLoginUGCSetSearchEULAAccepted( Packet* pak, Login2State* loginState )
{
	bool bIsReviewer = pktGetBool( pak );

	// VAS 010613: Hi Andy! In order for the EULAs to work, you need this if/else statement that either sets a key value or GAD attrib, depending on what's allowed
	if (gConf.bDontAllowGADModification)
		APSetKeyValueSimple(loginState->accountID, GetAccountUgcProjectSearchEULAKey(), bIsReviewer ? 1 : 0, 0, NULL, NULL);
	else
		AutoTrans_slGAD_tr_SetAttrib(NULL, GLOBALTYPE_LOGINSERVER, GLOBALTYPE_GAMEACCOUNTDATA, loginState->accountID, GetAccountUgcProjectSearchEULAKey(), bIsReviewer ? "1" : "0");
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void aslLogin_UGCSearchByID_Return(UGCProjectList *pResult, U64 loginCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginState(loginCookie);
	Packet* pak = NULL;

	if(!loginState)
		return;

    aslLogin2_SendUGCProjectRequestByIDResults(loginState, pResult);
}

void aslLoginUGCProjectRequestByID( Packet* pak, Login2State* loginState )
{
	UGCIDList* pList = pktGetStructFromUntrustedSource( pak, parse_UGCIDList );

	UGCIntershardData ugcIntershardData;
	StructInit(parse_UGCIntershardData, &ugcIntershardData);
	ugcIntershardData.pcShardName = GetShardNameFromShardInfoString();
	ugcIntershardData.loginCookie = loginState->loginCookie;

	RemoteCommand_Intershard_aslUGCSearchManager_SearchByID(ugc_ShardName(), GLOBALTYPE_UGCSEARCHMANAGER, 0, pList, &ugcIntershardData);

	StructDestroySafe( parse_UGCIDList, &pList );
}

AUTO_COMMAND_REMOTE;
void EditQueuePlaceUpdate(int iLoginCookie, int iNumAheadOfYouInQueue)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookie(iLoginCookie);

    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
        RemoteCommand_CancelEditQueueByLoginCookie(GLOBALTYPE_MAPMANAGER, 0, GetAppGlobalID(), iLoginCookie);
        return;
    }

    aslLogin2_SendUGCNumAheadInQueue(loginState, iNumAheadOfYouInQueue);
}

AUTO_COMMAND_REMOTE;
void HereIsEditQueueCookie(int iLoginCookie, U32 iQueueCookie)
{
    Login2State *loginState = aslLogin2_GetActiveLoginStateShortCookie(iLoginCookie);

    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
	{
		RemoteCommand_CancelEditQueueByQueueCookie(GLOBALTYPE_MAPMANAGER, 0, iQueueCookie);
		return;
	}

	loginState->editQueueCookie = iQueueCookie;

    aslLogin2_SendUGCEditPermissionCookie(loginState, iQueueCookie);
}


void aslLoginHandleDestroyUGCProject(Packet *pak, Login2State *loginState)
{
	int i;
	bool bFound = false;

    if (!aslLogin2_FailIfNotInState(loginState, LOGIN2_STATE_UGC_PROJECT_SELECT))
    {
		return;
	}
	if (!aslLoginIsUGCAllowed())
	{
		aslLogin2_FailLogin(loginState, "Login2_UGCEditingNotAllowed");
		return;
	}

    if (loginState->chosenUGCProjectForDelete)
	{
		StructReset(parse_PossibleUGCProject, loginState->chosenUGCProjectForDelete);
	}
	else
	{
		loginState->chosenUGCProjectForDelete = StructCreate(parse_PossibleUGCProject);
	}

	if (!ParserRecv(parse_PossibleUGCProject, pak, loginState->chosenUGCProjectForDelete, aslLoginServerClientsAreUntrustworthy() ? RECVDIFF_FLAG_UNTRUSTWORTHY_SOURCE : 0))
	{
	    aslLogin2_FailLogin(loginState, "LoginServer_PacketCorruption");
		return;
	}

	//verify that the chosenmap we got is one of the choices we sent to the game server in the first place
	for (i=0; i < eaSize(&loginState->possibleUGCProjects->ppProjects); i++)
	{
		if (loginState->chosenUGCProjectForDelete->iID == loginState->possibleUGCProjects->ppProjects[i]->iID 
			&& (loginState->possibleUGCProjects->ppProjects[i]->iPossibleUGCProjectFlags & POSSIBLEUGCPROJECT_FLAG_CANDELETE))
		{
			bFound = true;
			break;
		}
	}

	if (!bFound)
	{
		aslLogin2_FailLogin(loginState, "Login2_InvalidDelete");
		return;
	}

	// Run this transaction on the UGCDataManager, since only it knows if UGCAccounts exist
	RemoteCommand_Intershard_UGCProject_Delete(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		loginState->chosenUGCProjectForDelete->iID, GetShardNameFromShardInfoString(), GetAppGlobalID(), loginState->loginCookie);
}

