/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryServer.h"
#include "DiaryCommon.h"
#include "objTransactions.h"

#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/AppServerLib_autotransactions_autogen_wrappers.h"
#include "autogen/AppServerLib_autogen_slowfuncs.h"

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME(DiaryServer_GetIndex) ACMD_ACCESSLEVEL(9);
DiaryIndex *
DiaryServer_GetIndexXMLRPC(U32 playerID)
{
	return DiaryServer_GetIndex(playerID);
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
DiaryIndex *
DiaryServer_GetIndexCmd(U32 playerID)
{
	printf("GetIndex command\n");
	return DiaryServer_GetIndex(playerID);
}

static void 
AddBlogEntryXMLRPC_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;
	DiaryServer_BuildXMLResponseStringWithType(&pFullRetString, "boolean", ( pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) ? "1" : "0");

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME(DiaryServer_AddBlogEntry) ACMD_ACCESSLEVEL(9);
bool
DiaryServer_AddBlogEntryXMLRPC(CmdContext *pContext, U32 playerID, const char *title, const char *text)
{
	PlayerDiary *diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary != NULL )
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(AddBlogEntryXMLRPC_CB, DiaryServer_SetupSlowReturn(pContext));
		AutoTrans_DiaryServer_tr_AddBlogEntry(pReturn, GetAppGlobalType(), GLOBALTYPE_PLAYERDIARY, diary->containerID, title, text);
	}

	// return value only matters if we don't do the slow return
	return false;
}

static void 
AddBlogEntryCmd_CB(TransactionReturnVal *pReturnVal, SlowRemoteCommandID *pCmdID)
{
	char *pFullRetString = NULL;
	
	SlowRemoteCommandReturn_DiaryServer_AddBlogEntryCmd(*pCmdID, pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);

	printf("AddBlogEntry: after transaction\n");
	free(pCmdID);
}

AUTO_COMMAND_REMOTE_SLOW(int) ACMD_IFDEF(GAMESERVER);
void
DiaryServer_AddBlogEntryCmd(U32 playerID, const char *title, const char *text, SlowRemoteCommandID cmdID)
{
	PlayerDiary *diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary != NULL )
	{
		SlowRemoteCommandID *pCmdID;
		TransactionReturnVal *pReturn;

		pCmdID = (int *)malloc(sizeof(SlowRemoteCommandID));
		*pCmdID = cmdID;

		pReturn = objCreateManagedReturnVal(AddBlogEntryCmd_CB, pCmdID );

		printf("AddBlogEntry: before transaction\n");

		AutoTrans_DiaryServer_tr_AddBlogEntry(pReturn, GetAppGlobalType(), GLOBALTYPE_PLAYERDIARY, diary->containerID, title, text);
	}
	else
	{
		SlowRemoteCommandReturn_DiaryServer_AddBlogEntryCmd(cmdID, false);
	}
}

static void 
AddCommentXMLRPC_CB(TransactionReturnVal *pReturnVal, CmdSlowReturnForServerMonitorInfo *pSlowReturnInfo)
{
	char *pFullRetString = NULL;
	DiaryServer_BuildXMLResponseStringWithType(&pFullRetString, "boolean", ( pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) ? "1" : "0");

	DoSlowCmdReturn(pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS, pFullRetString, pSlowReturnInfo);
}

AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME(DiaryServer_AddComment) ACMD_ACCESSLEVEL(9);
bool
DiaryServer_AddCommentXMLRPC(CmdContext *pContext, U32 playerID, U32 entryID, const char *title, const char *text)
{
	PlayerDiary *diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary != NULL )
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(AddCommentXMLRPC_CB, DiaryServer_SetupSlowReturn(pContext));
		AutoTrans_DiaryServer_tr_AddComment(pReturn, GetAppGlobalType(), GLOBALTYPE_PLAYERDIARY, diary->containerID, entryID, title, text);
	}

	// return value only matters if we don't do the slow return
	return false;
}

static void 
AddCommentCmd_CB(TransactionReturnVal *pReturnVal, SlowRemoteCommandID *pCmdID)
{
	char *pFullRetString = NULL;

	SlowRemoteCommandReturn_DiaryServer_AddCommentCmd(*pCmdID, pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);

	printf("AddComment: after transaction\n");
	free(pCmdID);
}

AUTO_COMMAND_REMOTE_SLOW(int) ACMD_IFDEF(GAMESERVER);
void
DiaryServer_AddCommentCmd(U32 playerID, U32 entryID, const char *title, const char *text, SlowRemoteCommandID cmdID)
{
	PlayerDiary *diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary != NULL )
	{
		SlowRemoteCommandID *pCmdID;
		TransactionReturnVal *pReturn;

		pCmdID = (int *)malloc(sizeof(SlowRemoteCommandID));
		*pCmdID = cmdID;

		pReturn = objCreateManagedReturnVal(AddCommentCmd_CB, pCmdID );

		printf("AddComment: before transaction\n");

		AutoTrans_DiaryServer_tr_AddComment(pReturn, GetAppGlobalType(), GLOBALTYPE_PLAYERDIARY, diary->containerID, entryID, title, text);
	}
	else
	{
		SlowRemoteCommandReturn_DiaryServer_AddCommentCmd(cmdID, false);
	}
}


AUTO_COMMAND ACMD_CATEGORY(XMLRPC) ACMD_NAME(DiaryServer_GetEntry) ACMD_ACCESSLEVEL(9);
DiaryEntry *
DiaryServer_GetEntryXMLRPC(U32 playerID, U32 entryID)
{
	DiaryEntry *entry;
	DiaryEntry *entryCopy = NULL;

	entry = DiaryServer_GetEntry(playerID, entryID);
	if ( entry != NULL )
	{
		entryCopy = StructClone(parse_DiaryEntry, entry);
	}

	return entryCopy;
}

AUTO_COMMAND_REMOTE ACMD_IFDEF(GAMESERVER);
DiaryEntry *
DiaryServer_GetEntryCmd(U32 playerID, U32 entryID)
{
	DiaryEntry *entry;
	DiaryEntry *entryCopy = NULL;

	entry = DiaryServer_GetEntry(playerID, entryID);
	if ( entry != NULL )
	{
		entryCopy = StructClone(parse_DiaryEntry, entry);
	}

	return entryCopy;
}


static void 
AddMissionEntryCmd_CB(TransactionReturnVal *pReturnVal, SlowRemoteCommandID *pCmdID)
{
	char *pFullRetString = NULL;

	SlowRemoteCommandReturn_DiaryServer_AddMissionEntryCmd(*pCmdID, pReturnVal->eOutcome == TRANSACTION_OUTCOME_SUCCESS);

	printf("AddMissionEntry: after transaction\n");
	free(pCmdID);
}

AUTO_COMMAND_REMOTE_SLOW(int) ACMD_IFDEF(GAMESERVER);
void
DiaryServer_AddMissionEntryCmd(U32 playerID, const char *missionRef, SlowRemoteCommandID cmdID)
{
	PlayerDiary *diary = DiaryServer_GetDiaryByPlayerID(playerID);
	if ( diary != NULL )
	{
		SlowRemoteCommandID *pCmdID;
		TransactionReturnVal *pReturn;

		pCmdID = (int *)malloc(sizeof(SlowRemoteCommandID));
		*pCmdID = cmdID;

		pReturn = objCreateManagedReturnVal(AddMissionEntryCmd_CB, pCmdID );

		printf("AddMissionEntry: before transaction\n");

		AutoTrans_DiaryServer_tr_AddMissionEntry(pReturn, GetAppGlobalType(), GLOBALTYPE_PLAYERDIARY, diary->containerID, missionRef);
	}
	else
	{
		SlowRemoteCommandReturn_DiaryServer_AddMissionEntryCmd(cmdID, false);
	}
}