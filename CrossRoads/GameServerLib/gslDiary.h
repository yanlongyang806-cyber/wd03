/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

typedef struct Entity Entity;
typedef struct DiaryFilter DiaryFilter;

typedef void (*AddEntryCallback)(bool success, Entity *pEnt, void *userData);
typedef void (*UpdateEntryCallback)(bool success, Entity *pEnt, U32 entryID, void *userData);

void gslDiary_SchemaInit(void);
void gslDiary_SendHeadersToClient(Entity *pEnt);
void gslDiary_SendTagStringsToClient(Entity *pEnt);
void gslDiary_AddBlogEntry(Entity *pEnt, const char *title, const char *text, AddEntryCallback userCB, void *userData);
void gslDiary_AddMissionEntry(Entity *pEnt, const char *missionNameIn, const char *commentTitle, const char *commentText, U64 tagBits, AddEntryCallback userCB, void *userData);
void gslDiary_AddActivityEntry(Entity *pEnt, U32 activityEntryID, const char *commentTitle, const char *commentText, U64 tagBits, AddEntryCallback userCB, void *userData);
void gslDiary_RequestEntry(Entity *pEnt, U32 entryID);
void gslDiary_RemoveEntry(Entity *pEnt, U32 entryID);
void gslDiary_AddComment(Entity *pEnt, U32 entryID, const char *title, const char *text, UpdateEntryCallback userCB, void *userData);
void gslDiary_RemoveComment(Entity *pEnt, U32 entryID, U32 commentID, UpdateEntryCallback userCB, void *userData);
void gslDiary_EditComment(Entity *pEnt, U32 entryID, U32 commentID, const char *title, const char *body, UpdateEntryCallback userCB, void *userData);
void gslDiary_AddTagString(Entity *pEnt, const char *tagString);
void gslDiary_RemoveTagString(Entity *pEnt, const char *tagString);
void gslDiary_SetEntryTags(Entity *pEnt, U32 entryID, U64 tagBits);
void gslDiary_SendDefaultFilterToClient(Entity *pEnt);
void gslDiary_SetDefaultFilter(Entity *pEnt, DiaryFilter *protoFilter);
void gslDiary_RunOncePerFrame(void);
void gslDiary_DumpDependentContainers(Entity *pEnt);

void gslDiary_AddBlogEntryUpdateClient(Entity *pEnt, const char *title, const char *text);
void gslDiary_AddMissionEntryUpdateClient(Entity *pEnt, const char *missionNameIn, const char *commentTitle, const char *commentText, U64 tagBits);
void gslDiary_AddActivityEntryUpdateClient(Entity *pEnt, U32 activityEntryID, const char *commentTitle, const char *commentText, U64 tagBits);
void gslDiary_AddCommentUpdateClient(Entity *pEnt, U32 entryID, const char *title, const char *text);
void gslDiary_RemoveCommentUpdateClient(Entity *pEnt, U32 entryID, U32 commentID);
void gslDiary_EditCommentUpdateClient(Entity *pEnt, U32 entryID, U32 commentID, const char *title, const char *body);