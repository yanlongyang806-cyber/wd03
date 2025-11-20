/***************************************************************************
*     Copyright (c) 2009, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "DiaryCommon.h"

#include "timing.h"
#include "objTransactions.h"
#include "Entity.h"
#include "EntitySavedData.h"
#include "AutoTransDefs.h"

#include "AutoGen/DiaryCommon_h_ast.h"
#include "AutoGen/Entity_h_ast.h"
#include "AutoGen/EntitySavedData_h_ast.h"

//
// This transaction adds a diary container reference to a player entity.
// This is usually done when the diary container is first created.
//
AUTO_TRANSACTION
ATR_LOCKS(pEnt, ".Psaved.Hdiary")
ATR_LOCKS(diary, ".Currentbucketid, .Containerid");
enumTransactionOutcome
gslDiary_tr_NewDiaryFinalize(ATR_ARGS, NOCONST(Entity) *pEnt, NOCONST(PlayerDiary) *diary, U32 initialBucketID)
{
	char idBuf[128];
	SET_HANDLE_FROM_STRING(GlobalTypeToCopyDictionaryName(GLOBALTYPE_PLAYERDIARY), ContainerIDToString(diary->containerID, idBuf), pEnt->pSaved->hDiary);

	diary->currentBucketID = initialBucketID;

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Update the diary with a new bucket to use for new entries
//
AUTO_TRANSACTION
ATR_LOCKS(diary, ".Currentbucketid, .Currentbucketcount");
enumTransactionOutcome
gslDiary_tr_NewBucketFinalize(ATR_ARGS, NOCONST(PlayerDiary) *diary, U32 bucketID)
{
	diary->currentBucketID = bucketID;
	diary->currentBucketCount = 0;

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// This transaction adds an entry with an optional comment to the diary,
//  with the entry body going into the provided bucket.
// If no comment is to be provided, then the protoComment argument can be NULL.
//
AUTO_TRANSACTION
ATR_LOCKS(diary, ".Headers.Headers, .Nextentryid, .Nextcommentid, .Currentbucketcount")
ATR_LOCKS(bucket, ".Bucketid, .Entries");
enumTransactionOutcome
gslDiary_tr_AddEntry(ATR_ARGS, NOCONST(PlayerDiary) *diary, NOCONST(DiaryEntryBucket) *bucket, 
					 NON_CONTAINER DiaryHeader *protoHeader, NON_CONTAINER DiaryComment *protoComment )
{
	NOCONST(DiaryHeader) *header;
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;
	int n;
	int i;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) || ISNULL(bucket) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// Make sure that this entry has not already been made.  This is to prevent dual adds
	//  from client and web.
	if ( ( protoHeader->type == DiaryEntryType_Mission ) || ( protoHeader->type == DiaryEntryType_Perk ) )
	{
		n = eaSize(&diary->headers.headers);
		for ( i = 0; i < n; i++ )
		{
			header = diary->headers.headers[i];

			// if an entry already exists for the same mission/perk, then fail
			if ( ( header->type == protoHeader->type ) && ( header->refName == protoHeader->refName ) )
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}
	else if ( protoHeader->type == DiaryEntryType_Activity )
	{
		n = eaSize(&diary->headers.headers);
		for ( i = 0; i < n; i++ )
		{
			header = diary->headers.headers[i];

			// if an entry already exists for the same activity entry, then fail
			if ( ( header->type == protoHeader->type ) && ( header->activityEntryID == protoHeader->activityEntryID ) )
			{
				return TRANSACTION_OUTCOME_FAILURE;
			}
		}
	}

	// Create entry and give it a unique ID
	entry = StructCreateNoConst(parse_DiaryEntry);
	entry->entryID = diary->nextEntryID;

	// increment next entry ID
	diary->nextEntryID++;

	// add a comment if one is provided
	if ( protoComment != NULL )
	{
		// copy comment and give it a unique ID
		comment = StructCloneDeConst(parse_DiaryComment, protoComment);
		devassertmsg(comment != NULL, "got null diary comment from StructClone.");
		comment->id = diary->nextCommentID;

		// increment next comment ID
		diary->nextCommentID++;

		// add comment to entry
		eaPush(&entry->comments, comment);
	}

	// add entry to the bucket
	eaPush(&bucket->entries, entry);

	// increment count of entries in current bucket
	diary->currentBucketCount++;

	// copy prototype header
	header = StructCloneDeConst(parse_DiaryHeader, protoHeader);
	devassertmsg(header != NULL, "got null diary header from StructClone.");

	// header also gets entry ID
	header->entryID = entry->entryID;

	// header gets a ref to the bucket containing the entry body
	header->bucketID = bucket->bucketID;

	// add header to diary
	eaPush(&diary->headers.headers, header);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Add a comment to a diary entry
//
AUTO_TRANSACTION
ATR_LOCKS(diary, ".Entityid, .Nextcommentid")
ATR_LOCKS(bucket, "entries[]");
enumTransactionOutcome
gslDiary_tr_AddComment(ATR_ARGS, NOCONST(PlayerDiary) *diary, NOCONST(DiaryEntryBucket) *bucket, U32 entryID, const char *title, const char *text)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) || ISNULL(bucket) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	entry = eaIndexedGetUsingInt(&bucket->entries, entryID);
	if ( entry == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	comment = StructCreateNoConst(parse_DiaryComment);

	comment->entityID = diary->entityID;
	comment->time = timeSecondsSince2000();
	comment->title = StructAllocString(title);
	comment->text = StructAllocString(text);
	comment->id = diary->nextCommentID;

	// increment next comment ID
	diary->nextCommentID++;

	// add comment to entry
	eaPush(&entry->comments, comment);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Remove an entry from the diary
//
AUTO_TRANSACTION
ATR_LOCKS(diary, ".Headers.Headers")
ATR_LOCKS(bucket, ".Entries");
enumTransactionOutcome
gslDiary_tr_RemoveEntry(ATR_ARGS, NOCONST(PlayerDiary) *diary, NOCONST(DiaryEntryBucket) *bucket, U32 entryID)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryHeader) *header;
	int headerIndex;
	int entryIndex;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) || ISNULL(bucket) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the header to remove
	headerIndex = eaIndexedFindUsingInt(&diary->headers.headers, entryID);
	if ( headerIndex == -1 )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	header = diary->headers.headers[headerIndex];

	// currently can only delete blog entries
	if ( header->type != DiaryEntryType_Blog )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the associated entry
	entryIndex = eaIndexedFindUsingInt(&bucket->entries, entryID);
	if ( entryIndex == -1 )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// remove and free the header
	header = eaRemove(&diary->headers.headers, headerIndex);
	if ( header == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroyNoConst(parse_DiaryHeader, header);

	// remove and free the entry
	entry = eaRemove(&bucket->entries, entryIndex);
	if ( entry == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroyNoConst(parse_DiaryEntry, entry);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Remove a comment from the diary
//
AUTO_TRANSACTION
ATR_LOCKS(bucket, "entries[]");
enumTransactionOutcome
gslDiary_tr_RemoveComment(ATR_ARGS, NOCONST(DiaryEntryBucket) *bucket, U32 entryID, U32 commentID)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;
	int commentIndex;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(bucket) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( ( entryID == 0 ) || ( commentID == 0 ) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the associated entry
	entry = eaIndexedGetUsingInt(&bucket->entries, entryID);
	if ( entry == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the comment
	commentIndex = eaIndexedFindUsingInt(&entry->comments, commentID);
	if ( commentIndex == -1 )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// remove and free the comment
	comment = eaRemove(&entry->comments, commentIndex);
	if ( comment == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructDestroyNoConst(parse_DiaryComment, comment);

	return TRANSACTION_OUTCOME_SUCCESS;
}

//
// Edit an entry from the diary
//
AUTO_TRANSACTION
ATR_LOCKS(diary, "headers.headers[]")
ATR_LOCKS(bucket, "entries[]");
enumTransactionOutcome
gslDiary_tr_EditComment(ATR_ARGS, NOCONST(PlayerDiary) *diary, NOCONST(DiaryEntryBucket) *bucket, U32 entryID, U32 commentID, const char *title, const char *body)
{
	NOCONST(DiaryEntry) *entry;
	NOCONST(DiaryComment) *comment;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) || ISNULL(bucket) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	if ( ( entryID == 0 ) || ( commentID == 0 ) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the associated entry
	entry = eaIndexedGetUsingInt(&bucket->entries, entryID);
	if ( entry == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// find the comment
	comment = eaIndexedGetUsingInt(&entry->comments, commentID);
	if ( comment == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	// free old title and text
	if ( comment->title != NULL )
	{
		StructFreeString(comment->title);
	}

	if ( comment->text != NULL )
	{
		StructFreeString(comment->text);
	}

	// add new title and text
	comment->title = StructAllocString(title);
	comment->text = StructAllocString(body);

	// update time of comment
	comment->time = timeSecondsSince2000();

	// if this is first comment of a personal log, then update the header title
	if (comment == entry->comments[0])
	{
		NOCONST(DiaryHeader) *header;
		header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);

		// only update the title for user created entries
		if ( header->type == DiaryEntryType_Blog )
		{
			if ( header->title != NULL )
			{
				StructFreeString(header->title);
			}
			header->title = StructAllocString(title);
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANS_HELPER_SIMPLE;
static int
FindMatchingTag(NOCONST(DiaryTagString) ***tagsHandle, const char *matchTag)
{
	int i;
	int n;

	n = eaSize(tagsHandle);

	for ( i = 0; i < n; i++ )
	{
		NOCONST(DiaryTagString) *tagString = (*tagsHandle)[i];
		if ( tagString->tagName == matchTag )
		{
			return i;
		}
	}

	return -1;
}

AUTO_TRANS_HELPER_SIMPLE;
static int
FindEmptyTag(NOCONST(DiaryTagString) ***tagsHandle)
{
	int i;
	int n;

	n = eaSize(tagsHandle);

	for ( i = 0; i < n; i++ )
	{
		NOCONST(DiaryTagString) *tagString = (*tagsHandle)[i];
		if ( tagString == NULL )
		{
			return i;
		}
	}

	return -1;
}

AUTO_TRANSACTION
ATR_LOCKS(diary, ".Tags");
enumTransactionOutcome
gslDiary_tr_AddTagStrings(ATR_ARGS, NOCONST(PlayerDiary) *diary, DiaryTagStrings *tagStrings)
{
	int index;
	int i;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	for( i = 0; i < eaSize(&tagStrings->tagStrings); i++ )
	{
		DiaryTagString *tagString = tagStrings->tagStrings[i];

		index = FindMatchingTag(&diary->tags, tagString->tagName);
		if ( index == -1 )
		{
			// tag not already in array, so we need to add it
			NOCONST(DiaryTagString) *newTag = StructCreateNoConst(parse_DiaryTagString);
			newTag->permanent = tagString->permanent;
			newTag->tagName = tagString->tagName;

			// look for an existing spot that is empty
			index = FindEmptyTag(&diary->tags);
			if ( index == -1 )
			{
				// no empty spots

				// if we already have the max number of tags, then fail to add
				if ( eaSize(&diary->tags) >= DIARY_MAX_TAGS_PER_PLAYER )
				{
					return TRANSACTION_OUTCOME_FAILURE;
				}

				// add the tag
				eaPush(&diary->tags, newTag);
			}
			else
			{
				// found an empty spot, so add it here
				diary->tags[index] = newTag;
			}
		}
	}

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(diary, ".Tags, .Defaultfilter.Tagexcludemask, .Defaultfilter.Tagincludemask, .Headers.Headers");
enumTransactionOutcome
gslDiary_tr_RemoveTagStrings(ATR_ARGS, NOCONST(PlayerDiary) *diary, DiaryTagStrings *tagStrings)
{
	int i;
	int index;
	U64 mask = 0;
	U64 tmpMask;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	for ( i = 0; i < eaSize(&tagStrings->tagStrings); i++ )
	{
		DiaryTagString *tagString = tagStrings->tagStrings[i];

		// find the tag string
		index = FindMatchingTag(&diary->tags, tagString->tagName);

		if ( index >= 0 )
		{
			// remove tag string from player's list
			diary->tags[index] = NULL;

			// compute the bit for this tag
			tmpMask = ((U64)1) << index;

			// add the bit for this tag to the mask of all tags to remove
			mask = mask | tmpMask;
		}
	}

	// invert the mask so it becomes the bits we want to keep
	mask = ~mask;

	// traverse all headers and clear the tag bits for the tags we are removing
	FOR_EACH_IN_EARRAY(diary->headers.headers, NOCONST(DiaryHeader), header)
	{
		// clear the bits for the tags we removed
		header->tagBits = header->tagBits & mask;
	}
	FOR_EACH_END;

	// clear any removed tags from the default filter
	diary->defaultFilter.tagExcludeMask = diary->defaultFilter.tagExcludeMask & mask;
	diary->defaultFilter.tagIncludeMask = diary->defaultFilter.tagIncludeMask & mask;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(diary, "headers.headers[]");
enumTransactionOutcome
gslDiary_tr_SetEntryTags(ATR_ARGS, NOCONST(PlayerDiary) *diary, U32 entryID, U64 tagBits)
{
	NOCONST(DiaryHeader) *header;

	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	header = eaIndexedGetUsingInt(&diary->headers.headers, entryID);
	if ( header == NULL )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	header->tagBits = tagBits;

	return TRANSACTION_OUTCOME_SUCCESS;
}

AUTO_TRANSACTION
ATR_LOCKS(diary, ".Defaultfilter");
enumTransactionOutcome
gslDiary_tr_SetDefaultFilter(ATR_ARGS, NOCONST(PlayerDiary) *diary, NON_CONTAINER DiaryFilter *protoFilter)
{
	// make sure we have a valid diary and bucket before continuing
	if ( ISNULL(diary) )
	{
		return TRANSACTION_OUTCOME_FAILURE;
	}

	StructCopyAllDeConst(parse_DiaryFilter, protoFilter, &diary->defaultFilter);

	return TRANSACTION_OUTCOME_SUCCESS;
}