/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "gslDiaryWebRequest.h"
#include "DiaryCommon.h"
#include "DiaryDisplay.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "gslDiaryBucketManager.h"
#include "cmdparse.h"
#include "earray.h"
#include "structInternals.h"
#include "gslDiary.h"
#include "WebRequests.h"

#include "AutoGen/DiaryDisplay_h_ast.h"
#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/gslDiaryWebRequest_h_ast.h"
#include "AutoGen/GameServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/ObjectDB_autogen_RemoteFuncs.h"


static GetEntityCBData **s_pendingEntity = NULL;

static void
GetEntity(ContainerID playerID, GetEntityCallback userCB, void *userData)
{
	char idBuf[128];
	GetEntityCBData *cbData = (GetEntityCBData *)malloc(sizeof(GetEntityCBData));

	cbData->userCB = userCB;
	cbData->userData = userData;

	// setting the reference will subscribe to the container
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(playerID, idBuf), cbData->playerRef);

	eaPush(&s_pendingEntity, cbData);
}

static void
EntityNameLookup_CB(TransactionReturnVal *pReturn, GetEntityCBData *cbData)
{
	enumTransactionOutcome outcome;
	ContainerLocation *returnContainer;

	outcome = RemoteCommandCheck_dbContainerLocationFromPlayerRef(pReturn, &returnContainer);

	if ( outcome == TRANSACTION_OUTCOME_SUCCESS)
	{
		char idBuf[128];
		devassert(returnContainer->containerType == GLOBALTYPE_ENTITYPLAYER);

		// setting the reference will subscribe to the container
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(returnContainer->containerID, idBuf), cbData->playerRef);

		// stick it on the pending entity list
		eaPush(&s_pendingEntity, cbData);
	}

}

//
// The website doesn't always have a container ID, so we allow them to
//  use the characters name@handle instead.  We can't use the XMLRPC
//  feature to automatically look up the entity, because it does not
//  allow the command to do a slow return, so we just do the lookup
//  ourselves.
//
static void
GetEntityByName(const char *name, GetEntityCallback userCB, void *userData)
{
	TransactionReturnVal *pReturn;
	GetEntityCBData *cbData = (GetEntityCBData *)malloc(sizeof(GetEntityCBData));

	cbData->userCB = userCB;
	cbData->userData = userData;

	pReturn = objCreateManagedReturnVal(EntityNameLookup_CB, cbData);

	/*FIXME_VSHARD*/
	RemoteCommand_dbContainerLocationFromPlayerRef(pReturn, GLOBALTYPE_OBJECTDB, 0, GLOBALTYPE_ENTITYPLAYER, name, 0);
}

static void
NotifyAndFreeGetEntityCBData(GetEntityCBData *cbData)
{
	Entity *pEnt = GET_REF(cbData->playerRef);

	// call the callback function
	if ( cbData->userData != NULL )
	{
		(* cbData->userCB)(pEnt, cbData->userData);
	}

	// get rid of the reference
	REMOVE_HANDLE(cbData->playerRef);

	free(cbData);
}

DiaryDisplayHeaders *
gslDiaryWebRequest_GetDisplayHeaders(Entity *pEnt, int lang, int first, int count)
{
	int numDisplayHeaders;
	PlayerDiary *diary;
	DiaryDisplayHeader **displayHeaders = NULL;
	DiaryDisplayHeaders *returnHeaders = NULL;
	DiaryEntryReferences *refs = NULL;
	DiaryHeaders *diaryHeaders = NULL;
	int i;
	int freeCount;
	int copyCount;

	if ( pEnt->pSaved != NULL )
	{
		diary = GET_REF(pEnt->pSaved->hDiary);

		if ( diary != NULL )
		{
			diaryHeaders = &diary->headers;
		}

		// generate the diary entry references for the headers
		refs = DiaryDisplay_CreateDiaryEntryReferences();
		DiaryDisplay_PopulateDiaryEntryReferences(refs, diaryHeaders);

		numDisplayHeaders = DiaryDisplay_GenerateDisplayHeaders(lang, pEnt, diaryHeaders, NULL, &displayHeaders, false, refs);

		DiaryDisplay_FreeDiaryEntryReferences(refs);

		returnHeaders = StructAlloc(parse_DiaryDisplayHeaders);

		if ( numDisplayHeaders < first )
		{
			freeCount = numDisplayHeaders;
		}
		else
		{
			freeCount = first;
		}
		
		// free any headers before the first one we need
		for ( i = 0; i < freeCount; i++ )
		{
			StructDestroy(parse_DiaryDisplayHeader, displayHeaders[i]);
			displayHeaders[i] = NULL;
		}

		if ( numDisplayHeaders > first )
		{
			// copy the headers that we are returning
			if ( numDisplayHeaders >= ( first + count ) )
			{
				copyCount = count;
			}
			else
			{
				copyCount = numDisplayHeaders - first;
			}

			for ( i = 0; i < copyCount; i++ )
			{
				eaPush(&returnHeaders->headers, displayHeaders[i + first]);
				displayHeaders[i+first] = NULL;
			}
		}

		// free any remaining headers
		for ( i = ( first + count ); i < numDisplayHeaders; i++ )
		{
			StructDestroy(parse_DiaryDisplayHeader, displayHeaders[i]);
			displayHeaders[i] = NULL;
		}

		eaDestroy(&displayHeaders);
	}

	return returnHeaders;
}

void
SendDisplayCommentsReturn(bool success, CmdSlowReturnForServerMonitorInfo *slowReturnInfo, DiaryDisplayComments *displayComments)
{
	char *returnString = NULL;

	WebRequestSlow_BuildXMLResponseString(&returnString, parse_DiaryDisplayComments, displayComments);

	DoSlowCmdReturn(success, returnString, slowReturnInfo);

	estrDestroy(&returnString);
}

DiaryDisplayComments *
GetDisplayComments(Entity *pEnt, PlayerDiary *diary, DiaryEntryBucket *bucket, U32 entryID, Language lang)
{
	int numComments;
	DiaryComment *generatedComment;
	DiaryDisplayComments *returnComments;
	static DiaryHeaders *s_emptyHeaders = NULL;
	static DiaryEntry *s_emptyEntry = NULL;

	// Initialize static empty entry and headers.  These are used if there isn't a valid diary or bucket,
	//  which can happen if the diary or entry has not had any user data added to it yet.
	if ( s_emptyHeaders == NULL )
	{
		s_emptyHeaders = StructAlloc(parse_DiaryHeaders);
		s_emptyEntry = StructAlloc(parse_DiaryEntry);
	}

	returnComments = StructAlloc(parse_DiaryDisplayComments);

	if ( ( pEnt != NULL ) && ( pEnt->pSaved != NULL ) )
	{
		DiaryEntry *diaryEntry = NULL;
		DiaryHeaders *diaryHeaders = NULL;
		DiaryDisplayComment **displayComments = NULL;

		if ( bucket != NULL )
		{
			diaryEntry = eaIndexedGetUsingInt(&bucket->entries, entryID);
		}

		if ( diaryEntry == NULL )
		{
			diaryEntry = s_emptyEntry;
		}

		if ( diary == NULL )
		{
			diaryHeaders = s_emptyHeaders;
		}
		else
		{
			diaryHeaders = &diary->headers;
		}

		generatedComment = StructAlloc(parse_DiaryComment);

		numComments = DiaryDisplay_GenerateDisplayComments(lang, pEnt, diaryHeaders, CONTAINER_NOCONST(DiaryComment, generatedComment), diaryEntry, entryID, &displayComments, true);

		if ( numComments == 0 )
		{
			StructDestroy(parse_DiaryComment, generatedComment);
		}
		else
		{
			// generated comment is pointed to by displayComments and will be freed when returnComments is freed by XMLRPC command return code
			returnComments->comments = displayComments;
		}
	}

	return returnComments;
}

void
GetDisplayCommentsCB(Entity *pEnt, PlayerDiary *diary, DiaryEntryBucket *bucket, GetDisplayCommentsCBData *cbData)
{

	DiaryDisplayComments *returnComments = NULL;

	if ( pEnt == NULL )
	{
		pEnt = GET_REF(cbData->playerRef);
	}

	if ( pEnt != NULL )
	{
		if ( diary == NULL )
		{
			diary = GET_REF(pEnt->pSaved->hDiary);
		}

		returnComments = GetDisplayComments(pEnt, diary, bucket, cbData->entryID, cbData->lang);

	}

	// send XMLRPC return
	SendDisplayCommentsReturn(true, cbData->slowReturnInfo, returnComments);

	StructDestroy(parse_DiaryDisplayComments, returnComments);

	StructDestroy(parse_GetDisplayCommentsCBData, cbData);
}

static void
GetDisplayComments_HaveEntity(Entity *pEnt, GetDisplayCommentsCBData *cbData)
{
	PlayerDiary *diary;
	DiaryDisplayComments *returnComments = NULL;
	DiaryHeader *diaryHeader;
	U32 bucketID = 0;
	DiaryEntryBucket *bucket = NULL;
	bool pending = false;
	bool freeReturnComments = false;

	static DiaryDisplayComments *s_emptyDisplayComments = NULL;

	if ( s_emptyDisplayComments == NULL )
	{
		s_emptyDisplayComments = StructAlloc(parse_DiaryDisplayComments);
	}

	// if we don't compute actual return comments, then use the empty one
	returnComments = s_emptyDisplayComments;

	if ( ( pEnt != NULL ) && ( pEnt->pSaved != NULL ) )
	{
		char idBuf[128];

		// Setting the reference will subscribe to the container.
		// We need to keep a reference here because this function may need to 
		//  do another asynchronous data request for the diary entry bucket,
		//  so we will need to keep the entity around even after this function
		//  returns.
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), cbData->playerRef);

		if ( ENTRY_ID_IS_LOCAL(cbData->entryID) )
		{
			// if its a local entry, then just generate the results
			returnComments = GetDisplayComments(pEnt, NULL, NULL, cbData->entryID, cbData->lang);
			freeReturnComments = true;
		}
		else
		{
			diary = GET_REF(pEnt->pSaved->hDiary);
			if ( diary != NULL )
			{
				diaryHeader = eaIndexedGetUsingInt(&diary->headers.headers, cbData->entryID);
				if ( diaryHeader != NULL )
				{
					bucketID = diaryHeader->bucketID;

					bucket = gslDiary_GetBucket(bucketID);
					if ( bucket != NULL )
					{
						// we have the bucket already, so just generate the results
						returnComments = GetDisplayComments(pEnt, diary, bucket, cbData->entryID, cbData->lang);
						freeReturnComments = true;
					}
					else
					{
						gslDiary_RequestBucket(pEnt, bucketID, GetDisplayCommentsCB, cbData);
						pending = true;
					}
				}
			}
		}
		if ( !pending )
		{
			// send XMLRPC return
			SendDisplayCommentsReturn(true, cbData->slowReturnInfo, returnComments);

			if ( freeReturnComments )
			{
				StructDestroy(parse_DiaryDisplayComments, returnComments);
			}

			StructDestroy(parse_GetDisplayCommentsCBData, cbData);
		}
	}
	else
	{
        WebRequestSlow_SendXMLRPCReturn(false, cbData->slowReturnInfo);

		StructDestroy(parse_GetDisplayCommentsCBData, cbData);
	}

}

DiaryDisplayComments *
gslDiaryWebRequest_GetDisplayComments(CmdContext *cmdContext, const char * playerHandle, int lang, U32 entryID)
{
	GetDisplayCommentsCBData *cbData;

	// set up callback data
	cbData = StructAlloc(parse_GetDisplayCommentsCBData);
	cbData->entryID = entryID;
	cbData->lang = lang;
    cbData->slowReturnInfo = WebRequestSlow_SetupSlowReturn(cmdContext);

	// Request the entity.  Callback will be called when it has arrived.
	GetEntityByName(playerHandle, GetDisplayComments_HaveEntity, cbData);

	return NULL;
}

//
// Do the XMLRPC return.
//
static void
AddEntry_CB(bool success, Entity *pEnt, WebAddEntryCBData *cbData)
{
    WebRequestSlow_SendXMLRPCReturn(success, cbData->slowReturnInfo);

	// XXX - need remote command here to notify game server player is logged on to that it needs to update client diary headers

	RemoteCommand_gslDiary_SendHeadersToClient(GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, pEnt->myContainerID);

	StructDestroy(parse_WebAddEntryCBData, cbData);
}

//
// Now that we have the player entity, call the main function to add the entry
//
static void
AddBlogEntry_HaveEntity(Entity *pEnt, WebAddEntryCBData *cbData)
{

	if ( pEnt != NULL )
	{
		char idBuf[128];

		// Setting the reference will subscribe to the container.
		// We need to keep a reference here because this function may need to 
		//  do another asynchronous data request for the diary entry bucket,
		//  so we will need to keep the entity around even after this function
		//  returns.
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), cbData->playerRef);

		gslDiary_AddBlogEntry(pEnt, cbData->title, cbData->text, AddEntry_CB, cbData);
	}
	else
	{
        WebRequestSlow_SendXMLRPCReturn(false, cbData->slowReturnInfo);
		StructDestroy(parse_WebAddEntryCBData, cbData);
	}
	return;
}

bool
gslDiaryWebRequest_AddBlogEntry(CmdContext *cmdContext, const char *playerHandle, const char *title, const char *text)
{
	WebAddEntryCBData *cbData;

	// set up callback data
	cbData = StructAlloc(parse_WebAddEntryCBData);
    cbData->slowReturnInfo = WebRequestSlow_SetupSlowReturn(cmdContext);
	cbData->title = StructAllocString(title);
	cbData->text = StructAllocString(text);

	GetEntityByName(playerHandle, AddBlogEntry_HaveEntity, cbData);

	return true;
}


//
// Do the XMLRPC return for the case when an entry was added as a side effect of adding a comment.
//
static void
AddCommentEntry_CB(bool success, Entity *pEnt, WebAddCommentCBData *cbData)
{
    WebRequestSlow_SendXMLRPCReturn(success, cbData->slowReturnInfo);

	// If the entry ID was local, then a new persisted entry was created, so we need to update headers
	//  on the client if the player is logged on to the game.
	if ( ENTRY_ID_IS_LOCAL(cbData->entryID) )
	{
		RemoteCommand_gslDiary_SendHeadersToClient(GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, pEnt->myContainerID);
	}

	StructDestroy(parse_WebAddCommentCBData, cbData);
}

//
// Do the XMLRPC return for the case when a comment was added.
//
static void
AddComment_CB(bool success, Entity *pEnt, U32 entryID, WebAddCommentCBData *cbData)
{
	WebRequestSlow_SendXMLRPCReturn(success, cbData->slowReturnInfo);

	// If the entry ID was local, then a new persisted entry was created, so we need to update headers
	//  on the client if the player is logged on to the game.
	if ( ENTRY_ID_IS_LOCAL(cbData->entryID) )
	{
		RemoteCommand_gslDiary_SendHeadersToClient(GLOBALTYPE_ENTITYPLAYER, pEnt->myContainerID, pEnt->myContainerID);
	}

	StructDestroy(parse_WebAddCommentCBData, cbData);
}

//
// Now that we have the player entity, call the main function to add the comment
//
static void
AddComment_HaveEntity(Entity *pEnt, WebAddCommentCBData *cbData)
{
	if ( pEnt != NULL )
	{
		char idBuf[128];

		// Setting the reference will subscribe to the container.
		// We need to keep a reference here because this function may need to 
		//  do another asynchronous data request for the diary entry bucket,
		//  so we will need to keep the entity around even after this function
		//  returns.
		SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_ENTITYPLAYER), ContainerIDToString(pEnt->myContainerID, idBuf), cbData->playerRef);

		if ( ENTRY_ID_IS_LOCAL(cbData->entryID) )
		{
			DiaryEntryType entryType;

			// for local IDs we need to create a new entry

			entryType = DiaryDisplay_GetEntryTypeFromLocalID(cbData->entryID);
			if ( entryType == DiaryEntryType_Mission )
			{
				gslDiary_AddMissionEntry(pEnt, DiaryDisplay_GetMissionNameFromLocalID(cbData->entryID), cbData->title, cbData->text, 0, AddCommentEntry_CB, cbData);
			}
			else if ( entryType == DiaryEntryType_Activity )
			{
				gslDiary_AddActivityEntry(pEnt, ACTIVITY_ID_FROM_LOCAL_ID(cbData->entryID), cbData->title, cbData->text, 0, AddCommentEntry_CB, cbData);
			}
			else
			{
				devassert( ( entryType == DiaryEntryType_Mission ) || ( entryType == DiaryEntryType_Activity ) );
			}
		}
		else
		{
			gslDiary_AddComment(pEnt, cbData->entryID, cbData->title, cbData->text, AddComment_CB, cbData);
		}
	}
	else
	{
		WebRequestSlow_SendXMLRPCReturn(false, cbData->slowReturnInfo);
		StructDestroy(parse_WebAddCommentCBData, cbData);
	}
	return;
}

bool
gslDiaryWebRequest_AddComment(CmdContext *cmdContext, const char *playerHandle, U32 entryID, const char *title, const char *text)
{
	WebAddCommentCBData *cbData;

	// set up callback data
	cbData = StructAlloc(parse_WebAddCommentCBData);
	cbData->entryID = entryID;
    cbData->slowReturnInfo = WebRequestSlow_SetupSlowReturn(cmdContext);
	cbData->title = StructAllocString(title);
	cbData->text = StructAllocString(text);

	GetEntityByName(playerHandle, AddComment_HaveEntity, cbData);

	return true;
}

void
gslDiaryWebRequest_RunOncePerFrame(void)
{
	int i = 0;

	while ( i < eaSize(&s_pendingEntity) )
	{
		GetEntityCBData *pending = s_pendingEntity[i];
		Entity *playerEnt;

		playerEnt = GET_REF(pending->playerRef);

		if ( playerEnt != NULL )
		{
			if ( IS_HANDLE_ACTIVE(playerEnt->pSaved->hDiary) && ( GET_REF(playerEnt->pSaved->hDiary) == NULL ) )
			{
				// still waiting for diary to show up
				i++;
			}
			else
			{
				// remove from the pending list
				eaRemove(&s_pendingEntity, i);

				// call callback function and free cbdata.
				NotifyAndFreeGetEntityCBData(pending);

				// don't increment i on this path, since we removed an entry from the array
			}
		}
		else
		{
			i++;
		}
	}
}



#include "gslDiaryWebRequest_h_ast.c"