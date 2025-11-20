/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;
typedef struct CmdContext CmdContext;
typedef struct TransactionReturnVal TransactionReturnVal;
typedef struct DiaryIndex DiaryIndex;
typedef struct DiaryEntry DiaryEntry;
typedef struct PlayerDiary PlayerDiary;

#define DIARY_MAX_TITLE_LEN 80
#define DIARY_MAX_BODY_LEN 4096

DiaryIndex *DiaryServer_GetIndex(U32 playerID);
CmdSlowReturnForServerMonitorInfo *DiaryServer_SetupSlowReturn(CmdContext *pContext);
void DiaryServer_CancelSlowReturn(CmdContext *pContext);
PlayerDiary *DiaryServer_GetDiaryByPlayerID(U32 playerID);
void DiaryServer_BuildXMLResponseString(char **responseString, ParseTable *tpi, void *struct_mem);
void DiaryServer_BuildXMLResponseStringWithType(char **responseString, char *type, char *val);
DiaryEntry * DiaryServer_GetEntry(U32 playerID, U32 entryID);
