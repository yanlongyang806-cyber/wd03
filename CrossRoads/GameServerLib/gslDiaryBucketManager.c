/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslDiaryBucketManager.h"
#include "DiaryCommon.h"

#include "objSchema.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "ResourceManager.h"
#include "objTransactions.h"

#include "StashTable.h"
#include "earray.h"

#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/gslDiaryBucketManager_h_ast.h"

static StashTable s_BucketHandles = NULL;

typedef struct PendingBucketRequest
{
	ContainerID bucketID;

	EntityRef playerRef;

	BucketNotifyCB func;

	void *userData;
} PendingBucketRequest;

static PendingBucketRequest **s_PendingBucketSubscriptions = NULL;

//
// This function is invoked whenever a bucket container is received by the local
//  game server.  It will notify whoever asked for the bucket and keep track
//  of which buckets have been requested so that the subscriptions can be removed
//  when they are no longer needed.
//
static void
NotifyBucketReceived(PendingBucketRequest *pending, DiaryEntryBucket *bucket)
{
	PlayerDiary *diary = NULL;
	Entity *pEnt = NULL;

	pEnt = entFromEntityRefAnyPartition(pending->playerRef);
	if ( pEnt != NULL )
	{
		diary = GET_REF(pEnt->pSaved->hDiary);
		if ( diary != NULL )
		{
			// remember which buckets the diary is subscribed to
			ea32Push(&diary->subscribedBucketIDs, bucket->bucketID);
		}
	}

	// call user callback to notify that the bucket has arrived
	if ( pending->func != NULL )
	{
		(*pending->func)(pEnt, diary, bucket, pending->userData);
	}
}


void
gslDiaryBucketManagerInit(void)
{
	
	s_BucketHandles = stashTableCreateInt(0);

	// set up schema and copy dictionary for diary entry bucket references
	objRegisterNativeSchema(GLOBALTYPE_DIARYENTRYBUCKET, parse_DiaryEntryBucket, NULL, NULL, NULL, NULL, NULL);

	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_DIARYENTRYBUCKET), false, parse_DiaryEntryBucket, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_DIARYENTRYBUCKET), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_DIARYENTRYBUCKET));
}

//
// Request a local copy of a diary entry bucket, and call the callback when it arrives.
//
void
gslDiary_RequestBucket(Entity *pEnt, U32 bucketID, BucketNotifyCB func, void *userData)
{
	// XXX - do I need to see if we already have the container locally?
	PendingBucketRequest *pending;
	DiaryBucketHandle *bucketHandle = NULL;

	stashIntFindPointer(s_BucketHandles, bucketID, &bucketHandle);

	// only request the bucket if we don't already have a handle
	if ( bucketHandle == NULL )
	{
		char idBuf[128];

		// Create data for pending bucket request.  This data is only needed until the bucket arrives.
		pending = (PendingBucketRequest *)malloc(sizeof(PendingBucketRequest));

		pending->bucketID = bucketID;
		pending->func = func;
		pending->userData = userData;
		pending->playerRef = entGetRef(pEnt);

		eaPush(&s_PendingBucketSubscriptions, pending);

		// set up the handle struct that we keep around to hold the reference
		bucketHandle = StructAlloc(parse_DiaryBucketHandle);
		bucketHandle->bucketID = bucketID;

		// setting the reference will subscribe to the container
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_DIARYENTRYBUCKET), ContainerIDToString(bucketID, idBuf), bucketHandle->bucketRef);

		// save it in the stash table with other active handles
		stashIntAddPointer(s_BucketHandles, bucketID, bucketHandle, false);
	}
}

static void
ReleaseBucket(ContainerID bucketID)
{
	DiaryBucketHandle *bucketHandle;

	// remove the handle from the stash table
	stashIntRemovePointer(s_BucketHandles, bucketID, &bucketHandle);

	if ( bucketHandle != NULL )
	{
		// free the handle, which will also release the reference
		StructDestroy(parse_DiaryBucketHandle, bucketHandle);
	}
}

//
// Release subscriptions for any diary buckets associated with the given player
//
void
gslDiary_ReleaseBuckets(Entity *pEnt)
{
	PlayerDiary *diary;

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		int len = ea32Size(&diary->subscribedBucketIDs);
		int i;

		for ( i = 0; i < len; i++ )
		{
			// unsubscribe to each bucket container
			ReleaseBucket(diary->subscribedBucketIDs[i]);
		}

		// clear the list of subscribed containers
		ea32Clear(&diary->subscribedBucketIDs);
	}
}

DiaryEntryBucket *
gslDiary_GetBucket(U32 bucketID)
{
	DiaryEntryBucket *bucket = NULL;
	DiaryBucketHandle *bucketHandle = NULL;

	stashIntFindPointer(s_BucketHandles, bucketID, &bucketHandle);
	
	if ( bucketHandle != NULL )
	{
		bucket = GET_REF(bucketHandle->bucketRef);
	}

	return bucket;
}

//
// call the provided function after making sure we have a local copy of the requested bucket
//
void
gslDiary_ValidateBucket(Entity *pEnt, U32 bucketID, BucketNotifyCB func, void *userData)
{
	DiaryEntryBucket *bucket = NULL;
	PlayerDiary *diary;

	diary = GET_REF(pEnt->pSaved->hDiary);

	if ( diary != NULL )
	{
		bucket = gslDiary_GetBucket(bucketID);	

		if ( bucket != NULL )
		{
			// bucket already exists locally, so we are done
			(* func)(pEnt, diary, bucket, userData);

			return;
		}
	}

	// Need to request the bucket.  Callback will be called when it arrives.
	gslDiary_RequestBucket(pEnt, bucketID, func, userData);
}

void
gslDiary_BucketManagerRunOncePerFrame(void)
{
	int i = 0;

	while ( i < eaSize(&s_PendingBucketSubscriptions) )
	{
		PendingBucketRequest *pending = s_PendingBucketSubscriptions[i];
		ContainerID bucketID = pending->bucketID;
		DiaryEntryBucket *bucket;

		bucket = gslDiary_GetBucket(bucketID);

		if ( bucket != NULL )
		{
			// bucket has arrived
			NotifyBucketReceived(pending, bucket);

			// remove from the pending list
			eaRemove(&s_PendingBucketSubscriptions, i);

			// free pending info
			free(pending);

			// don't increment i on this path, since we removed an entry from the array
		}
		else
		{
			i++;
		}
	}
}

#include "gslDiaryBucketManager_h_ast.c"