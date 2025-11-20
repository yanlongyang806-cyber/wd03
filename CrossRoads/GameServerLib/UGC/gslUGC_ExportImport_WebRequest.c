//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These are XMLRPC commands used by python scripts (C:\src\CrossRoads\Common\UGCExportImport\UGCExportImport.py) which are in turn used by UGCExport.exe and UGCImport.exe.
//
// IMPORTANT - please keep these functions in sync with the python scripts and EXEs.
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#include "stdtypes.h"
#include "cmdparse.h"
#include "textparser.h"
#include "textParserXML.h"
#include "utilitiesLib.h"
#include "ServerLib.h"
#include "crypt.h"
#include "file.h"
#include "StringUtil.h"

#include "TransactionOutcomes.h"
#include "LoggedTransactions.h"
#include "WebRequests.h"

#include "UGCProjectCommon.h"
#include "UGCProjectCommon_h_ast.h"

#include "UGCCommon.h"

#include "gslUGCTransactions.h"
#include "gslUGC_cmd.h"

#include "gslUGC_ExportImport_WebRequest_c_ast.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"

AUTO_STRUCT;
typedef struct UGCSearchResultAndIndex
{
	UGCSearchResult *pUGCSearchResult;
	int iUGCSearchResultIndex;
	U32 secondsSince2000;
} UGCSearchResultAndIndex;

static UGCSearchResultAndIndex **s_eaUGCSearchResultAndIndex = NULL;

static void UGCExport_SearchInit_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		UGCSearchResultAndIndex *pUGCSearchResultAndIndex = NULL;
		int index = 0;
		char idStr[256] = "";

		if(!s_eaUGCSearchResultAndIndex)
			eaCreate(&s_eaUGCSearchResultAndIndex);

		FOR_EACH_IN_EARRAY_FORWARDS(s_eaUGCSearchResultAndIndex, UGCSearchResultAndIndex, pUGCSearchResultAndIndexIter)
		{
			int idx = FOR_EACH_IDX(s_eaUGCSearchResultAndIndex, pUGCSearchResultAndIndexIter);
			if(timeSecondsSince2000() - pUGCSearchResultAndIndexIter->secondsSince2000 > 60*60) // if older than 1 hour, it must have timed out, reuse it
			{
				index = idx;
				pUGCSearchResultAndIndex = pUGCSearchResultAndIndexIter;
				break;
			}
		}
		FOR_EACH_END;

		if(pUGCSearchResultAndIndex)
			StructReset(parse_UGCSearchResultAndIndex, pUGCSearchResultAndIndex);
		else
		{
			pUGCSearchResultAndIndex = StructCreate(parse_UGCSearchResultAndIndex);
			index = eaSize(&s_eaUGCSearchResultAndIndex);
			eaPush(&s_eaUGCSearchResultAndIndex, pUGCSearchResultAndIndex);
		}

		pUGCSearchResultAndIndex->secondsSince2000 = timeSecondsSince2000();

		RemoteCommandCheck_aslUGCDataManager_GetAllContent(returnVal, &pUGCSearchResultAndIndex->pUGCSearchResult);

		ContainerIDToString(index, idStr);
		DoSlowCmdReturn(true, idStr, slowReturnInfo);
	}
	else
	{
		DoSlowCmdReturn(true, "", slowReturnInfo);
	}
}

// SearchInit should always return a new, unique string for each call. The key is used in subsequent calls to SearchNext in order to iterate over the set of IDs.
//
// TODO: it would be nice to allow recycling a key by passing it in.
AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
const char *UGCExport_SearchInit(CmdContext *pContext, bool bIncludeSaved, bool bIncludePublished, const char *strUGCProjectSearchInfo)
{
	CmdSlowReturnForServerMonitorInfo *slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	UGCProjectSearchInfo *pUGCProjectSearchInfo = NULL;
	if(!nullStr(strUGCProjectSearchInfo))
	{
		pUGCProjectSearchInfo = StructCreate(parse_UGCProjectSearchInfo);
		ParserReadText(strUGCProjectSearchInfo, parse_UGCProjectSearchInfo, pUGCProjectSearchInfo, 0);
	}

	RemoteCommand_aslUGCDataManager_GetAllContent(objCreateManagedReturnVal(UGCExport_SearchInit_CB, slowReturnInfo), GLOBALTYPE_UGCDATAMANAGER, 0, bIncludeSaved, bIncludePublished, pUGCProjectSearchInfo);

	StructDestroySafe(parse_UGCProjectSearchInfo, &pUGCProjectSearchInfo);

	return NULL;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
const char *UGCExport_SearchNext(const char *searchKey)
{
	int index, i;
	UGCSearchResult *pPartialResult = StructCreate(parse_UGCSearchResult);
	char *estrResult = NULL;
	UGCSearchResultAndIndex *pUGCSearchResultAndIndex = NULL;

	index = atoi(searchKey);
	if(index < 0 || index >= eaSize(&s_eaUGCSearchResultAndIndex))
		return "";

	pUGCSearchResultAndIndex = s_eaUGCSearchResultAndIndex[index];

	if(!pUGCSearchResultAndIndex || !pUGCSearchResultAndIndex->pUGCSearchResult)
		return "";
	if(pUGCSearchResultAndIndex->iUGCSearchResultIndex < 0 || pUGCSearchResultAndIndex->iUGCSearchResultIndex >= eaSize(&pUGCSearchResultAndIndex->pUGCSearchResult->eaResults))
		return "";

	for(i = pUGCSearchResultAndIndex->iUGCSearchResultIndex;
		i < eaSize(&pUGCSearchResultAndIndex->pUGCSearchResult->eaResults) && i < pUGCSearchResultAndIndex->iUGCSearchResultIndex + 100;
		i++)
	{
		UGCContentInfo *pUGCContentInfo = StructCreate(parse_UGCContentInfo);
		pUGCContentInfo->iUGCProjectID = pUGCSearchResultAndIndex->pUGCSearchResult->eaResults[i]->iUGCProjectID;
		pUGCContentInfo->iUGCProjectSeriesID = pUGCSearchResultAndIndex->pUGCSearchResult->eaResults[i]->iUGCProjectSeriesID;
		eaPush(&pPartialResult->eaResults, pUGCContentInfo);
	}

	pUGCSearchResultAndIndex->iUGCSearchResultIndex = i;
	if(pUGCSearchResultAndIndex->iUGCSearchResultIndex >= eaSize(&pUGCSearchResultAndIndex->pUGCSearchResult->eaResults))
	{
		eaRemove(&s_eaUGCSearchResultAndIndex, index);
		StructDestroy(parse_UGCSearchResultAndIndex, pUGCSearchResultAndIndex);
	}

	ParserWriteText(&estrResult, parse_UGCSearchResult, pPartialResult, 0, 0, 0);
	StructDestroy(parse_UGCSearchResult, pPartialResult);

	return estrResult;
}

static void UGCExport_GetUGCProjectContainer_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		char *estrResult = NULL;
		UGCProject *pUGCProject = NULL;
		RemoteCommandCheck_aslUGCDataManager_GetProjectContainer(returnVal, &pUGCProject);

		if(pUGCProject)
		{
			ParserWriteText(&estrResult, parse_UGCProject, pUGCProject, 0, 0, 0);
			XMLWrapInCdata(&estrResult);

			DoSlowCmdReturn(true, estrResult, slowReturnInfo);

			estrDestroy(&estrResult);
			StructDestroy(parse_UGCProject, pUGCProject);

			return;
		}
	}

	DoSlowCmdReturn(true, "", slowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
const char *UGCExport_GetUGCProjectContainer(CmdContext *pContext, ContainerID uUGCProjectID)
{
	CmdSlowReturnForServerMonitorInfo *slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	RemoteCommand_aslUGCDataManager_GetProjectContainer(objCreateManagedReturnVal(UGCExport_GetUGCProjectContainer_CB, slowReturnInfo), GLOBALTYPE_UGCDATAMANAGER, 0, uUGCProjectID);

	return NULL;
}

static void UGCExport_GetUGCProjectSeriesContainer_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		char *estrResult = NULL;
		UGCProjectSeries *pUGCProjectSeries = NULL;
		RemoteCommandCheck_aslUGCDataManager_GetProjectSeriesContainer(returnVal, &pUGCProjectSeries);

		if(pUGCProjectSeries)
		{
			ParserWriteText(&estrResult, parse_UGCProjectSeries, pUGCProjectSeries, 0, 0, 0);
			XMLWrapInCdata(&estrResult);

			DoSlowCmdReturn(true, estrResult, slowReturnInfo);

			estrDestroy(&estrResult);
			StructDestroy(parse_UGCProjectSeries, pUGCProjectSeries);

			return;
		}
	}

	DoSlowCmdReturn(true, "", slowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
const char *UGCExport_GetUGCProjectSeriesContainer(CmdContext *pContext, ContainerID uUGCProjectSeriesID)
{
	CmdSlowReturnForServerMonitorInfo *slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	RemoteCommand_aslUGCDataManager_GetProjectSeriesContainer(objCreateManagedReturnVal(UGCExport_GetUGCProjectSeriesContainer_CB, slowReturnInfo), GLOBALTYPE_UGCDATAMANAGER, 0, uUGCProjectSeriesID);

	return NULL;
}

static void UGCExport_GetUGCPatchInfo_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		char *estrResult = NULL;
		UGCPatchInfo *pUGCPatchInfo = NULL;
		RemoteCommandCheck_aslUGCDataManager_GetPatchInfo(returnVal, &pUGCPatchInfo);

		if(pUGCPatchInfo)
		{
			ParserWriteText(&estrResult, parse_UGCPatchInfo, pUGCPatchInfo, 0, 0, 0);

			DoSlowCmdReturn(true, estrResult, slowReturnInfo);

			estrDestroy(&estrResult);
			StructDestroy(parse_UGCPatchInfo, pUGCPatchInfo);

			return;
		}
	}

	DoSlowCmdReturn(true, "", slowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
const char *UGCExport_GetUGCPatchInfo(CmdContext *pContext)
{
	CmdSlowReturnForServerMonitorInfo *slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	RemoteCommand_aslUGCDataManager_GetPatchInfo(objCreateManagedReturnVal(UGCExport_GetUGCPatchInfo_CB, slowReturnInfo), GLOBALTYPE_UGCDATAMANAGER, 0);

	return NULL;
}

AUTO_STRUCT;
typedef struct UGCProjectContainerAndDataForImport
{
	UGCProject *pUGCProjectOriginal;

	UGCProjectData *pUGCProjectDataPublished;			AST(LATEBIND)
	UGCProjectData *pUGCProjectDataSaved;				AST(LATEBIND)

	UGCProject *pUGCProjectImported;

	DynamicPatchInfo *pDynamicPatchInfo;

	CmdSlowReturnForServerMonitorInfo *slowReturnInfo;	NO_AST
} UGCProjectContainerAndDataForImport;

AUTO_STRUCT;
typedef struct UGCProjectSeriesContainerForImport
{
	UGCProjectSeries *pUGCProjectSeriesOriginal;

	UGCProjectSeries *pUGCProjectSeriesImported;

	CmdSlowReturnForServerMonitorInfo *slowReturnInfo;	NO_AST
} UGCProjectSeriesContainerForImport;

static void UGCImport_FailWithError(const char *error, UGCProjectContainerAndDataForImport **ppUGCProjectContainerAndDataForImport)
{
	ServerLib_SetPatchInfo(NULL);

	setProductionEditMode(false);

	if(error && error[0])
		DoSlowCmdReturn(true, error, (*ppUGCProjectContainerAndDataForImport)->slowReturnInfo);
	else
		DoSlowCmdReturn(true, "An unknown error occurred on the UGCDataManager while importing UGC Project", (*ppUGCProjectContainerAndDataForImport)->slowReturnInfo);
	StructDestroySafe(parse_UGCProjectContainerAndDataForImport, ppUGCProjectContainerAndDataForImport);
}

static void UGCImport_SucceedWithMessage(const char *message, UGCProjectContainerAndDataForImport **ppUGCProjectContainerAndDataForImport)
{
	ServerLib_SetPatchInfo(NULL);

	setProductionEditMode(false);

	DoSlowCmdReturn(true, message, (*ppUGCProjectContainerAndDataForImport)->slowReturnInfo);
	StructDestroySafe(parse_UGCProjectContainerAndDataForImport, ppUGCProjectContainerAndDataForImport);
}

static void UGCImport_Succeed(UGCProjectContainerAndDataForImport **ppUGCProjectContainerAndDataForImport)
{
	UGCImport_SucceedWithMessage("", ppUGCProjectContainerAndDataForImport);
}

static void UGCImport_SaveSaved_CB(TransactionReturnVal *returnVal, UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
		UGCImport_Succeed(&pUGCProjectContainerAndDataForImport);
	else
		UGCImport_FailWithError(GetTransactionFailureString(returnVal), &pUGCProjectContainerAndDataForImport);
}

static void UGCImport_GetProjectContainerForSaved_CB(TransactionReturnVal *returnVal, UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport)
{
	UGCProject *pUGCProject = NULL;
	if(TRANSACTION_OUTCOME_SUCCESS == RemoteCommandCheck_aslUGCDataManager_GetProjectContainer(returnVal, &pUGCProject) && pUGCProject)
	{
		TransactionReturnVal *pRetVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCImport_SaveSaved_CB, pUGCProjectContainerAndDataForImport);
		char *estrError = NULL;

		// refresh UGCProject
		StructDestroy(parse_UGCProject, pUGCProjectContainerAndDataForImport->pUGCProjectImported);
		pUGCProjectContainerAndDataForImport->pUGCProjectImported = pUGCProject;

		estrError = UGCImport_Save(pUGCProjectContainerAndDataForImport->pUGCProjectImported, pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved, /*bPublish=*/false, pRetVal);
		if(estrError)
		{
			UGCImport_FailWithError(estrError, &pUGCProjectContainerAndDataForImport);
			estrDestroy(&estrError);
		}
	}
	else
		UGCImport_FailWithError("Failed to refresh UGCProject after creating new version for saved content.", &pUGCProjectContainerAndDataForImport);
}

static void UGCImport_CreateNewVersionForSaved_CB(TransactionReturnVal *returnVal, UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		TransactionReturnVal *pRetVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCImport_GetProjectContainerForSaved_CB, pUGCProjectContainerAndDataForImport);
		RemoteCommand_aslUGCDataManager_GetProjectContainer(pRetVal, GLOBALTYPE_UGCDATAMANAGER, 0, pUGCProjectContainerAndDataForImport->pUGCProjectImported->id);
	}
	else
		UGCImport_FailWithError(GetTransactionFailureString(returnVal), &pUGCProjectContainerAndDataForImport);
}

static void UGCImport_SavePublished_CB(TransactionReturnVal *returnVal, UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport)
{
	if(TRANSACTION_OUTCOME_SUCCESS == returnVal->eOutcome)
	{
		if(pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved)
		{
			TransactionReturnVal *pRetVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCImport_CreateNewVersionForSaved_CB, pUGCProjectContainerAndDataForImport);
			AutoTrans_trUGCProjectFillInNewVersionForImport(pRetVal, GLOBALTYPE_UGCDATAMANAGER,
				GLOBALTYPE_UGCPROJECT, pUGCProjectContainerAndDataForImport->pUGCProjectImported->id,
				/*bClearExistingVersions=*/false);
		}
		else
			UGCImport_Succeed(&pUGCProjectContainerAndDataForImport);
	}
	else
		UGCImport_FailWithError(GetTransactionFailureString(returnVal), &pUGCProjectContainerAndDataForImport);
}

static void UGCImport_ImportUGCProjectContainerAndData_SendProjectContainerForImport_CB(TransactionReturnVal *returnVal, UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport)
{
	UGCProjectContainerCreateForImportData *pReturn = NULL;
	if(TRANSACTION_OUTCOME_SUCCESS == RemoteCommandCheck_aslUGCDataManager_SendProjectContainerForImport(returnVal, &pReturn))
	{
		if(pReturn && pReturn->pUGCProject && (!pReturn->estrError || !pReturn->estrError[0]))
		{
			ServerLib_SetPatchInfo(pReturn->pDynamicPatchInfo);

			setProductionEditMode(true);

			pUGCProjectContainerAndDataForImport->pUGCProjectImported = StructClone(parse_UGCProject, pReturn->pUGCProject);

			if(pUGCProjectContainerAndDataForImport->pUGCProjectDataPublished)
			{
				TransactionReturnVal *pRetVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCImport_SavePublished_CB, pUGCProjectContainerAndDataForImport);
				char *estrError = UGCImport_Save(pUGCProjectContainerAndDataForImport->pUGCProjectImported, pUGCProjectContainerAndDataForImport->pUGCProjectDataPublished, /*bPublish=*/true, pRetVal);
				if(estrError)
				{
					UGCImport_FailWithError(estrError, &pUGCProjectContainerAndDataForImport);
					estrDestroy(&estrError);
				}
			}
			else if(pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved)
			{
				TransactionReturnVal *pRetVal = LoggedTransactions_CreateManagedReturnVal(__FUNCTION__, UGCImport_SaveSaved_CB, pUGCProjectContainerAndDataForImport);
				char *estrError = UGCImport_Save(pUGCProjectContainerAndDataForImport->pUGCProjectImported, pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved, /*bPublish=*/false, pRetVal);

				if(estrError)
				{
					UGCImport_FailWithError(estrError, &pUGCProjectContainerAndDataForImport);
					estrDestroy(&estrError);
				}
			}
			else
				UGCImport_Succeed(&pUGCProjectContainerAndDataForImport);
		}
		else
			UGCImport_FailWithError(pReturn ? pReturn->estrError : NULL, &pUGCProjectContainerAndDataForImport);

		StructDestroy(parse_UGCProjectContainerCreateForImportData, pReturn);
	}
	else
		UGCImport_FailWithError(NULL, &pUGCProjectContainerAndDataForImport);
}

static void UGCImport_DeleteAllUGC_CB(TransactionReturnVal *returnVal, CmdSlowReturnForServerMonitorInfo *slowReturnInfo)
{
	char *strResult = NULL;
	if(TRANSACTION_OUTCOME_SUCCESS == RemoteCommandCheck_aslUGCDataManager_DeleteAllUGC(returnVal, &strResult))
		DoSlowCmdReturn(true, strResult, slowReturnInfo);
	else
		DoSlowCmdReturn(true, "UNKNOWN ERROR", slowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
char *UGCImport_DeleteAllUGC(CmdContext *pContext, const char *strComment)
{
	CmdSlowReturnForServerMonitorInfo *slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	RemoteCommand_aslUGCDataManager_DeleteAllUGC(objCreateManagedReturnVal(UGCImport_DeleteAllUGC_CB, slowReturnInfo), GLOBALTYPE_UGCDATAMANAGER, 0, strComment);

	return NULL;
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
char *UGCImport_ImportUGCProjectContainerAndData(CmdContext *pContext, const char *strUGCProject, const char *strUGCProjectDataPublished, const char *strUGCProjectDataSaved,
	const char *strPreviousShard, const char *strComment, bool forceDelete)
{
	UGCProjectContainerAndDataForImport *pUGCProjectContainerAndDataForImport = StructCreate(parse_UGCProjectContainerAndDataForImport);

	pUGCProjectContainerAndDataForImport->slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	pUGCProjectContainerAndDataForImport->pUGCProjectOriginal = StructCreate(parse_UGCProject);
	ParserReadText(strUGCProject, parse_UGCProject, pUGCProjectContainerAndDataForImport->pUGCProjectOriginal, 0);

	if(strUGCProjectDataPublished && strUGCProjectDataPublished[0])
	{
		char *estrNewNotes = NULL;
		UGCProjectDataHeader *pUGCProjectDataHeader = NULL;

		pUGCProjectContainerAndDataForImport->pUGCProjectDataPublished = StructCreate(parse_UGCProjectData);
		ParserReadText(strUGCProjectDataPublished, parse_UGCProjectData, pUGCProjectContainerAndDataForImport->pUGCProjectDataPublished, 0);

		pUGCProjectDataHeader = (UGCProjectDataHeader*)pUGCProjectContainerAndDataForImport->pUGCProjectDataPublished;

		estrPrintf(&estrNewNotes, "Project imported from other shard (%s): %s\n\n%s",
			!nullStr(strPreviousShard) ? strPreviousShard : "UNKNOWN",
			!nullStr(strComment) ? strComment : "NO COMMENT",
			!nullStr(SAFE_MEMBER2(pUGCProjectDataHeader, project, strNotes))
				? SAFE_MEMBER2(pUGCProjectDataHeader, project, strNotes)
				: "");

		if(pUGCProjectDataHeader->project)
			StructCopyString(&pUGCProjectDataHeader->project->strNotes, estrNewNotes);

		estrDestroy(&estrNewNotes);
	}

	if(strUGCProjectDataSaved && strUGCProjectDataSaved[0])
	{
		char *estrNewNotes = NULL;
		UGCProjectDataHeader *pUGCProjectDataHeader = NULL;

		pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved = StructCreate(parse_UGCProjectData);
		ParserReadText(strUGCProjectDataSaved, parse_UGCProjectData, pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved, 0);

		pUGCProjectDataHeader = (UGCProjectDataHeader*)pUGCProjectContainerAndDataForImport->pUGCProjectDataSaved;

		estrPrintf(&estrNewNotes, "Project imported from other shard (%s): %s\n\n%s",
			!nullStr(strPreviousShard) ? strPreviousShard : "UNKNOWN",
			!nullStr(strComment) ? strComment : "NO COMMENT",
			!nullStr(SAFE_MEMBER2(pUGCProjectDataHeader, project, strNotes))
				? SAFE_MEMBER2(pUGCProjectDataHeader, project, strNotes)
				: "");

		if(pUGCProjectDataHeader->project)
			StructCopyString(&pUGCProjectDataHeader->project->strNotes, estrNewNotes);

		estrDestroy(&estrNewNotes);
	}

	RemoteCommand_aslUGCDataManager_SendProjectContainerForImport(
		objCreateManagedReturnVal(UGCImport_ImportUGCProjectContainerAndData_SendProjectContainerForImport_CB, pUGCProjectContainerAndDataForImport),
		GLOBALTYPE_UGCDATAMANAGER, 0,
		pUGCProjectContainerAndDataForImport->pUGCProjectOriginal, strPreviousShard, strComment, forceDelete);

	return NULL;
}

static void UGCImport_ImportUGCProjectSeriesContainer_SendProjectSeriesContainerForImport_CB(TransactionReturnVal *returnVal, UGCProjectSeriesContainerForImport *pUGCProjectSeriesContainerForImport)
{
	UGCProjectSeriesContainerCreateForImportData *pReturn = NULL;
	if(TRANSACTION_OUTCOME_SUCCESS == RemoteCommandCheck_aslUGCDataManager_SendProjectSeriesContainerForImport(returnVal, &pReturn))
	{
		if(pReturn && pReturn->pUGCProjectSeries && (!pReturn->estrError || !pReturn->estrError[0]))
		{
			DoSlowCmdReturn(true, "", pUGCProjectSeriesContainerForImport->slowReturnInfo);
			StructDestroy(parse_UGCProjectSeriesContainerForImport, pUGCProjectSeriesContainerForImport);
		}
		else
		{
			DoSlowCmdReturn(true, (pReturn && pReturn->estrError) ? pReturn->estrError : "UNKNOWN ERROR", pUGCProjectSeriesContainerForImport->slowReturnInfo);
			StructDestroy(parse_UGCProjectSeriesContainerForImport, pUGCProjectSeriesContainerForImport);
		}
	}
	else
	{
		DoSlowCmdReturn(true, "UNKNOWN ERROR", pUGCProjectSeriesContainerForImport->slowReturnInfo);
		StructDestroy(parse_UGCProjectSeriesContainerForImport, pUGCProjectSeriesContainerForImport);
	}
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_ACCESSLEVEL(7) ACMD_IFDEF(GAMESERVER);
char *UGCImport_ImportUGCProjectSeriesContainer(CmdContext *pContext, const char *strUGCProjectSeries, const char *strPreviousShard, const char *strComment, bool forceDelete)
{
	UGCProjectSeriesContainerForImport *pUGCProjectSeriesContainerForImport = StructCreate(parse_UGCProjectSeriesContainerForImport);

	pUGCProjectSeriesContainerForImport->slowReturnInfo = WebRequestSlow_SetupSlowReturn(pContext);

	pUGCProjectSeriesContainerForImport->pUGCProjectSeriesOriginal = StructCreate(parse_UGCProjectSeries);
	ParserReadText(strUGCProjectSeries, parse_UGCProjectSeries, pUGCProjectSeriesContainerForImport->pUGCProjectSeriesOriginal, 0);

	RemoteCommand_aslUGCDataManager_SendProjectSeriesContainerForImport(
		objCreateManagedReturnVal(UGCImport_ImportUGCProjectSeriesContainer_SendProjectSeriesContainerForImport_CB, pUGCProjectSeriesContainerForImport),
		GLOBALTYPE_UGCDATAMANAGER, 0,
		pUGCProjectSeriesContainerForImport->pUGCProjectSeriesOriginal, strPreviousShard, strComment, forceDelete);

	return NULL;
}

#include "gslUGC_ExportImport_WebRequest_c_ast.c"
