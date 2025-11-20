/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "referencesystem.h"

typedef struct Entity Entity;
typedef struct PlayerDiary PlayerDiary;
typedef struct DiaryEntryBucket DiaryEntryBucket;
typedef U32 ContainerID;

AUTO_STRUCT;
typedef struct DiaryBucketHandle
{
	ContainerID bucketID;						AST(KEY)
	REF_TO(DiaryEntryBucket) bucketRef;			AST(COPYDICT(DiaryEntryBucket))
} DiaryBucketHandle;

typedef void (* BucketNotifyCB)(Entity *pEnt, PlayerDiary *diary, DiaryEntryBucket *bucket, void *userData);

void gslDiaryBucketManagerInit(void);
void gslDiary_ReleaseBuckets(Entity *pEnt);
void gslDiary_RequestBucket(Entity *pEnt, U32 bucketID, BucketNotifyCB func, void *userData);
void gslDiary_ValidateBucket(Entity *pEnt, U32 bucketID, BucketNotifyCB func, void *userData);
DiaryEntryBucket *gslDiary_GetBucket(U32 bucketID);
void gslDiary_BucketManagerRunOncePerFrame(void);