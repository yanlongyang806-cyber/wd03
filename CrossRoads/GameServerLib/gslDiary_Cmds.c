/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryCommon.h"
#include "gslDiary.h"
#include "DiaryDisplay.h"
#include "EString.h"
#include "gslDiaryWebRequest.h"
#include "cmdparse.h"
#include "EntityLib.h"

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_RequestHeaders(Entity *pEnt)
{
	gslDiary_SendHeadersToClient(pEnt);
	gslDiary_SendTagStringsToClient(pEnt);
	gslDiary_SendDefaultFilterToClient(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_AddBlogEntry) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_AddBlogEntryCmd(Entity *pEnt, const char *title, const char *text)
{
	gslDiary_AddBlogEntryUpdateClient(pEnt, title, text);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_AddMissionEntry) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_AddMissionEntryCmd(Entity *pEnt, const char *missionName, const char *title, const char *text, U64 tagBits)
{
	gslDiary_AddMissionEntryUpdateClient(pEnt, missionName, title, text, tagBits);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_AddActivityEntry) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_AddActivityEntryCmd(Entity *pEnt, U32 activityEntryID, const char *title, const char *text, U64 tagBits)
{
	gslDiary_AddActivityEntryUpdateClient(pEnt, activityEntryID, title, text, tagBits);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_RemoveEntry) ACMD_SERVERCMD ACMD_CATEGORY(Diary) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void 
gslDiary_RemoveEntryCmd(Entity *pEnt, U32 entryID)
{
	gslDiary_RemoveEntry(pEnt, entryID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_AddComment) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_AddCommentCmd(Entity *pEnt, U32 entryID, const char *title, const char *text)
{
	gslDiary_AddCommentUpdateClient(pEnt, entryID, title, text);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_RemoveComment) ACMD_SERVERCMD ACMD_CATEGORY(Diary) ACMD_HIDE ACMD_PRODUCTS(StarTrek);
void 
gslDiary_RemoveCommentCmd(Entity *pEnt, U32 entryID, U32 commentID)
{
	gslDiary_RemoveCommentUpdateClient(pEnt, entryID, commentID);
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_EditComment) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_EditCommentCmd(Entity *pEnt, U32 entryID, U32 commentID, const char *title, const char *text)
{
	gslDiary_EditCommentUpdateClient(pEnt, entryID, commentID, title, text);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_RequestEntry) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_RequestEntryCmd(Entity *pEnt, U32 entryID)
{
	gslDiary_RequestEntry(pEnt, entryID);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_AddTagString) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_AddTagStringCmd(Entity *pEnt, const char *tagString)
{
	gslDiary_AddTagString(pEnt, tagString);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_RemoveTagString) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_RemoveTagStringCmd(Entity *pEnt, const char *tagString)
{
	gslDiary_RemoveTagString(pEnt, tagString);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_SetEntryTags) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_SetEntryTagsCmd(Entity *pEnt, U32 entryID, U64 tagBits)
{
	gslDiary_SetEntryTags(pEnt, entryID, tagBits);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(gslDiary_SetDefaultFilter) ACMD_PRIVATE ACMD_SERVERCMD ACMD_CATEGORY(Diary);
void 
gslDiary_SetDefaultFilterCmd(Entity *pEnt, DiaryFilter *protoFilter)
{
	gslDiary_SetDefaultFilter(pEnt, protoFilter);
}

AUTO_COMMAND ACMD_NAME(gslDiaryWebRequest_GetDisplayHeaders) ACMD_CATEGORY(XMLRPC);
DiaryDisplayHeaders *
gslDiaryWebRequest_GetDisplayHeadersCmd(Entity *pEnt, int lang, int first, int count)
{
	return gslDiaryWebRequest_GetDisplayHeaders(pEnt, lang, first, count);
}

AUTO_COMMAND ACMD_NAME(gslDiaryWebRequest_GetDisplayComments) ACMD_CATEGORY(XMLRPC);
DiaryDisplayComments *
gslDiaryWebRequest_GetDisplayCommentsCmd(CmdContext *cmdContext, const char *playerHandle, int lang, U32 entryID)
{
	return gslDiaryWebRequest_GetDisplayComments(cmdContext, playerHandle, lang, entryID);
}

AUTO_COMMAND ACMD_NAME(gslDiaryWebRequest_AddBlogEntry) ACMD_CATEGORY(XMLRPC);
bool
gslDiaryWebRequest_AddBlogEntryCmd(CmdContext *cmdContext, const char *playerHandle, const char *title, const char *text)
{
	return gslDiaryWebRequest_AddBlogEntry(cmdContext, playerHandle, title, text);
}

AUTO_COMMAND ACMD_NAME(gslDiaryWebRequest_AddComment) ACMD_CATEGORY(XMLRPC);
bool
gslDiaryWebRequest_AddCommentCmd(CmdContext *cmdContext, const char *playerHandle, U32 entryID, const char *title, const char *text)
{
	return gslDiaryWebRequest_AddComment(cmdContext, playerHandle, entryID, title, text);
}

AUTO_COMMAND_REMOTE ACMD_NAME(gslDiary_SendHeadersToClient);
void
gslDiary_SendHeadersToClientCmd(ContainerID entityID)
{
	Entity *pEnt;

	pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entityID);
	if ( pEnt != NULL )
	{
		gslDiary_SendHeadersToClient(pEnt);
	}
}