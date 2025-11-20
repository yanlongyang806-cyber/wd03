/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "AppLocale.h"
#include "referencesystem.h"

typedef struct DiaryDisplayHeaders DiaryDisplayHeaders;
typedef struct DiaryDisplayComments DiaryDisplayComments;
typedef struct Entity Entity;
typedef struct CmdContext CmdContext;
typedef U32 ContainerID;
typedef struct CmdSlowReturnForServerMonitorInfo CmdSlowReturnForServerMonitorInfo;

typedef void (*GetEntityCallback)(Entity *pEnt, void *userData);

typedef struct GetEntityCBData
{
	REF_TO(Entity) playerRef;
	void *userData;
	GetEntityCallback userCB;
} GetEntityCBData;

AUTO_STRUCT;
typedef struct GetDisplayCommentsCBData
{
	int lang;
	ContainerID entryID;
	REF_TO(Entity) playerRef;												AST(COPYDICT(EntityPlayer))
	OPTIONAL_STRUCT(CmdSlowReturnForServerMonitorInfo) slowReturnInfo;		NO_AST
} GetDisplayCommentsCBData;

AUTO_STRUCT;
typedef struct WebAddEntryCBData
{
	STRING_MODIFIABLE title;
	STRING_MODIFIABLE text;
	REF_TO(Entity) playerRef;												AST(COPYDICT(EntityPlayer))
	OPTIONAL_STRUCT(CmdSlowReturnForServerMonitorInfo) slowReturnInfo;		NO_AST
} WebAddEntryCBData;

AUTO_STRUCT;
typedef struct WebAddCommentCBData
{
	STRING_MODIFIABLE title;
	STRING_MODIFIABLE text;
	U32 entryID;
	REF_TO(Entity) playerRef;												AST(COPYDICT(EntityPlayer))
	OPTIONAL_STRUCT(CmdSlowReturnForServerMonitorInfo) slowReturnInfo;		NO_AST
} WebAddCommentCBData;

DiaryDisplayHeaders *gslDiaryWebRequest_GetDisplayHeaders(Entity *pEnt, int lang, int first, int count);
DiaryDisplayComments *gslDiaryWebRequest_GetDisplayComments(CmdContext *cmdContext, const char *playerHandle, int lang, U32 entryID);
bool gslDiaryWebRequest_AddBlogEntry(CmdContext *cmdContext, const char *playerHandle, const char *title, const char *text);
bool gslDiaryWebRequest_AddComment(CmdContext *cmdContext, const char *playerHandle, U32 entryID, const char *title, const char *text);
void gslDiaryWebRequest_RunOncePerFrame(void);