/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslDiary.h"
#include "DiaryCommon.h"
#include "gslDiaryBucketManager.h"
#include "Entity.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "stdtypes.h"
#include "StringCache.h"
#include "ActivityLogCommon.h"
#include "gslDiaryWebRequest.h"

#include "objTransactions.h"
#include "mission_common.h"
#include "objSchema.h"
#include "ResourceManager.h"

#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/DiaryEnums_h_ast.h"

#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"
#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameServerLib_autotransactions_autogen_wrappers.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"

//
// This is the number of entries we want per bucket.  It is a soft limit, so
//  we don't jump through hoops to ensure that each bucket has exactly this many
//  entries.  By not enforcing a hard limit, we avoid the difficulties caused
//  by the fact that container creation and transactions are asynchronous.
// For example, if the bucket is full, we don't have to queue up any
//  new entries until the new bucket is ready.  We can just add them
//  to the current bucket, and the new bucket will take over when its
//  finalize transaction is complete.
// There is no assumption when reading entries that there will be any
//  particular number per bucket. So it is safe to change this number
//  at any time.  It will affect current and future buckets, but existing
//  buckets that are smaller or larger than the limit will continue
//  to work fine for all operations.
//
#define SOFT_MAX_ENTRIES_PER_DIARY_BUCKET 20

//
// Initialize diary schema and set up copy dictionary for diary containers
//
void
gslDiary_SchemaInit(void)
{
	// set up schema and copy dictionary for diary container references
	objRegisterNativeSchema(GLOBALTYPE_PLAYERDIARY, parse_PlayerDiary, NULL, NULL, NULL, NULL, NULL);

	RefSystem_RegisterSelfDefiningDictionary(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), false, parse_PlayerDiary, false, false, NULL);
	resDictRequestMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), RES_DICT_KEEP_NONE, false, objCopyDictHandleRequest);
	resDictProvideMissingResources(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY));

	gslDiaryBucketManagerInit();
}

// callback prototype used when creating containers for the diary system
typedef void (*DiaryCreationCallback)(bool success, U32 playerID, U32 diaryContainerID, U32 bucketContainerID, void *userData);

// callback data used when creating containers for the diary system
typedef struct DiaryCreationCBData
{
	DiaryCreationCallback func;
	void *userData;
	ContainerID playerID;
	ContainerID diaryID;
	ContainerID bucketID;
	bool failure;
	REF_TO(PlayerDiary) diaryRef; 
} DiaryCreationCBData;

static void
NewDiaryFinalize_CB(TransactionReturnVal *pReturn, DiaryCreationCBData *cbData)
{
	// call the callback if it exists
	if ( cbData->func != NULL )
	{
		if ( ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) )
		{
			(* cbData->func)(true, cbData->playerID, cbData->diaryID, cbData->bucketID, cbData->userData);
		}
		else
		{
			(* cbData->func)(false, 0, 0, 0, cbData->userData);
			printf("new diary finalize failed\n");
		}
	}

	// should be safe to remove the handle at this point
	REMOVE_HANDLE(cbData->diaryRef);

	free(cbData);
}

static void
CreateInitialBucketContainer_CB(TransactionReturnVal *pReturn, DiaryCreationCBData *cbData)
{
	if ( ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS ) )
	{
		char idBuf[128];
		TransactionReturnVal *pNewReturn;

		// grab new diary entry container ID from the transaction return value
		cbData->bucketID = atoi(pReturn->pBaseReturnVals[0].returnString);

		pNewReturn = objCreateManagedReturnVal(NewDiaryFinalize_CB, cbData);

		// hack to keep the reference system from unsubscribing from the diary container too soon, leaving
		//  a local copy without a subscription to keep it up to date.

		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), ContainerIDToString(cbData->diaryID, idBuf), cbData->diaryRef);

		// call the transaction that initializes the references to the containers
		AutoTrans_gslDiary_tr_NewDiaryFinalize(pNewReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_ENTITYPLAYER, cbData->playerID, GLOBALTYPE_PLAYERDIARY, cbData->diaryID, cbData->bucketID);

	}
	else
	{
		if ( cbData->func != NULL )
		{
			(* cbData->func)(false, 0, 0, 0, cbData->userData);
		}
		free(cbData);

		printf("create initial bucket fail\n");
	}
}

static void
CreateDiaryContainer_CB(TransactionReturnVal *pReturn, DiaryCreationCBData *cbData)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		NOCONST(DiaryEntryBucket) *bucket;
		TransactionReturnVal *pNewReturn;

		// save diary container id
		cbData->diaryID = atoi(pReturn->pBaseReturnVals[0].returnString);

		// Create prototype bucket object
		bucket = StructCreateNoConst(parse_DiaryEntryBucket);

		pNewReturn = objCreateManagedReturnVal(CreateInitialBucketContainer_CB, cbData);

		// request creation of bucket object
		objRequestContainerCreate(pNewReturn, GLOBALTYPE_DIARYENTRYBUCKET, bucket, GLOBALTYPE_OBJECTDB, 0);

		StructDestroyNoConst(parse_DiaryEntryBucket, bucket);
	}
	else
	{
		// if we failed, just call the callback
		if ( cbData->func != NULL )
		{
			(* cbData->func)(false, 0, 0, 0, cbData->userData);
		}
		free(cbData);
		printf("create diary container fail\n");
	}
}

//
// Initialize a new player diary
// This is a multi-step process which includes the following:
//  1) create diary container
//  2) create initial entry bucket container 
//  3) once both are done, execute a transaction that both points the diary to the bucket
//     and initializes the reference from the player to the diary
//
static void
CreatePlayerDiary(Entity *pEnt, DiaryCreationCallback func, void *userData)
{
	NOCONST(PlayerDiary) *diary;

	DiaryCreationCBData *cbData;
	TransactionReturnVal *pReturnDiary;

	// Create prototype diary object
	// The currentBucketID field will be initialized by the transaction invoked
	//  by the callbacks when the diary and bucket containers have been created.
	diary = StructCreateNoConst(parse_PlayerDiary);

	diary->entityID = pEnt->myContainerID;
	diary->nextCommentID = 1;
	diary->nextEntryID = 1;
	diary->currentBucketCount = 0;

	cbData = (DiaryCreationCBData *)malloc(sizeof(DiaryCreationCBData));
	cbData->playerID = pEnt->myContainerID;
	cbData->func = func;
	cbData->userData = userData;
	cbData->diaryID = 0;
	cbData->bucketID = 0;
	cbData->failure = false;

	pReturnDiary = objCreateManagedReturnVal(CreateDiaryContainer_CB, cbData);
	objRequestContainerCreate(pReturnDiary, GLOBALTYPE_PLAYERDIARY, diary, GLOBALTYPE_OBJECTDB, 0);

	StructDestroyNoConst(parse_PlayerDiary, diary);
}

static void
CreateNewBucketFinalize_CB(TransactionReturnVal *pReturn, void *cbData)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_FAILURE )
	{
		printf("Failed new bucket finalize transaction\n");
	}
}

static void
CreateNewBucket_CB(TransactionReturnVal *pReturn, void *cbData)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)cbData));

		if ( pEnt != NULL )
		{
			PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);
			if ( diary != NULL )
			{
				U32 bucketID = atoi(pReturn->pBaseReturnVals[0].returnString);
				TransactionReturnVal *pNewReturn = objCreateManagedReturnVal(CreateNewBucketFinalize_CB, NULL);

				AutoTrans_gslDiary_tr_NewBucketFinalize(pNewReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, bucketID);

				return;
			}
		}
		printf("Couldn't find player or diary to finalize new bucket container\n");
	}
	else
	{
		printf("Failed to create new bucket container\n");
	}
}

static void
CreateNewBucket(Entity *pEnt)
{
	NOCONST(DiaryEntryBucket) *bucket;
	TransactionReturnVal *pReturn;
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);

	// Create prototype bucket object
	bucket = StructCreateNoConst(parse_DiaryEntryBucket);

	pReturn = objCreateManagedReturnVal(CreateNewBucket_CB, (void *)((intptr_t)entGetRef(pEnt)));

	// request creation of bucket object
	objRequestContainerCreate(pReturn, GLOBALTYPE_DIARYENTRYBUCKET, bucket, GLOBALTYPE_OBJECTDB, 0);

	StructDestroyNoConst(parse_DiaryEntryBucket, bucket);
}

static void
CheckIfNeedNewBucket(Entity *pEnt)
{
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);
	if ( ( diary != NULL ) && ( diary->currentBucketCount >= SOFT_MAX_ENTRIES_PER_DIARY_BUCKET ) )
	{
		// If we have passed the soft limit on the current bucket, then create a new
		//  bucket.
		CreateNewBucket(pEnt);
	}
}

void
gslDiary_SendTagStringsToClient(Entity *pEnt)
{
	DiaryTagStrings *tagStrings;
	PlayerDiary *diary;
	const char * const *srcStrings = NULL;

	diary = GET_REF(pEnt->pSaved->hDiary);

	if ( ( diary != NULL ) && ( diary->tags != NULL ) )
	{
		int i;
		int n;
		NOCONST(DiaryTagString) *tagString;
		DiaryTagString *srcTag;

		tagStrings = StructCreate(parse_DiaryTagStrings);
		
		n = eaSize(&diary->tags);

		for ( i = 0; i < n; i++)
		{
			srcTag = diary->tags[i];
			tagString = StructCreateNoConst(parse_DiaryTagString);
			tagString->permanent = srcTag->permanent;
			tagString->tagName = srcTag->tagName;

			eaPush(&tagStrings->tagStrings, (DiaryTagString *)tagString);
		}
	}
	else
	{
		// no player tags, so send the default ones instead
		tagStrings = DiaryConfig_CopyDefaultTagStrings();	
	}

	ClientCmd_gclDiary_SetTagStrings(pEnt, tagStrings);

	StructDestroy(parse_DiaryTagStrings, tagStrings);

}

void
gslDiary_SendHeadersToClient(Entity *pEnt)
{
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);
	DiaryHeaders *headers;
	static DiaryHeaders *emptyHeaders = NULL;

	if ( diary == NULL )
	{
		if ( emptyHeaders == NULL )
		{
			emptyHeaders = StructCreate(parse_DiaryHeaders);
		}
		// send empty headers if we don't have a diary
		headers = emptyHeaders;
	}
	else
	{
		headers = &diary->headers;
	}
	// send index to client
	ClientCmd_gclDiary_SetHeaders(pEnt, headers);
}

//
// Create a prototype comment.  Returns NULL if both strings are blank.
//
static NOCONST(DiaryComment) *
CreateProtoComment(const char *title, const char *text, U32 entityID, U32 time)
{
	NOCONST(DiaryComment) *protoComment = NULL;

	// don't create the comment if both strings are empty
	if ( ( ( title != NULL ) && ( title[0] != '\0' ) ) || ( ( text != NULL ) && ( text[0] != '\0' ) ) )
	{
		protoComment = StructCreateNoConst(parse_DiaryComment);

		protoComment->time = time;
		protoComment->entityID = entityID;

		if ( title != NULL )
		{
			protoComment->title = StructAllocString(title);
		}

		if ( text != NULL )
		{
			protoComment->text = StructAllocString(text);		
		}
	}

	return protoComment;
}

typedef struct AddEntryCBData
{
	NOCONST(DiaryHeader) *protoHeader;
	NOCONST(DiaryComment) *protoComment;
	// use a reference here rather than an EntityRef, because when called on the Web Request server
	//  the EntityRef doesn't work. 
	REF_TO(Entity) playerRef;
	U32 playerID;
	void *userData;
	AddEntryCallback userCB;
} AddEntryCBData;

static AddEntryCBData *
CreateAddEntryCBData(Entity *pEnt, NOCONST(DiaryHeader) *protoHeader, NOCONST(DiaryComment) *protoComment, AddEntryCallback userCB, void *userData)
{
	char idBuf[128];
	AddEntryCBData *cbData = (AddEntryCBData *)malloc(sizeof(AddEntryCBData));
	cbData->protoHeader = protoHeader;
	cbData->protoComment = protoComment;
	cbData->userData = userData;
	cbData->userCB = userCB;
	cbData->playerID = pEnt->myContainerID;

	// setting the reference will subscribe to the container
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), cbData->playerRef);

	return cbData;
}

static void
FreeAddEntryCBData(AddEntryCBData *cbData)
{
	if ( cbData->protoHeader != NULL )
	{
		StructDestroyNoConst(parse_DiaryHeader, cbData->protoHeader);
	}

	if ( cbData->protoComment != NULL )
	{
		StructDestroyNoConst(parse_DiaryComment, cbData->protoComment);
	}

	REMOVE_HANDLE(cbData->playerRef);

	free(cbData);
}

static void
AddEntryNotifyAndFree(bool success, AddEntryCBData *cbData)
{
	if ( cbData->userCB != NULL )
	{
		// minor hack here to make sure we use the live entity if it exists, otherwise use the reference system copy.
		// necessary to work on both a game server with real logged on character and web request server.
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, cbData->playerID);
		if ( pEnt == NULL )
		{
			pEnt = GET_REF(cbData->playerRef);
		}

		(*cbData->userCB)(success, pEnt, cbData->userData);
	}

	FreeAddEntryCBData(cbData);
}

static void
SendHeadersToClientCB(bool success, Entity *pEnt, void *userData)
{
	if ( pEnt != NULL )
	{
		gslDiary_SendHeadersToClient(pEnt);
	}
}

static void 
AddEntry_CB(TransactionReturnVal *pReturn, AddEntryCBData *cbData) 
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		// minor hack here to make sure we use the live entity if it exists, otherwise use the reference system copy.
		// necessary to work on both a game server with real logged on character and web request server.
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, cbData->playerID);
		if ( pEnt == NULL )
		{
			pEnt = GET_REF(cbData->playerRef);
		}

		// make a new bucket if needed
		if ( pEnt != NULL )
		{
			CheckIfNeedNewBucket(pEnt);
		}
	}

	AddEntryNotifyAndFree(pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS, cbData);
}

//
// Callback to handle adding of an entry after diary creation.
//
static void
DelayedAddEntry(bool success, U32 playerID, U32 diaryContainerID, U32 bucketContainerID, AddEntryCBData *cbData)
{
	if ( success )
	{
		if ( ( diaryContainerID == 0 ) || ( bucketContainerID == 0 ) )
		{
			ErrorDetailsf("playerID = %d, diaryContainerID = %d, bucketContainerID = %d", playerID, diaryContainerID, bucketContainerID);
			Errorf("DelayedAddEntry: Invalid diary or bucket container ID");
		}
		else
		{
			TransactionReturnVal *pReturn = objCreateManagedReturnVal(AddEntry_CB, cbData);
			AutoTrans_gslDiary_tr_AddEntry(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diaryContainerID, GLOBALTYPE_DIARYENTRYBUCKET, bucketContainerID, (DiaryHeader *)cbData->protoHeader, (DiaryComment *)cbData->protoComment);
		}
	}
}

void 
gslDiary_AddEntry(AddEntryCBData *cbData)
{
	PlayerDiary *diary;
	Entity *pEnt;

	pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, cbData->playerID);
	if ( pEnt == NULL )
	{
		pEnt = GET_REF(cbData->playerRef);
	}

	if ( pEnt != NULL )
	{
		diary = GET_REF(pEnt->pSaved->hDiary);
		// Since we only create the diary container for a player when it is actually needed,
		//	we need to be able to handle the case where we are adding an entry and the diary
		//	doesn't yet exist.
		if ( diary == NULL )
		{
			// Create the diary container first, and then add the entry
			CreatePlayerDiary(pEnt, DelayedAddEntry, cbData);
		}
		else
		{
			if ( ( diary->containerID == 0 ) || ( diary->currentBucketID == 0 ) )
			{
				ErrorDetailsf("playerID = %d, diaryContainerID = %d, bucketContainerID = %d", pEnt->myContainerID, diary->containerID, diary->currentBucketID);
				Errorf("DelayedAddEntry: Invalid diary or bucket container ID");
				AddEntryNotifyAndFree(false, cbData);
			}
			else
			{
				TransactionReturnVal *pReturn = objCreateManagedReturnVal(AddEntry_CB, cbData);
				AutoTrans_gslDiary_tr_AddEntry(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, GLOBALTYPE_DIARYENTRYBUCKET, diary->currentBucketID, (DiaryHeader *)cbData->protoHeader, (DiaryComment *)cbData->protoComment);
			}
		}
	}
	else
	{
		AddEntryNotifyAndFree(false, cbData);
	}
}

void 
gslDiary_AddBlogEntry(Entity *pEnt, const char *title, const char *text, AddEntryCallback userCB, void *userData)
{
	NOCONST(DiaryHeader) *protoHeader;
	NOCONST(DiaryComment) *protoComment;
	AddEntryCBData *cbData;

	static char *s_tmpTitle = NULL;
	static char *s_tmpText = NULL;

	// Don't add an entry with an empty title.  Client UI should prevent this from ever happening.
	if ( ( title == NULL ) || ( title[0] == '\0' ) )
	{
		return;
	}
	estrClear(&s_tmpTitle);
	estrClear(&s_tmpText);
	
	estrAppendEscaped(&s_tmpTitle, title);
	estrAppendEscaped(&s_tmpText, text);

	// create prototype header and comment
	protoHeader = StructCreateNoConst(parse_DiaryHeader);

	protoHeader->hidden = false;
	protoHeader->time = timeSecondsSince2000();
	protoHeader->title = StructAllocString(s_tmpTitle);
	protoHeader->type = DiaryEntryType_Blog;

	protoComment = CreateProtoComment(s_tmpTitle, s_tmpText, pEnt->myContainerID, protoHeader->time);

	cbData = CreateAddEntryCBData(pEnt, protoHeader, protoComment, userCB, userData);

	gslDiary_AddEntry(cbData);
}

void
gslDiary_AddBlogEntryUpdateClient(Entity *pEnt, const char *title, const char *text)
{
	gslDiary_AddBlogEntry(pEnt, title, text, SendHeadersToClientCB, NULL);
}

void
gslDiary_AddMissionEntry(Entity *pEnt, const char *missionNameIn, const char *commentTitle, const char *commentText, U64 tagBits, AddEntryCallback userCB, void *userData)
{
	NOCONST(DiaryHeader) *protoHeader;
	NOCONST(DiaryComment) *protoComment;
	MissionInfo *missionInfo;
	Mission *mission;
	MissionDef *missionDef;
	U32 time;
	const char *missionName = allocAddString(missionNameIn);
	AddEntryCBData *cbData;

	static char *s_tmpTitle = NULL;
	static char *s_tmpText = NULL;

	estrClear(&s_tmpTitle);
	estrClear(&s_tmpText);

	estrAppendEscaped(&s_tmpTitle, commentTitle);
	estrAppendEscaped(&s_tmpText, commentText);

	if (!missionName)
	{
		devassertmsg(0, "Cannot add a mission entry to the diary without a mission name!");
		return;
	}

	// get time and missionDef for the named mission from the player
	missionInfo = mission_GetInfoFromPlayer(pEnt);
	mission = mission_GetMissionByName(missionInfo, missionName);
	if ( mission != NULL )
	{
		time = mission->startTime;
		missionDef = mission_GetDef(mission);
	}
	else
	{
		CompletedMission *completedMission = mission_GetCompletedMissionByName(missionInfo, missionName);
		if ( completedMission == NULL )
		{
			// couldn't find mission, so just exit without adding the entry
			return;
		}
		else
		{
			time = completedMission->completedTime;
			missionDef = GET_REF(completedMission->def);
		}
	}

	if ( missionDef == NULL )
	{
		return;
	}

	// create prototype header and comment
	protoHeader = StructCreateNoConst(parse_DiaryHeader);

	protoHeader->hidden = false;
	protoHeader->time = time;
	protoHeader->title = NULL;
	protoHeader->refName = missionName;
	protoHeader->tagBits = tagBits;
	if ( missionDef->missionType == MissionType_Perk )
	{
		protoHeader->type = DiaryEntryType_Perk;
	}
	else
	{
		protoHeader->type = DiaryEntryType_Mission;
	}

	protoComment = CreateProtoComment(s_tmpTitle, s_tmpText, pEnt->myContainerID, timeSecondsSince2000());

	cbData = CreateAddEntryCBData(pEnt, protoHeader, protoComment, userCB, userData);

	gslDiary_AddEntry(cbData);
}

void
gslDiary_AddMissionEntryUpdateClient(Entity *pEnt, const char *missionNameIn, const char *commentTitle, const char *commentText, U64 tagBits)
{
	gslDiary_AddMissionEntry(pEnt, missionNameIn, commentTitle, commentText, tagBits, SendHeadersToClientCB, NULL);
}

void
gslDiary_AddActivityEntry(Entity *pEnt, U32 activityEntryID, const char *commentTitle, const char *commentText, U64 tagBits, AddEntryCallback userCB, void *userData)
{
	NOCONST(DiaryHeader) *protoHeader;
	NOCONST(DiaryComment) *protoComment;
	AddEntryCBData *cbData;

	static char *s_tmpTitle = NULL;
	static char *s_tmpText = NULL;

	ActivityLogEntry *activityEntry;

	activityEntry = eaIndexedGetUsingInt(&pEnt->pSaved->activityLogEntries, activityEntryID);
	if ( activityEntry == NULL )
	{
		return;
	}

	estrClear(&s_tmpTitle);
	estrClear(&s_tmpText);

	estrAppendEscaped(&s_tmpTitle, commentTitle);
	estrAppendEscaped(&s_tmpText, commentText);

	// create prototype header and comment
	protoHeader = StructCreateNoConst(parse_DiaryHeader);

	protoHeader->hidden = false;
	protoHeader->time = activityEntry->time;
	protoHeader->title = NULL;
	protoHeader->refName = NULL;
	protoHeader->activityEntryID = activityEntry->entryID;
	protoHeader->tagBits = tagBits;
	protoHeader->type = DiaryEntryType_Activity;

	protoComment = CreateProtoComment(s_tmpTitle, s_tmpText, pEnt->myContainerID, timeSecondsSince2000());

	cbData = CreateAddEntryCBData(pEnt, protoHeader, protoComment, userCB, userData);

	gslDiary_AddEntry(cbData);
}

void
gslDiary_AddActivityEntryUpdateClient(Entity *pEnt, U32 activityEntryID, const char *commentTitle, const char *commentText, U64 tagBits)
{
	gslDiary_AddActivityEntry(pEnt, activityEntryID, commentTitle, commentText, tagBits, SendHeadersToClientCB, NULL);
}

static void 
UpdateHeaders_CB(TransactionReturnVal *pReturn, void *pData) 
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)pData));

		if ( pEnt != NULL )
		{
			// update the headers for the client
			gslDiary_SendHeadersToClient(pEnt);
		}
	}
	else
	{
		// some kind of notification on error?
		printf("transaction failed\n");
	}
}

void
gslDiary_RemoveEntry(Entity *pEnt, U32 entryID)
{
	PlayerDiary *diary;

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		TransactionReturnVal *pReturn;
		DiaryHeader *header;

		header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
		// can only currently delete blog type entries
		if ( ( header != NULL ) && ( header->type == DiaryEntryType_Blog ) )
		{
			pReturn = objCreateManagedReturnVal(UpdateHeaders_CB, (void *)((intptr_t)entGetRef(pEnt)));

			AutoTrans_gslDiary_tr_RemoveEntry(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, GLOBALTYPE_DIARYENTRYBUCKET, header->bucketID, entryID);

			return;
		}
	}

	printf("diary or entry not found when removing entry\n");
}

typedef struct UpdateEntryCBData
{
	// use a reference here rather than an EntityRef, because when called on the Web Request server
	//  the EntityRef doesn't work. 
	REF_TO(Entity) playerRef;
	U32 playerID;
	void *userData;
	UpdateEntryCallback userCB;
	U32 entryID;
} UpdateEntryCBData;

static UpdateEntryCBData *
CreateUpdateEntryCBData(Entity *pEnt, U32 entryID, UpdateEntryCallback userCB, void *userData)
{
	char idBuf[128];
	UpdateEntryCBData *cbData = (UpdateEntryCBData *)malloc(sizeof(UpdateEntryCBData));

	cbData->entryID = entryID;
	cbData->userCB = userCB;
	cbData->userData = userData;
	cbData->playerID = pEnt->myContainerID;

	// setting the reference will subscribe to the container
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), cbData->playerRef);

	return cbData;
}

static void
NotifyAndFreeUpdateEntryCBData(bool success, UpdateEntryCBData *cbData)
{
	if ( cbData->userCB != NULL )
	{
		// minor hack here to make sure we use the live entity if it exists, otherwise use the reference system copy.
		// necessary to work on both a game server with real logged on character and web request server.
		Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, cbData->playerID);
		if ( pEnt == NULL )
		{
			pEnt = GET_REF(cbData->playerRef);
		}

		(* cbData->userCB)(success, pEnt, cbData->entryID, cbData->userData);
	}

	REMOVE_HANDLE(cbData->playerRef);

	free(cbData);
}

//
// Generic callback used for any commands that need to push an entry back to the client
// when done.
//
static void
UpdateEntryCB(TransactionReturnVal *pReturn, UpdateEntryCBData *cbData)
{
	NotifyAndFreeUpdateEntryCBData( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS, cbData );
}

static void
UpdateEntryOnClientCB(bool success, Entity *pEnt, U32 entryID, void *userData)
{
	if ( pEnt != NULL )
	{
		if ( userData != NULL )
		{
			gslDiary_SendHeadersToClient(pEnt);
		}
		gslDiary_RequestEntry(pEnt, entryID);
	}
}

void
gslDiary_AddComment(Entity *pEnt, U32 entryID, const char *title, const char *text, UpdateEntryCallback userCB, void *userData)
{
	PlayerDiary *diary;

	static char *s_tmpTitle = NULL;
	static char *s_tmpText = NULL;

	estrClear(&s_tmpTitle);
	estrClear(&s_tmpText);

	estrAppendEscaped(&s_tmpTitle, title);
	estrAppendEscaped(&s_tmpText, text);

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		DiaryHeader *header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
		if ( header != NULL )
		{
			UpdateEntryCBData *cbData;
			TransactionReturnVal *pReturn;

			cbData = CreateUpdateEntryCBData(pEnt, entryID, userCB, userData);

			pReturn = objCreateManagedReturnVal(UpdateEntryCB, cbData);
			
			AutoTrans_gslDiary_tr_AddComment(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, GLOBALTYPE_DIARYENTRYBUCKET, header->bucketID, entryID, s_tmpTitle, s_tmpText);

			return;
		}
	}

	printf("diary or entry not found when adding comment\n");
}

void
gslDiary_AddCommentUpdateClient(Entity *pEnt, U32 entryID, const char *title, const char *text)
{
	gslDiary_AddComment(pEnt, entryID, title, text, UpdateEntryOnClientCB, NULL);
}

static void
RequestEntryCB(Entity *pEnt, PlayerDiary *diary, DiaryEntryBucket *bucket, U32 *pEntryID)
{
	DiaryEntry *entry;

	entry = eaIndexedGetUsingInt(&bucket->entries, *pEntryID);
	if ( entry != NULL )
	{
		ClientCmd_gclDiary_SetCurrentEntry(pEnt, entry);
	}
	else
	{
		printf("unable to send requested entry from callback\n");
	}
}

//
// send the requested log entry to the client
//
void
gslDiary_RequestEntry(Entity *pEnt, U32 entryID)
{
	PlayerDiary *diary;
	DiaryHeader *header;

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
		if ( header != NULL )
		{
			U32 *pEntryID = malloc(sizeof(U32));
			*pEntryID = entryID;
			gslDiary_ValidateBucket(pEnt, header->bucketID, RequestEntryCB, pEntryID);
			return;
		}
	}
	printf("unable to send requested entry\n");
}

void
gslDiary_RemoveComment(Entity *pEnt, U32 entryID, U32 commentID, UpdateEntryCallback userCB, void *userData)
{
	PlayerDiary *diary;

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		UpdateEntryCBData *cbData;
		TransactionReturnVal *pReturn;
		DiaryHeader *header;

		header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
		if ( header != NULL )
		{
			// set up callback data
			cbData = CreateUpdateEntryCBData(pEnt, entryID, userCB, userData);

			pReturn = objCreateManagedReturnVal(UpdateEntryCB, cbData);

			AutoTrans_gslDiary_tr_RemoveComment(pReturn, GLOBALTYPE_GAMESERVER, 
				GLOBALTYPE_DIARYENTRYBUCKET, header->bucketID, entryID, commentID);

			return;
		}
	}

	printf("diary or entry not found when removing comment\n");
}

void
gslDiary_RemoveCommentUpdateClient(Entity *pEnt, U32 entryID, U32 commentID)
{
	gslDiary_RemoveComment(pEnt, entryID, commentID, UpdateEntryOnClientCB, NULL);
}

void
gslDiary_EditComment(Entity *pEnt, U32 entryID, U32 commentID, const char *title, const char *text, UpdateEntryCallback userCB, void *userData)
{
	PlayerDiary *diary;

	static char *s_tmpTitle = NULL;
	static char *s_tmpText = NULL;

	estrClear(&s_tmpTitle);
	estrClear(&s_tmpText);

	estrAppendEscaped(&s_tmpTitle, title);
	estrAppendEscaped(&s_tmpText, text);

	diary = GET_REF(pEnt->pSaved->hDiary);
	if ( diary != NULL )
	{
		UpdateEntryCBData *cbData;
		TransactionReturnVal *pReturn;
		DiaryHeader *header;

		header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
		if ( header != NULL )
		{
			// set up callback data
			cbData = CreateUpdateEntryCBData(pEnt, entryID, userCB, userData);

			pReturn = objCreateManagedReturnVal(UpdateEntryCB, cbData);

			AutoTrans_gslDiary_tr_EditComment(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, GLOBALTYPE_DIARYENTRYBUCKET, header->bucketID, entryID, commentID, s_tmpTitle, s_tmpText);

			return;
		}
	}

	printf("diary or entry not found when editing comment\n");
}

void
gslDiary_EditCommentUpdateClient(Entity *pEnt, U32 entryID, U32 commentID, const char *title, const char *text)
{
	gslDiary_EditComment(pEnt, entryID, commentID, title, text, UpdateEntryOnClientCB, (void *)((intptr_t)1));
}

static void
UpdateTagString_CB(TransactionReturnVal *pReturn, void *cbData)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)cbData));

		if ( pEnt != NULL )
		{
			gslDiary_SendTagStringsToClient(pEnt);

			// deleting tags can change headers too, since tag bits can be cleared
			gslDiary_SendHeadersToClient(pEnt);

			// also need to send default filter
			gslDiary_SendDefaultFilterToClient(pEnt);
		}
	}
	else
	{
		printf("transaction to update tag strings failed\n");
	}
}

void
gslDiary_AddTagString(Entity *pEnt, const char *tagString)
{
	PlayerDiary *diary;
	DiaryTagStrings *newStrings;
	TransactionReturnVal *pReturn;
	const char *pooledTagString;
	NOCONST(DiaryTagString) *newString;

	diary = GET_REF(pEnt->pSaved->hDiary);

	if ( ( diary != NULL ) && ( tagString != NULL ) && ( strlen(tagString) <= DIARY_MAX_TAG_LENGTH ) )
	{
		int i;
		int n;
		pooledTagString = allocAddString(tagString);

		// return if the player already has the requested tag
		n = eaSize(&diary->tags);
		for ( i = 0; i < n; i++ )
		{
			if ( diary->tags[i]->tagName == pooledTagString )
			{
				return;
			}
		}

		// if the player's list of tags is empty, then copy the default tags to their list as well as the new tag
		if ( eaSize(&diary->tags) == 0 )
		{
			newStrings = DiaryConfig_CopyDefaultTagStrings();
		}
		else
		{
			newStrings = StructCreate(parse_DiaryTagStrings);
		}

		newString = StructCreateNoConst(parse_DiaryTagString);
		newString->permanent = false;
		newString->tagName = pooledTagString;

		eaPush(&newStrings->tagStrings, (DiaryTagString *)newString);

		pReturn = objCreateManagedReturnVal(UpdateTagString_CB, (void *)((intptr_t)entGetRef(pEnt)));

		AutoTrans_gslDiary_tr_AddTagStrings(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, newStrings);

		StructDestroy(parse_DiaryTagStrings, newStrings);
	}
}

void
gslDiary_RemoveTagString(Entity *pEnt, const char *tagString)
{
	PlayerDiary *diary;
	DiaryTagStrings *removeStrings;
	TransactionReturnVal *pReturn;
	const char *pooledTagString;

	diary = GET_REF(pEnt->pSaved->hDiary);

	if ( ( diary != NULL ) && ( tagString != NULL ) )
	{
		int n;
		int i;
		bool found = false;
		NOCONST(DiaryTagString) *removeString;

		pooledTagString = allocAddString(tagString);

		n = eaSize(&diary->tags);
		for ( i = 0; i < n; i++ )
		{
			DiaryTagString *tmpTagString = diary->tags[i];
			if ( tmpTagString->tagName == pooledTagString )
			{
				found = true;
				break;
			}
		}

		if ( found == false )
		{
			// string is not in the player's list, so just return
			return;
		}

		removeStrings = StructCreate(parse_DiaryTagStrings);
		removeString = StructCreateNoConst(parse_DiaryTagString);
		removeString->permanent = false;
		removeString->tagName = pooledTagString;

		eaPush(&removeStrings->tagStrings, (DiaryTagString *)removeString);

		pReturn = objCreateManagedReturnVal(UpdateTagString_CB, (void *)((intptr_t)entGetRef(pEnt)));

		AutoTrans_gslDiary_tr_RemoveTagStrings(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, removeStrings);

		StructDestroy(parse_DiaryTagStrings, removeStrings);
	}
}

void
gslDiary_SetEntryTags(Entity *pEnt, U32 entryID, U64 tagBits)
{
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);

	if ( diary != NULL )
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(UpdateHeaders_CB, (void *)((intptr_t)entGetRef(pEnt)));
		AutoTrans_gslDiary_tr_SetEntryTags(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, entryID, tagBits);
	}
}

void
gslDiary_SendDefaultFilterToClient(Entity *pEnt)
{
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);

	if ( diary != NULL )
	{
		ClientCmd_gclDiary_SetDefaultFilter(pEnt, &diary->defaultFilter);
	}
}

static void
UpdateDefaultFilter_CB(TransactionReturnVal *pReturn, void *cbData)
{
	if ( pReturn->eOutcome == TRANSACTION_OUTCOME_SUCCESS )
	{
		Entity *pEnt = (Entity*)entFromEntityRefAnyPartition((EntityRef)((intptr_t)cbData));

		if ( pEnt != NULL )
		{
			gslDiary_SendDefaultFilterToClient(pEnt);
		}
	}
	else
	{
		printf("transaction to default filter failed\n");
	}
}

void
gslDiary_SetDefaultFilter(Entity *pEnt, DiaryFilter *protoFilter)
{
	PlayerDiary *diary = GET_REF(pEnt->pSaved->hDiary);

	if ( diary != NULL )
	{
		TransactionReturnVal *pReturn = objCreateManagedReturnVal(UpdateDefaultFilter_CB, (void *)((intptr_t)entGetRef(pEnt)));
		AutoTrans_gslDiary_tr_SetDefaultFilter(pReturn, GLOBALTYPE_GAMESERVER, GLOBALTYPE_PLAYERDIARY, diary->containerID, protoFilter);
	}
}

void
gslDiary_RunOncePerFrame(void)
{
	gslDiary_BucketManagerRunOncePerFrame();
	gslDiaryWebRequest_RunOncePerFrame();
}

void
DumpDependentContainersCB(TransactionReturnVal *pReturn, void *cbData)
{
	ContainerRefArray *refArray;

	if ( RemoteCommandCheck_DBReturnDependentContainers(pReturn, &refArray) == TRANSACTION_OUTCOME_SUCCESS )
	{
		int i;
		int n = eaSize(&refArray->containerRefs);

		for ( i = 0; i < n; i++ )
		{
			ContainerRef *containerRef;

			containerRef = refArray->containerRefs[i];

			printf("Type: %d, ID: %d\n", containerRef->containerType, containerRef->containerID);
		}
	}
}

void 
gslDiary_DumpDependentContainers(Entity *pEnt)
{
	TransactionReturnVal *pReturn = objCreateManagedReturnVal(DumpDependentContainersCB, NULL);
	RemoteCommand_DBReturnDependentContainers(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID);
}